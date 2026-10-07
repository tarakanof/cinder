#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "cinder_line.h"
#include "improv.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static size_t rpc_frame(uint8_t cmd, const char *const *strs, size_t n, uint8_t *out, size_t cap)
{
    size_t len = improv_result(cmd, strs, n, out, cap);
    if (len) {
        out[7] = IMPROV_TYPE_RPC;
        out[len - 2] = improv_checksum(out, len - 2);
    }
    return len;
}

static cJSON *load(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("FAIL cannot open %s\n", path);
        exit(1);
    }
    static char buf[64 * 1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    cJSON *j = cJSON_Parse(buf);
    if (!j) {
        printf("FAIL cannot parse %s\n", path);
        exit(1);
    }
    return j;
}

static size_t unhex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    for (; s[0] && s[1] && n < cap; s += 2) {
        unsigned v;
        sscanf(s, "%2x", &v);
        out[n++] = (uint8_t)v;
    }
    return n;
}

static const char *str(const cJSON *o, const char *k) { return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, k)); }
static int num(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k)->valueint; }

static void strings_of(const cJSON *v, const char **out, size_t *n)
{
    *n = 0;
    const cJSON *s;
    cJSON_ArrayForEach(s, cJSON_GetObjectItemCaseSensitive(v, "strings")) out[(*n)++] = s->valuestring;
}

static void test_improv_vectors(const cJSON *j)
{
    uint8_t want[IMPROV_FRAME_MAX], got[IMPROV_FRAME_MAX];
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j, "states"))
    {
        size_t wn = unhex(str(v, "frame_hex"), want, sizeof want);
        size_t gn = improv_state((uint8_t)num(v, "state"), got, sizeof got);
        CHECK(gn == wn && memcmp(got, want, wn) == 0, "state %s", str(v, "name"));
    }
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j, "errors"))
    {
        size_t wn = unhex(str(v, "frame_hex"), want, sizeof want);
        size_t gn = improv_error((uint8_t)num(v, "error"), got, sizeof got);
        CHECK(gn == wn && memcmp(got, want, wn) == 0, "error %s", str(v, "name"));
    }
    const char *ss[8];
    size_t n;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j, "rpc"))
    {
        strings_of(v, ss, &n);
        size_t wn = unhex(str(v, "frame_hex"), want, sizeof want);
        size_t gn = rpc_frame((uint8_t)num(v, "command"), ss, n, got, sizeof got);
        CHECK(gn == wn && memcmp(got, want, wn) == 0, "rpc encode %s", str(v, "name"));
        improv_pkt_t pkt;
        improv_rpc_t rpc;
        CHECK(improv_parse(want, wn - 1, &pkt) == IMPROV_OK && pkt.type == IMPROV_TYPE_RPC, "rpc parse %s", str(v, "name"));
        CHECK(improv_parse_rpc(&pkt, &rpc) == IMPROV_OK && rpc.cmd == num(v, "command") && rpc.n == n, "rpc strings %s",
              str(v, "name"));
        for (size_t i = 0; i < n && i < rpc.n; i++) {
            char s[256];
            CHECK(improv_rpc_string(&rpc, (int)i, s, sizeof s) && strcmp(s, ss[i]) == 0, "rpc string %zu of %s", i,
                  str(v, "name"));
        }
    }
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j, "result"))
    {
        strings_of(v, ss, &n);
        size_t wn = unhex(str(v, "frame_hex"), want, sizeof want);
        size_t gn = improv_result((uint8_t)num(v, "command"), ss, n, got, sizeof got);
        CHECK(gn == wn && memcmp(got, want, wn) == 0, "result %s", str(v, "name"));
    }
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j, "invalid"))
    {
        size_t wn = unhex(str(v, "frame_hex"), want, sizeof want);
        improv_pkt_t pkt;
        improv_err_t e = improv_parse(want, wn, &pkt);
        const char *err = str(v, "error");
        if (strcmp(err, "strings") == 0) {
            improv_rpc_t rpc;
            CHECK(e == IMPROV_OK && improv_parse_rpc(&pkt, &rpc) == IMPROV_E_STRINGS, "invalid %s", str(v, "name"));
        } else {
            improv_err_t want_e = strcmp(err, "checksum") == 0  ? IMPROV_E_CHECKSUM
                                  : strcmp(err, "version") == 0 ? IMPROV_E_VERSION
                                  : strcmp(err, "length") == 0  ? IMPROV_E_LENGTH
                                                                : IMPROV_E_SHORT;
            CHECK(e == want_e, "invalid %s: got %d", str(v, "name"), e);
        }
    }
}

static void test_improv_edges(void)
{
    uint8_t out[IMPROV_FRAME_MAX];
    char big[300];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    const char *one[] = {big};
    CHECK(improv_result(3, one, 1, out, sizeof out) == 0, "string > 255 rejected");
    char s200[201];
    memset(s200, 'y', 200);
    s200[200] = 0;
    const char *two[] = {s200, s200};
    CHECK(improv_result(3, two, 2, out, sizeof out) == 0, "payload > 253 rejected");
    CHECK(improv_state(2, out, 5) == 0, "small buffer");
    uint8_t h[] = "IMPROX";
    improv_pkt_t pkt;
    CHECK(improv_parse(h, 6, &pkt) == IMPROV_E_HEADER, "header");
    uint8_t data[] = {1, 4, 3, 'a', 0, 'b'};
    uint8_t fr[32];
    size_t fn = improv_frame(IMPROV_TYPE_RPC, data, sizeof data, fr, sizeof fr);
    improv_rpc_t rpc;
    char s[8];
    CHECK(improv_parse(fr, fn - 1, &pkt) == IMPROV_OK && improv_parse_rpc(&pkt, &rpc) == IMPROV_OK, "nul frame");
    CHECK(!improv_rpc_string(&rpc, 0, s, sizeof s), "nul in string");
    CHECK(!improv_rpc_string(&rpc, 1, s, sizeof s), "missing string");
    CHECK(improv_join_failure_reason(202) && improv_join_failure_reason(15) && improv_join_failure_reason(201), "join failures");
    CHECK(!improv_join_failure_reason(203) && !improv_join_failure_reason(8), "transient reasons");
    {
        improv_join_t j;
        improv_join_reset(&j);
        improv_join_note(&j, 15, -60);
        improv_join_note(&j, 202, -85);
        CHECK(improv_join_failures(&j) == 2, "15 at -60 and 202 count");
        improv_join_note(&j, 203, -60);
        improv_join_note(&j, 200, -90);
        improv_join_note(&j, 8, -60);
        CHECK(improv_join_failures(&j) == 2, "transient reasons neither count nor reset");
        improv_join_note(&j, 201, 0);
        CHECK(improv_join_failures(&j) == 3, "no AP counts");
        improv_join_reset(&j);
        CHECK(improv_join_failures(&j) == 0, "IP resets");
        improv_join_note(&j, 15, -80);
        improv_join_note(&j, 15, 0);
        CHECK(improv_join_failures(&j) == 2, "-80 and unknown RSSI count fully");
        improv_join_reset(&j);
        int n = 0;
        while (improv_join_failures(&j) < 3 && n < 100) {
            improv_join_note(&j, n % 3 == 2 ? 203 : (n % 2 ? 204 : 15), -82);
            n++;
        }
        CHECK(improv_join_failures(&j) == 3 && n == 8, "weak link: reported after %d disconnects", n);
    }
}

typedef struct {
    int frames, lines, too_long;
    char last_line[1100];
    uint8_t last_frame[IMPROV_FRAME_MAX];
    size_t last_frame_len;
} sink_t;

static void feed(prov_rx_t *rx, const uint8_t *b, size_t n, sink_t *s)
{
    for (size_t i = 0; i < n; i++) {
        const uint8_t *o;
        size_t ol;
        switch (prov_rx_feed(rx, b[i], &o, &ol)) {
        case PROV_RX_IMPROV:
            s->frames++;
            memcpy(s->last_frame, o, ol);
            s->last_frame_len = ol;
            break;
        case PROV_RX_LINE:
            s->lines++;
            CHECK(strlen((const char *)o) == ol, "line NUL-terminated");
            snprintf(s->last_line, sizeof s->last_line, "%s", (const char *)o);
            break;
        case PROV_RX_LINE_TOO_LONG: s->too_long++; break;
        case PROV_RX_NONE: break;
        }
    }
}

static void feeds(prov_rx_t *rx, const char *t, sink_t *s) { feed(rx, (const uint8_t *)t, strlen(t), s); }

static void test_demux(void)
{
    static uint8_t buf[1024];
    prov_rx_t rx;
    prov_rx_init(&rx, buf, sizeof buf);
    sink_t s = {0};

    const char *ss[] = {"ab", "123456"};
    uint8_t fr[IMPROV_FRAME_MAX];
    size_t fn = rpc_frame(IMPROV_CMD_WIFI, ss, 2, fr, sizeof fr);
    CHECK(fr[8] == 12 && fr[10] == 0x0a, "vector has a 0x0a payload length byte");
    feeds(&rx, "junk typed in monitor\n", &s);
    feed(&rx, fr, fn, &s);
    CHECK(s.frames == 1 && s.last_frame_len == fn - 1 && memcmp(s.last_frame, fr, fn - 1) == 0, "frame with 0x0a inside");
    improv_pkt_t pkt;
    CHECK(improv_parse(s.last_frame, s.last_frame_len, &pkt) == IMPROV_OK, "demuxed frame parses");

    feeds(&rx, "CINDER1 {\"id\":1,\"op\":\"info\"}\r\n", &s);
    CHECK(s.lines == 1 && strcmp(s.last_line, "{\"id\":1,\"op\":\"info\"}") == 0, "cinder line, CR dropped: %s", s.last_line);

    feeds(&rx, "CINDER1 {\"id\":2,\"name\":\"IMPROV\"}\n", &s);
    CHECK(s.lines == 2 && s.frames == 1 && strstr(s.last_line, "IMPROV"), "IMPROV in JSON");

    uint8_t two[2 * IMPROV_FRAME_MAX];
    size_t a = rpc_frame(IMPROV_CMD_INFO, NULL, 0, two, sizeof two) - 1;
    size_t b = rpc_frame(IMPROV_CMD_SCAN, NULL, 0, two + a, sizeof two - a);
    feed(&rx, two, a + b, &s);
    CHECK(s.frames == 3, "back-to-back frames: %d", s.frames);
    feeds(&rx, "CINDER1 {}\n", &s);
    CHECK(s.lines == 3, "line after frames");

    feeds(&rx, "hello\n\n\nCINDER1x\n", &s);
    CHECK(s.lines == 3, "non-protocol lines dropped");
    char longl[1500];
    memset(longl, 'z', sizeof longl);
    memcpy(longl, "CINDER1 ", 8);
    feed(&rx, (const uint8_t *)longl, sizeof longl, &s);
    feeds(&rx, "\n", &s);
    CHECK(s.too_long == 1 && s.lines == 3, "overlong line");
    memset(longl, 'z', sizeof longl);
    feed(&rx, (const uint8_t *)longl, sizeof longl, &s);
    feeds(&rx, "\nCINDER1 {\"id\":9}\n", &s);
    CHECK(s.too_long == 1 && s.lines == 4 && strcmp(s.last_line, "{\"id\":9}") == 0, "overlong junk line is silent");

    feeds(&rx, "CIN", &s);
    feeds(&rx, "DER1 {\"id\":10}", &s);
    feeds(&rx, "\n", &s);
    CHECK(s.lines == 5 && strcmp(s.last_line, "{\"id\":10}") == 0, "split line");

    uint8_t f2[IMPROV_FRAME_MAX];
    size_t f2n = rpc_frame(IMPROV_CMD_INFO, NULL, 0, f2, sizeof f2);
    feed(&rx, f2, 7, &s);
    prov_rx_idle(&rx);
    feed(&rx, f2, f2n, &s);
    CHECK(s.frames == 4 && s.last_frame_len == f2n - 1, "frame after a cut-short one: %d", s.frames);
    feeds(&rx, "CINDER1 {\"id\"", &s);
    prov_rx_idle(&rx);
    feeds(&rx, ":11}\n", &s);
    CHECK(s.lines == 6 && strcmp(s.last_line, "{\"id\":11}") == 0, "idle keeps text");
}

static void test_requests(const cJSON *j)
{
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j, "requests"))
    {
        const char *line = str(v, "line");
        CHECK(strncmp(line, CL_PREFIX, 8) == 0, "prefix %s", str(v, "name"));
        cl_req_t r;
        cl_err_t e = cl_parse(line + 8, &r);
        const char *err = str(v, "error");
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(v, "id");
        if (cJSON_IsNumber(id)) CHECK(r.has_id && r.id == id->valueint, "id %s", str(v, "name"));
        else CHECK(!r.has_id, "no id %s", str(v, "name"));
        if (err) {
            CHECK(e != CL_OK && strcmp(cl_err_name(e), err) == 0, "%s: got %s", str(v, "name"), cl_err_name(e));
            continue;
        }
        CHECK(e == CL_OK, "%s: %s", str(v, "name"), cl_err_name(e));
        const char *op = str(v, "op");
        static const char *const OPS[] = {"info", "set_ember", "status", "reset", "reboot"};
        CHECK(strcmp(OPS[r.op], op) == 0, "op %s", str(v, "name"));
        if (r.op == CL_OP_SET_EMBER) {
            CHECK(strcmp(r.url, str(v, "url")) == 0, "url %s", r.url);
            CHECK(strcmp(r.device_id, str(v, "device_id")) == 0, "device_id");
            CHECK(strcmp(r.token, str(v, "token")) == 0, "token");
            CHECK(strcmp(r.name, str(v, "name")) == 0, "name");
        }
        if (r.op == CL_OP_RESET) {
            static const char *const SC[] = {"factory", "ember", "wifi"};
            CHECK(strcmp(SC[r.scope], str(v, "scope")) == 0, "scope");
        }
    }
    cl_req_t r;
    CHECK(cl_parse("[1,2]", &r) == CL_E_BAD_JSON, "array");
    CHECK(cl_parse("{\"id\":\"x\",\"op\":\"info\"}", &r) == CL_E_BAD_JSON, "string id");
    CHECK(cl_parse("{\"op\":\"info\"}", &r) == CL_OK && !r.has_id, "no id is fine");
    CHECK(cl_parse("{\"id\":1}", &r) == CL_E_BAD_JSON && r.has_id, "no op");
    CHECK(cl_parse("{\"id\":1,\"op\":\"set_ember\",\"url\":\"http://h\",\"device_id\":\"a b\",\"token\":\"t\"}", &r) ==
              CL_E_BAD_VALUE, "device_id chars");
    CHECK(cl_parse("{\"id\":1,\"op\":\"set_ember\",\"url\":\"http://h\",\"device_id\":\"a\",\"token\":\"t t\"}", &r) ==
              CL_E_BAD_VALUE && r.token[0] == 0, "token with space, wiped");
    CHECK(cl_parse("{\"id\":1,\"op\":\"set_ember\",\"url\":\"http://h\",\"device_id\":\"a\"}", &r) == CL_E_BAD_JSON,
          "token missing");
}

static void test_replies(const cJSON *j)
{
    char out[CL_LINE_MAX];
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j, "replies"))
    {
        const char *kind = str(v, "kind"), *want = str(v, "line");
        const cJSON *idj = cJSON_GetObjectItemCaseSensitive(v, "id");
        bool has_id = cJSON_IsNumber(idj);
        long id = has_id ? idj->valueint : 0;
        size_t n = 0;
        if (strcmp(kind, "ok") == 0) {
            n = cl_reply_ok(has_id, id, out, sizeof out);
        } else if (strcmp(kind, "error") == 0) {
            const char *e = str(v, "error");
            cl_err_t ce = CL_E_BAD_JSON;
            for (int k = CL_E_BAD_JSON; k <= CL_E_BAD_VALUE; k++)
                if (strcmp(cl_err_name((cl_err_t)k), e) == 0) ce = (cl_err_t)k;
            n = cl_reply_error(has_id, id, ce, out, sizeof out);
        } else if (strcmp(kind, "info") == 0) {
            cl_info_t in = {
                .fw = str(v, "fw"),
                .hw_id = str(v, "hw_id"),
                .device_id = str(v, "device_id"),
                .wifi_configured = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v, "wifi_configured")),
                .ember_configured = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v, "ember_configured")),
            };
            n = cl_reply_info(id, &in, out, sizeof out);
        } else if (strcmp(kind, "status") == 0) {
            const cJSON *rssi = cJSON_GetObjectItemCaseSensitive(v, "rssi");
            const cJSON *lc = cJSON_GetObjectItemCaseSensitive(v, "last_checkin_s");
            const cJSON *cv = cJSON_GetObjectItemCaseSensitive(v, "config_version");
            cl_status_t st = {
                .wifi_state = str(v, "wifi_state"),
                .ssid = str(v, "ssid"),
                .ip = str(v, "ip"),
                .has_rssi = cJSON_IsNumber(rssi),
                .rssi = cJSON_IsNumber(rssi) ? rssi->valueint : 0,
                .ember_state = str(v, "ember_state"),
                .last_checkin_s = cJSON_IsNumber(lc) ? lc->valueint : -1,
                .config_version = cJSON_IsNumber(cv) ? cv->valueint : -1,
                .internal_free = (uint32_t)num(v, "internal_free"),
                .internal_largest = (uint32_t)num(v, "internal_largest"),
                .psram_free = (uint32_t)num(v, "psram_free"),
            };
            n = cl_reply_status(id, &st, out, sizeof out);
        } else if (strcmp(kind, "event_boot") == 0) {
            n = cl_event_boot(str(v, "fw"), cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v, "provisioned")), out, sizeof out);
        } else if (strcmp(kind, "event_ember") == 0) {
            n = cl_event_ember(str(v, "state"), out, sizeof out);
        } else if (strcmp(kind, "event_wifi") == 0) {
            n = cl_event_wifi(str(v, "state"), out, sizeof out);
        }
        CHECK(n == strlen(want) && strcmp(out, want) == 0, "%s\n  got  %s  want %s", kind, out, want);
    }
    CHECK(cl_reply_ok(true, 1, out, 10) == 0, "small buffer");
}

static void test_diag_override(void)
{
    cl_req_t r;
    CHECK(cl_parse("{\"id\":4,\"op\":\"diag_override\",\"stats_s\":120,\"live_s\":2}", &r) == CL_OK &&
              r.op == CL_OP_DIAG_OVERRIDE && r.stats_s == 120 && r.live_s == 2 && r.id == 4,
          "both set");
    CHECK(cl_parse("{\"op\":\"diag_override\"}", &r) == CL_OK && r.stats_s == -1 && r.live_s == -1,
          "absent: read only");
    CHECK(cl_parse("{\"op\":\"diag_override\",\"stats_s\":0,\"live_s\":null}", &r) == CL_OK && r.stats_s == 0 &&
              r.live_s == -1,
          "0 clears, null leaves");
    CHECK(cl_parse("{\"op\":\"diag_override\",\"stats_s\":4}", &r) == CL_E_BAD_VALUE, "stats below 5 s");
    CHECK(cl_parse("{\"id\":9,\"op\":\"snapshot\"}", &r) == CL_OK && r.op == CL_OP_SNAPSHOT && r.id == 9,
          "snapshot (dev tool)");
    CHECK(cl_parse("{\"id\":30,\"op\":\"ota_fault\",\"fault\":\"net\"}", &r) == CL_OK && r.op == CL_OP_OTA_FAULT &&
              strcmp(r.fault, "net") == 0,
          "ota_fault net (test builds)");
    CHECK(cl_parse("{\"id\":31,\"op\":\"ota_fault\",\"fault\":\"sha\"}", &r) == CL_OK && strcmp(r.fault, "sha") == 0,
          "ota_fault sha");
    CHECK(cl_parse("{\"id\":32,\"op\":\"ota_fault\",\"fault\":\"crash_boot\"}", &r) == CL_E_BAD_VALUE,
          "boot faults are build-time only");
    CHECK(cl_parse("{\"id\":33,\"op\":\"ota_fault\"}", &r) == CL_E_BAD_JSON, "ota_fault without a fault");
    CHECK(cl_parse("{\"id\":10,\"op\":\"chase\"}", &r) == CL_OK && r.op == CL_OP_CHASE && r.id == 10 && r.chase_style == 0,
          "chase (dev tool), full by default");
    CHECK(cl_parse("{\"id\":11,\"op\":\"chase\",\"style\":\"half\"}", &r) == CL_OK && r.chase_style == 1, "chase half");
    CHECK(cl_parse("{\"id\":12,\"op\":\"chase\",\"style\":\"full\"}", &r) == CL_OK && r.chase_style == 0, "chase full");
    CHECK(cl_parse("{\"id\":13,\"op\":\"chase\",\"style\":\"spin\"}", &r) == CL_E_BAD_VALUE, "chase bad style");
    CHECK(cl_parse("{\"id\":19,\"op\":\"chase\"}", &r) == CL_OK && r.chase_fps == 0 && r.chase_laps == 0,
          "chase without fps/laps keeps them");
    CHECK(cl_parse("{\"id\":20,\"op\":\"chase\",\"style\":\"full\",\"fps\":60}", &r) == CL_OK && r.chase_fps == 60,
          "chase fps 60");
    CHECK(cl_parse("{\"id\":15,\"op\":\"chase\",\"fps\":32,\"laps\":10}", &r) == CL_OK && r.chase_fps == 32 &&
              r.chase_laps == 10 && r.chase_style == 0,
          "chase fps 32, 10 laps");
    CHECK(cl_parse("{\"id\":16,\"op\":\"chase\",\"fps\":120}", &r) == CL_E_BAD_VALUE, "chase fps too high");
    CHECK(cl_parse("{\"id\":17,\"op\":\"chase\",\"fps\":\"60\"}", &r) == CL_E_BAD_JSON, "chase fps not a number");
    CHECK(cl_parse("{\"id\":18,\"op\":\"chase\",\"laps\":21}", &r) == CL_E_BAD_VALUE, "chase laps too many");
    CHECK(cl_parse("{\"id\":14,\"op\":\"input\",\"input\":\"push\"}", &r) == CL_OK && r.op == CL_OP_INPUT &&
              r.input == CL_INPUT_PUSH && r.input_n == 1,
          "input push (dev tool)");
    CHECK(cl_parse("{\"id\":15,\"op\":\"input\",\"input\":\"turn\",\"n\":-5}", &r) == CL_OK &&
              r.input == CL_INPUT_TURN && r.input_n == -5,
          "input turn -5");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"push\",\"n\":2}", &r) == CL_OK && r.input_n == 2, "double push");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"turn\",\"n\":0}", &r) == CL_E_BAD_VALUE, "turn 0");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"turn\",\"n\":25}", &r) == CL_E_BAD_VALUE, "turn 25");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"long\",\"n\":4}", &r) == CL_E_BAD_VALUE, "4 long pushes");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"push\",\"n\":1.5}", &r) == CL_E_BAD_JSON, "n not whole");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"jump\"}", &r) == CL_E_BAD_VALUE, "unknown input");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"swipe_up\"}", &r) == CL_OK && r.input == CL_INPUT_SWIPE_UP &&
              r.input_n == 1,
          "swipe up (dev tool)");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"swipe_down\",\"n\":3}", &r) == CL_OK &&
              r.input == CL_INPUT_SWIPE_DOWN && r.input_n == 3,
          "three swipes down");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"swipe_up\",\"n\":-1}", &r) == CL_E_BAD_VALUE, "swipe n < 1");
    CHECK(cl_parse("{\"op\":\"input\"}", &r) == CL_E_BAD_JSON, "input missing");
    CHECK(cl_parse("{\"op\":\"input\",\"input\":\"page\",\"n\":-1}", &r) == CL_OK && r.input == CL_INPUT_PAGE &&
              r.input_n == -1,
          "page back");
    {
        char buf[128];
        cl_reply_error(true, 10, CL_E_NOT_WORKING, buf, sizeof buf);
        CHECK(strstr(buf, "not_working") != NULL, "not_working error: %s", buf);
    }
    char line[CL_LINE_MAX];
    CHECK(cl_reply_error(true, 9, CL_E_BUSY, line, sizeof line) && strstr(line, "\"busy\""), "busy error name");
    CHECK(cl_reply_error(true, 9, CL_E_FAILED, line, sizeof line) && strstr(line, "\"failed\""), "failed error name");
    CHECK(cl_parse("{\"op\":\"diag_override\",\"live_s\":61}", &r) == CL_E_BAD_VALUE, "live above 60 s");
    CHECK(cl_parse("{\"op\":\"diag_override\",\"live_s\":2.5}", &r) == CL_E_BAD_JSON, "not an integer");
    CHECK(cl_parse("{\"op\":\"diag_override\",\"stats_s\":\"60\"}", &r) == CL_E_BAD_JSON, "string");

    char out[CL_LINE_MAX];
    cl_diag_t d = {.diagnostics = "full", .checkin_s = 30, .stats_s = 30, .live_s = 2, .override = true,
                   .requests = 1234, .rx_bytes = 567890, .tx_bytes = 345678, .uptime_ms = 3600123};
    size_t n = cl_reply_diag(4, &d, out, sizeof out);
    const char *want = "CINDER1 {\"checkin_s\":30,\"diagnostics\":\"full\",\"id\":4,\"live_s\":2,\"ok\":true,\"override\":true,\"requests\":1234,"
                       "\"rx_bytes\":567890,\"stats_s\":30,\"tx_bytes\":345678,\"uptime_ms\":3600123}\n";
    CHECK(n == strlen(want) && strcmp(out, want) == 0, "diag reply: %s", out);

    cl_status_t st = {.wifi_state = "connected", .ember_state = "ok", .last_checkin_s = -1, .config_version = -1,
                      .diag_override = true, .diag_stats_s = 120, .diag_live_s = 2};
    n = cl_reply_status(5, &st, out, sizeof out);
    CHECK(n && strncmp(out, "CINDER1 {\"diag\":{\"live_s\":2,\"override\":true,\"stats_s\":120},\"ember\":", 63) == 0,
          "status shows an active override: %s", out);
    st.diag_override = false;
    n = cl_reply_status(5, &st, out, sizeof out);
    CHECK(n && strstr(out, "\"diag\"") == NULL, "no override: status unchanged (vectors): %s", out);
}

int main(int argc, char **argv)
{
    test_diag_override();
    const char *dir = argc > 1 ? argv[1] : "../vectors";
    cJSON *imp = load(dir, "improv.json"), *cl = load(dir, "cinder1.json");
    test_improv_vectors(imp);
    test_improv_edges();
    test_demux();
    test_requests(cl);
    test_replies(cl);
    cJSON_Delete(imp);
    cJSON_Delete(cl);
    if (failures) {
        printf("provision: %d failure(s)\n", failures);
        return 1;
    }
    printf("provision: all tests passed\n");
    return 0;
}
