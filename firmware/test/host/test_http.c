#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/eventfd.h>
#endif

#include "config_store.h"
#include "device_api.h"
#include "diag.h"
#include "ember_legacy.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "http_conn.h"
#include "knob_view.h"
#include "link_state.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct esp_http_client {
    http_event_handle_cb on_event;
    void *user_data;
    bool open;
};

static struct {
    struct esp_http_client client;
    int status;
    const char *body;
    int body_len;
    int sent;
    int chunk;
    bool cut_short;
    bool chunked;
    bool missing_final;
    int read_err_at;
    bool open_fail;
    bool headers_fail;
    bool write_fail;
    int opens;
    int closes;
    int inits;
    int buffer_size;
    int requests_ok;
    int requests_failed;
    int warns;
    int64_t now_us;
    bool has_token;
    int auth_headers;
    bool headers_fail_once;
    char idem[32];
    char idem_sent[4][32];
    int idem_n;
} F;

static void server(int status, const char *body, int len)
{
    F.status = status;
    F.body = body;
    F.body_len = len;
    F.sent = 0;
    F.cut_short = F.chunked = F.missing_final = F.open_fail = F.headers_fail = F.write_fail = false;
    F.read_err_at = 0;
}

void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
    if (level == ESP_LOG_WARN) F.warns++;
}

int64_t esp_timer_get_time(void) { return F.now_us += 1000; }

void diag_note_request(bool ok, uint32_t ms)
{
    (void)ms;
    if (ok) F.requests_ok++;
    else F.requests_failed++;
}

void config_store_token(char *out, size_t cap)
{
    if (cap) snprintf(out, cap, "%s", F.has_token ? "ekd_fake" : "");
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    memset(&F.client, 0, sizeof F.client);
    F.inits++;
    F.buffer_size = config->buffer_size;
    F.client.on_event = config->event_handler;
    F.client.user_data = config->user_data;
    return &F.client;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url)
{
    (void)client;
    (void)url;
    return ESP_OK;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method)
{
    (void)client;
    (void)method;
    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value)
{
    (void)client;
    if (strcmp(key, "Authorization") == 0) F.auth_headers++;
    if (strcmp(key, "Idempotency-Key") == 0) snprintf(F.idem, sizeof F.idem, "%s", value);
    return ESP_OK;
}

esp_err_t esp_http_client_delete_header(esp_http_client_handle_t client, const char *key)
{
    (void)client;
    if (strcmp(key, "Idempotency-Key") == 0) F.idem[0] = 0;
    return ESP_OK;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len)
{
    (void)write_len;
    F.sent = 0;
    if (F.idem_n < 4) snprintf(F.idem_sent[F.idem_n++], sizeof F.idem_sent[0], "%s", F.idem);
    if (F.open_fail) return ESP_FAIL;
    if (!client->open) {
        F.opens++;
        client->open = true;
        esp_http_client_event_t e = {.event_id = HTTP_EVENT_ON_CONNECTED, .client = client, .user_data = client->user_data};
        client->on_event(&e);
    }
    return ESP_OK;
}

int esp_http_client_write(esp_http_client_handle_t client, const char *buffer, int len)
{
    (void)client;
    (void)buffer;
    return F.write_fail ? -1 : len;
}

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client)
{
    (void)client;
    if (F.headers_fail) return -1;
    if (F.headers_fail_once) {
        F.headers_fail_once = false;
        return -1;
    }
    return F.chunked ? 0 : F.body_len;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    (void)client;
    return F.status;
}

int64_t esp_http_client_get_content_length(esp_http_client_handle_t client)
{
    (void)client;
    return F.chunked ? -1 : F.body_len;
}

int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int len)
{
    (void)client;
    int end = F.cut_short ? F.body_len / 2 : F.body_len;
    int n = end - F.sent;
    if (n > len) n = len;
    if (n > F.chunk) n = F.chunk;
    if (F.read_err_at && F.sent >= F.read_err_at) return -1;
    if (F.read_err_at && n > F.read_err_at - F.sent) n = F.read_err_at - F.sent;
    if (n <= 0) return 0;
    memcpy(buffer, F.body + F.sent, (size_t)n);
    F.sent += n;
    return n;
}

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client)
{
    (void)client;
    return F.sent == F.body_len && !(F.chunked && F.missing_final);
}

bool esp_http_client_is_persistent_connection(esp_http_client_handle_t client)
{
    (void)client;
    return true;
}

int esp_http_client_get_socket(esp_http_client_handle_t client)
{
    (void)client;
    return -1;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t client)
{
    if (client->open) F.closes++;
    client->open = false;
    return ESP_OK;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    client->open = false;
    return ESP_OK;
}

#define DRAIN_MAX (2 * KNOB_VIEW_BUF + 4096)
#define URL "http://ember.test/state"

static char *state_body(int min_len, int *len)
{
    int cap = min_len + 4096;
    char *s = malloc((size_t)cap);
    int n = snprintf(s, (size_t)cap, "{\"render\":{\"running\":1},\"sessions\":[");
    for (int i = 0; n < min_len; i++)
        n += snprintf(s + n, (size_t)(cap - n),
                      "%s{\"source\":\"host%d\",\"state\":\"running\",\"updated_at\":\"2026-10-03T01:31:24Z\"}",
                      i ? "," : "", i);
    n += snprintf(s + n, (size_t)(cap - n), "]}");
    *len = n;
    return s;
}

static char *padded(const char *json, int len)
{
    char *s = malloc((size_t)len + 1);
    memset(s, ' ', (size_t)len);
    memcpy(s, json, strlen(json));
    s[len] = 0;
    return s;
}

typedef struct {
    int status;
    int body_len;
    bool truncated;
    int mood;
    link_outcome_t outcome;
} poll_t;

static char s_buf[KNOB_VIEW_BUF];

static poll_t poll_state(http_conn_t *c, link_level_t level)
{
    static ember_host_session_t sess[32];
    ember_host_info_t host = {0};
    poll_t p = {0};
    const http_req_opts_t o = {.idempotent = true, .body_len = &p.body_len, .truncated = &p.truncated};
    p.status = http_conn_req_opts(c, URL, &o, s_buf, KNOB_VIEW_BUF);
    p.mood = ember_state_answer(level, p.status, p.truncated, s_buf, sess, 32, &host, &p.outcome);
    return p;
}

static void test_anon(http_conn_t *c)
{
    static const char OK[] = "{}";
    char buf[64];
    F.has_token = true;
    static const struct {
        http_req_opts_t o;
        int want;
        const char *what;
    } C[] = {
        {{.post_body = "{}"}, 1, "a POST carries the token"},
        {{.auth = true}, 1, "an auth GET carries the token"},
        {{0}, 0, "a plain GET carries none"},
        {{.post_body = "{}", .anon = true}, 0, "an anon POST carries none"},
        {{.auth = true, .anon = true}, 0, "anon beats auth"},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        server(200, OK, (int)strlen(OK));
        F.auth_headers = 0;
        int st = http_conn_req_opts(c, "http://e/v1/devices/claim", &C[i].o, buf, sizeof buf);
        CHECK(st == 200 && F.auth_headers == C[i].want, "%s (%d Authorization headers, status %d)", C[i].what, F.auth_headers, st);
    }
    F.has_token = false;
}

static void test_fits(http_conn_t *c)
{
    int len;
    char *body = state_body(2000, &len);
    server(200, body, len);
    int closes = F.closes;
    poll_t p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == 200 && p.body_len == len && !p.truncated, "small /state: %d, %d B", p.status, p.body_len);
    CHECK(strcmp(s_buf, body) == 0, "small /state read whole");
    CHECK(p.outcome == LINK_OK && p.mood >= 0, "small /state parses: %d", p.outcome);
    CHECK(F.closes == closes, "small /state keeps the socket");
    free(body);
}

static void test_drained(http_conn_t *c)
{
    int len;
    char *body = state_body(KNOB_VIEW_BUF + 4096, &len);
    server(200, body, len);
    int closes = F.closes;
    poll_t p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == 200 && p.body_len == len, "drained /state: %d, %d of %d B", p.status, p.body_len, len);
    CHECK(p.truncated && s_buf[0] == 0, "drained /state: truncated, prefix dropped");
    CHECK(F.sent == len, "drained /state read to the end");
    CHECK(p.outcome == LINK_SKIP && p.mood == -1, "drained /state is a skip: %d", p.outcome);
    CHECK(F.closes == closes, "drained /state keeps the socket");
    free(body);
}

static void test_past_drain_limit(http_conn_t *c)
{
    int len;
    char *body = state_body(DRAIN_MAX + 8192, &len);
    server(200, body, len);
    http_conn_stats_t before, after;
    http_conn_get_stats(&before);
    int failed = F.requests_failed, closes = F.closes;
    poll_t p = poll_state(c, LINK_ONLINE);
    http_conn_get_stats(&after);
    CHECK(p.status == 200, "past the drain limit the status still comes back: %d", p.status);
    CHECK(p.body_len > DRAIN_MAX && p.body_len < len, "body_len past the limit, short of the whole: %d", p.body_len);
    CHECK(F.sent < len, "stops reading past the limit: %d of %d", F.sent, len);
    CHECK(p.truncated && s_buf[0] == 0, "past the limit: truncated, prefix dropped");
    CHECK(p.outcome == LINK_SKIP, "an answered oversized /state is a skip: %d", p.outcome);
    CHECK(after.failures == before.failures && F.requests_failed == failed, "not counted as a failure");
    CHECK(F.closes == closes + 1, "the socket with unread bytes is dropped");

    link_state_t ls;
    link_state_init(&ls, 0);
    link_state_note(&ls, LINK_OK, 1);
    for (int i = 0; i < 6; i++) link_state_note(&ls, poll_state(c, ls.level).outcome, 100 + i);
    CHECK(ls.level == LINK_ONLINE, "online, oversized /state only: %s", link_level_name(ls.level));
    link_state_init(&ls, 0);
    for (int i = 0; i < 6; i++) link_state_note(&ls, poll_state(c, ls.level).outcome, 200 + i);
    CHECK(ls.level == LINK_BOOT, "boot, oversized /state only: %s", link_level_name(ls.level));

    int opens = F.opens;
    char *small = state_body(100, &len);
    server(200, small, len);
    poll_t q = poll_state(c, LINK_ONLINE);
    CHECK(q.status == 200 && q.outcome == LINK_OK, "next request after a cut: %d", q.status);
    CHECK(F.opens == opens + 1, "next request opens a fresh connection");
    free(small);
    free(body);
}

static void test_limit_edges(http_conn_t *c)
{
    for (int len = DRAIN_MAX; len <= DRAIN_MAX + 1; len++) {
        char *body = padded("{}", len);
        server(200, body, len);
        int closes = F.closes;
        poll_t p = poll_state(c, LINK_ONLINE);
        CHECK(p.status == 200 && p.body_len == len && F.sent == len, "%d B: read whole: %d, %d B", len, p.status,
              p.body_len);
        CHECK(p.truncated && p.outcome == LINK_SKIP, "%d B: truncated skip", len);
        CHECK(F.closes == closes, "%d B: complete, so the socket is kept", len);
        free(body);
    }
}

static void test_cut_logged_once(http_conn_t *c)
{
    int len, slen;
    char *body = state_body(DRAIN_MAX + 8192, &len);
    char *small = state_body(100, &slen);
    server(200, small, slen);
    poll_state(c, LINK_ONLINE);
    int warns = F.warns;
    for (int i = 0; i < 6; i++) {
        server(200, body, len);
        poll_state(c, LINK_ONLINE);
    }
    CHECK(F.warns == warns + 1, "six cuts in a row: one warning, got %d", F.warns - warns);
    server(200, small, slen);
    const http_req_opts_t o = {.post_body = "{}", .idempotent = true};
    http_conn_req_opts(c, "http://ember.test/v1/devices/self/checkin", &o, s_buf, KNOB_VIEW_BUF);
    server(200, body, len);
    poll_state(c, LINK_ONLINE);
    CHECK(F.warns == warns + 1, "a whole answer on another route does not re-arm, got %d", F.warns - warns);
    int dlen;
    char *drained = state_body(KNOB_VIEW_BUF + 4096, &dlen);
    server(200, drained, dlen);
    poll_state(c, LINK_ONLINE);
    server(200, body, len);
    poll_state(c, LINK_ONLINE);
    CHECK(F.warns == warns + 1, "a drained but truncated /state does not re-arm, got %d", F.warns - warns);
    free(drained);
    server(200, small, slen);
    poll_state(c, LINK_ONLINE);
    server(200, body, len);
    poll_state(c, LINK_ONLINE);
    CHECK(F.warns == warns + 2, "a cut after a whole answer warns again, got %d", F.warns - warns);
    free(small);
    free(body);
}

static void test_checkin_cut(http_conn_t *c)
{
    const char *json = "{\"config_version\":8,\"config\":{\"poll_ms\":2500},\"new_token\":\"ekd_abcDEF-_123\"}";
    int lens[] = {(int)strlen(json), KNOB_VIEW_BUF + 100, DRAIN_MAX + 8192};
    for (int i = 0; i < 3; i++) {
        char *body = padded(json, lens[i]);
        server(200, body, lens[i]);
        bool truncated = true;
        const http_req_opts_t o = {.post_body = "{}", .idempotent = true, .truncated = &truncated};
        int status = http_conn_req_opts(c, "http://ember.test/v1/devices/self/checkin", &o, s_buf, KNOB_VIEW_BUF);
        dev_checkin_result_t r;
        dev_checkin_parse(s_buf, &r);
        bool whole = i == 0;
        CHECK(status == 200 && truncated == !whole, "checkin %d B: %d, truncated %d", lens[i], status, truncated);
        CHECK(dev_checkin_answer(status, truncated) == (whole ? DEV_CHECKIN_OK : DEV_CHECKIN_FAILED),
              "checkin %d B: answer", lens[i]);
        CHECK(r.has_new_token == whole && (r.config != NULL) == whole, "checkin %d B: token %d, config %d", lens[i],
              r.has_new_token, r.config != NULL);
        dev_checkin_result_free(&r);
        free(body);
    }
}

static void test_cut_by_server(http_conn_t *c)
{
    int len;
    char *body = state_body(4000, &len);
    server(200, body, len);
    F.cut_short = true;
    http_conn_stats_t before, after;
    http_conn_get_stats(&before);
    poll_t p = poll_state(c, LINK_ONLINE);
    http_conn_get_stats(&after);
    CHECK(p.status == -1, "a body the server cut short fails: %d", p.status);
    CHECK(p.outcome == LINK_FAIL, "and counts: %d", p.outcome);
    CHECK(after.failures > before.failures, "counted in the stats");
    free(body);
}

static void test_read_error(http_conn_t *c)
{
    int len;
    char *body = state_body(4000, &len);
    server(200, body, len);
    F.read_err_at = len / 2;
    poll_t p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == -1 && p.outcome == LINK_FAIL, "a read error mid-body fails: %d", p.status);
    free(body);
}

static void test_chunked(http_conn_t *c)
{
    int len;
    char *body = state_body(4000, &len);
    server(200, body, len);
    F.chunked = true;
    poll_t p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == 200 && p.outcome == LINK_OK, "chunked with its last chunk: %d", p.status);
    server(200, body, len);
    F.chunked = F.missing_final = true;
    p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == -1 && p.outcome == LINK_FAIL, "chunked without its last chunk fails: %d", p.status);
    free(body);
    body = state_body(DRAIN_MAX + 8192, &len);
    server(200, body, len);
    F.chunked = F.missing_final = true;
    p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == 200 && p.truncated && p.outcome == LINK_SKIP, "chunked past the limit is a skip: %d", p.status);
    free(body);
}

static void test_transport_failures(http_conn_t *c)
{
    int len;
    char *body = state_body(100, &len);
    server(200, body, len);
    F.open_fail = true;
    poll_t p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == -1 && p.outcome == LINK_FAIL, "open fails: %d", p.status);
    server(200, body, len);
    F.headers_fail = true;
    p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == -1 && p.outcome == LINK_FAIL, "headers fail: %d", p.status);
    server(200, body, len);
    F.write_fail = true;
    const http_req_opts_t o = {.post_body = "{}"};
    CHECK(http_conn_req_opts(c, URL, &o, s_buf, KNOB_VIEW_BUF) == -1, "write fails");
    free(body);
}

static void test_idem_key_retry(http_conn_t *c)
{
    static const char OK[] = "{}";
    char buf[64];
    const http_req_opts_t get = {.idempotent = true};
    server(200, OK, (int)strlen(OK));
    CHECK(http_conn_req_opts(c, "http://e/warm", &get, buf, sizeof buf) == 200, "warm the socket");
    server(202, OK, (int)strlen(OK));
    F.headers_fail_once = true;
    F.idem_n = 0;
    const http_req_opts_t o = {.post_body = "{}", .idempotent = true, .idem_key = "abc-1"};
    int st = http_conn_req_opts(c, "http://e/v1/controls/action", &o, buf, sizeof buf);
    CHECK(st == 202 && F.idem_n == 2, "a stale socket resends once: %d after %d tries", st, F.idem_n);
    CHECK(strcmp(F.idem_sent[0], "abc-1") == 0 && strcmp(F.idem_sent[1], "abc-1") == 0, "the resend keeps the key: %s, %s", F.idem_sent[0], F.idem_sent[1]);
    CHECK(F.idem[0] == 0, "the next request carries no key");
}

static void test_oversized_error(http_conn_t *c)
{
    char *body = padded("x", DRAIN_MAX + 8192);
    server(503, body, DRAIN_MAX + 8192);
    poll_t p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == 503 && p.truncated, "oversized 503 keeps its status: %d", p.status);
    CHECK(p.outcome == LINK_FAIL, "a 5xx still counts: %d", p.outcome);
    free(body);
}

static void test_state_answer(void)
{
    static ember_host_session_t sess[32];
    ember_host_info_t host = {0};
    const char *ok = "{\"render\":{\"running\":1},\"sessions\":[]}";
    const struct {
        link_level_t level;
        int status;
        bool truncated;
        const char *body;
        link_outcome_t want;
        bool mood;
        const char *what;
    } rows[] = {
        {LINK_ONLINE, 200, false, ok, LINK_OK, true, "whole 200"},
        {LINK_ONLINE, 200, true, ok, LINK_SKIP, false, "truncated 200: skip, not parsed"},
        {LINK_BOOT, 200, true, "", LINK_SKIP, false, "truncated 200 at boot"},
        {LINK_ONLINE, 200, false, "<html>", LINK_FAIL, false, "unparsed 200 that fit"},
        {LINK_ONLINE, 304, false, "", LINK_OK, false, "304"},
        {LINK_ONLINE, 500, true, "", LINK_FAIL, false, "truncated 5xx counts"},
        {LINK_ONLINE, 302, false, "", LINK_FAIL, false, "3xx counts"},
        {LINK_ONLINE, 401, false, "", LINK_SKIP, false, "401"},
        {LINK_ONLINE, -1, false, "", LINK_FAIL, false, "no answer"},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        link_outcome_t o;
        int mood = ember_state_answer(rows[i].level, rows[i].status, rows[i].truncated, rows[i].body, sess, 32, &host, &o);
        CHECK(o == rows[i].want && (mood >= 0) == rows[i].mood, "%s: outcome %d, mood %d", rows[i].what, o, mood);
    }
    CHECK(dev_checkin_answer(200, false) == DEV_CHECKIN_OK, "checkin 200");
    CHECK(dev_checkin_answer(200, true) == DEV_CHECKIN_FAILED, "checkin truncated 200");
    CHECK(dev_checkin_answer(401, false) == DEV_CHECKIN_UNAUTHORIZED, "checkin 401");
    const struct {
        int status;
        bool truncated;
        dev_checkin_outcome_t answer;
        int ota;
    } settle[] = {
        {200, false, DEV_CHECKIN_OK, 200},
        {200, true, DEV_CHECKIN_FAILED, -1},
        {401, false, DEV_CHECKIN_UNAUTHORIZED, 401},
        {401, true, DEV_CHECKIN_UNAUTHORIZED, -1},
        {503, false, DEV_CHECKIN_FAILED, 503},
        {503, true, DEV_CHECKIN_FAILED, -1},
        {-1, false, DEV_CHECKIN_FAILED, -1},
    };
    for (size_t i = 0; i < sizeof settle / sizeof settle[0]; i++) {
        int last = 0;
        dev_checkin_step_t st;
        dev_checkin_settle(settle[i].status, settle[i].truncated, &last, &st);
        CHECK(st.answer == settle[i].answer && st.ota_status == settle[i].ota,
              "settle %d, truncated %d: answer %d, OTA status %d", settle[i].status, settle[i].truncated, st.answer,
              st.ota_status);
    }
    ota_valid_in_t v = {.uptime_ms = 120000, .frame = true, .view_ok = true, .health = OTA_HEALTH_PASS};
    int last = 0;
    dev_checkin_step_t st;
    dev_checkin_settle(200, true, &last, &st);
    v.checkin_ok = st.ota_status == 200;
    CHECK(!ota_valid_ready(&v), "a pending image is not marked valid on a truncated checkin");
    dev_checkin_settle(200, false, &last, &st);
    v.checkin_ok = st.ota_status == 200;
    CHECK(ota_valid_ready(&v), "a whole checkin marks it valid");
    last = 0;
    const struct {
        int status;
        bool truncated;
        bool logs;
        bool warn;
        const char *line;
    } seq[] = {
        {200, false, true, false, "checkin ok"},
        {200, false, false, false, NULL},
        {200, true, true, true, "checkin -> HTTP 200, reply too large: not applied"},
        {200, true, false, false, NULL},
        {-1, false, false, false, NULL},
        {200, false, true, false, "checkin ok"},
        {401, false, true, true, "checkin -> HTTP 401: not paired (token rejected)"},
        {401, false, false, false, NULL},
        {401, true, false, false, NULL},
        {503, true, true, true, "checkin -> HTTP 503"},
        {401, true, true, true, "checkin -> HTTP 401: not paired (token rejected)"},
        {401, false, false, false, NULL},
    };
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        dev_checkin_settle(seq[i].status, seq[i].truncated, &last, &st);
        CHECK(st.log == seq[i].logs && (!st.log || (st.warn == seq[i].warn && strcmp(st.line, seq[i].line) == 0)),
              "checkin log %zu (%d, truncated %d): %d, warn %d, \"%s\"", i, seq[i].status, seq[i].truncated, st.log,
              st.warn, st.line);
    }
    CHECK(dev_checkin_answer(500, true) == DEV_CHECKIN_FAILED && dev_checkin_answer(-1, false) == DEV_CHECKIN_FAILED,
          "checkin 5xx and no answer");
}

static bool sink_all(void *ctx, const uint8_t *p, int n)
{
    (void)p;
    *(int *)ctx += n;
    return true;
}

static void test_rx_buf(void)
{
    http_conn_t c;
    http_conn_init(&c, "test");
    int len;
    char *body = state_body(9000, &len);
    server(200, body, len);
    F.chunk = 4096;
    int inits = F.inits;
    poll_t p = poll_state(&c, LINK_ONLINE);
    CHECK(p.status == 200 && F.inits == inits + 1 && F.buffer_size == 0, "default rx buffer: %d", F.buffer_size);
    http_conn_set_rx_buf(&c, 4096);
    CHECK(c.h == NULL, "a new rx size frees the handle");
    uint8_t piece[4096];
    int got = 0, status = 0;
    const http_stream_opts_t o = {.sink = sink_all, .sink_ctx = &got, .piece = piece, .piece_cap = 4096, .status = &status};
    server(200, body, len);
    CHECK(http_conn_stream(&c, URL, &o) == 200 && got == len, "stream with 4 KB rx: %d B", got);
    CHECK(F.inits == inits + 2 && F.buffer_size == 4096, "stream handle built with 4 KB rx: %d", F.buffer_size);
    http_conn_set_rx_buf(&c, 4096);
    CHECK(c.h != NULL, "the same size keeps the handle");
    http_conn_set_rx_buf(&c, 0);
    server(200, body, len);
    p = poll_state(&c, LINK_ONLINE);
    CHECK(p.status == 200 && F.inits == inits + 3 && F.buffer_size == 0, "back to the default: %d", F.buffer_size);
    http_conn_free(&c);
    free(body);
}

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void wake(int fd)
{
    uint64_t one = 1;
    CHECK(write(fd, &one, sizeof one) == (ssize_t)sizeof one, "wake written");
}

static void *wake_later(void *arg)
{
    int *fd = arg;
    usleep(100 * 1000);
    wake(*fd);
    return NULL;
}

static void test_wake_wait(void)
{
    int64_t t0 = mono_ms();
    CHECK(http_conn_wake_wait(-1, 1000) == -1, "no fd: cannot wait");
    CHECK(mono_ms() - t0 < 250, "no fd returns at once: %lld ms", (long long)(mono_ms() - t0));
    int p[2];
    CHECK(pipe(p) == 0, "pipe: one write per wake, so it drains as the device's eventfd does; coalescing is checked on eventfd below");
    t0 = mono_ms();
    CHECK(http_conn_wake_wait(p[0], 80) == 0, "no wake: times out");
    int64_t took = mono_ms() - t0;
    CHECK(took >= 70 && took < 500, "the timeout is waited, not spun: %lld ms", (long long)took);
    wake(p[1]);
    t0 = mono_ms();
    CHECK(http_conn_wake_wait(p[0], 1000) == 1, "a pending wake returns at once");
    CHECK(mono_ms() - t0 < 250, "pending wake: %lld ms", (long long)(mono_ms() - t0));
    t0 = mono_ms();
    CHECK(http_conn_wake_wait(p[0], 80) == 0, "the wake was drained: the next wait blocks");
    took = mono_ms() - t0;
    CHECK(took >= 70, "drained wake does not spin: %lld ms", (long long)took);
    pthread_t th;
    CHECK(pthread_create(&th, NULL, wake_later, &p[1]) == 0, "thread");
    t0 = mono_ms();
    CHECK(http_conn_wake_wait(p[0], 2000) == 1, "a wake mid-wait ends it");
    took = mono_ms() - t0;
    CHECK(took >= 80 && took < 500, "ends at the wake, not the timeout: %lld ms", (long long)took);
    pthread_join(th, NULL);
    CHECK(http_conn_wake_wait(p[0], 0) == 0, "drained after a mid-wait wake");
    close(p[0]);
    close(p[1]);
    CHECK(http_conn_wake_wait(p[0], 1000) == -1, "a closed fd cannot wait");
#ifdef __linux__
    int e = eventfd(0, 0);
    CHECK(e >= 0, "eventfd");
    wake(e);
    wake(e);
    wake(e);
    CHECK(http_conn_wake_wait(e, 1000) == 1, "eventfd: three wakes, one wait");
    t0 = mono_ms();
    CHECK(http_conn_wake_wait(e, 80) == 0 && mono_ms() - t0 >= 70, "eventfd: one read clears all three");
    wake(e);
    CHECK(http_conn_wake_wait(e, 1000) == 1, "eventfd: a later wake is kept");
    close(e);
#endif
}

int main(void)
{
    test_wake_wait();
    test_rx_buf();
    test_state_answer();
    http_conn_t c;
    for (int chunk = 512; chunk <= 4096; chunk *= 8) {
        F.chunk = chunk;
        http_conn_init(&c, "test");
        test_anon(&c);
        test_fits(&c);
        test_drained(&c);
        test_past_drain_limit(&c);
        test_limit_edges(&c);
        test_cut_logged_once(&c);
        test_checkin_cut(&c);
        test_cut_by_server(&c);
        test_read_error(&c);
        test_chunked(&c);
        test_transport_failures(&c);
        test_idem_key_retry(&c);
        test_oversized_error(&c);
        http_conn_free(&c);
    }
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("http: all tests passed\n");
    return 0;
}
