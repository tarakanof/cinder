#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "claim.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *REPLY = "{\"claim_id\":\"c_0123abcd\",\"code\":\"482913\",\"expires_in_s\":600,\"poll_s\":5}";
static const char *PENDING = "{\"state\":\"pending\",\"expires_in_s\":120}";
static const char *APPROVED =
    "{\"state\":\"approved\",\"device_id\":\"knob-10c0de\",\"name\":\"Knob 10C0DE\",\"token\":\"ekd_TEST-token\"}";
static const char *VERSION_NEW = "{\"binary\":\"ember\",\"features\":[\"firmware_delete_keep\",\"lan_pair\"]}";
static const char *VERSION_OLD = "{\"binary\":\"ember\",\"features\":[\"firmware_delete_keep\",\"device_app_enabled\"]}";

static uint8_t RND[CLAIM_KEY_BYTES];

static int freed_with_token;

static void *hook_malloc(size_t n)
{
    size_t *p = malloc(n + sizeof(size_t));
    if (!p) return NULL;
    *p = n;
    return p + 1;
}

static void hook_free(void *q)
{
    if (!q) return;
    size_t *p = (size_t *)q - 1;
    const char *b = q;
    for (size_t i = 0; i + 14 <= *p; i++) freed_with_token += memcmp(b + i, "ekd_TEST-token", 14) == 0;
    free(p);
}

static void test_b64url(void)
{
    static const struct {
        const char *in, *out;
    } V[] = {{"", ""}, {"f", "Zg"}, {"fo", "Zm8"}, {"foo", "Zm9v"}, {"foob", "Zm9vYg"}, {"fooba", "Zm9vYmE"}, {"foobar", "Zm9vYmFy"}};
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        char out[16];
        CHECK(claim_b64url((const uint8_t *)V[i].in, strlen(V[i].in), out, sizeof out) && strcmp(out, V[i].out) == 0,
              "base64url '%s' = '%s', want '%s'", V[i].in, out, V[i].out);
    }
    uint8_t hi[3] = {0xfb, 0xff, 0xbf};
    char out[8];
    CHECK(claim_b64url(hi, 3, out, sizeof out) && strcmp(out, "-_-_") == 0, "the url alphabet: %s", out);
    uint8_t k[CLAIM_KEY_BYTES];
    memset(k, 0xff, sizeof k);
    char key[CLAIM_KEY_LEN + 1];
    CHECK(claim_b64url(k, sizeof k, key, sizeof key) && strlen(key) == CLAIM_KEY_LEN, "32 bytes are 43 characters (%zu)", strlen(key));
    CHECK(!claim_b64url(k, sizeof k, key, CLAIM_KEY_LEN), "no room for the NUL fails");
}

static void test_bodies(void)
{
    char out[CLAIM_BODY_MAX];
    const char *key = "TEST-claim-key-not-real-0000000000000000000";
    size_t n = claim_open_body("02005e10c0de", "0.9.110", key, out, sizeof out);
    cJSON *got = cJSON_Parse(out);
    cJSON *want = cJSON_Parse("{\"hw_id\":\"02005e10c0de\",\"fw\":\"0.9.110\",\"claim_key\":\"TEST-claim-key-not-real-0000000000000000000\"}");
    CHECK(n == strlen(out) && got && cJSON_Compare(want, got, true), "open body: %s", out);
    cJSON_Delete(got);
    cJSON_Delete(want);
    n = claim_poll_body("c_0123abcd", key, out, sizeof out);
    got = cJSON_Parse(out);
    want = cJSON_Parse("{\"claim_id\":\"c_0123abcd\",\"claim_key\":\"TEST-claim-key-not-real-0000000000000000000\"}");
    CHECK(n == strlen(out) && got && cJSON_Compare(want, got, true), "poll body: %s", out);
    cJSON_Delete(got);
    cJSON_Delete(want);
    CHECK(claim_open_body("02005e10c0de", "0.9.110", key, out, 40) == 0, "too small a buffer: 0");
    CHECK(claim_open_body("02005e10c0de", "0.9\"x", key, out, sizeof out) == 0, "a quote in fw: 0");
    CHECK(claim_open_body("02005e10c0de", "0.9\\x", key, out, sizeof out) == 0, "a backslash in fw: 0");
    CHECK(claim_poll_body("c_\n", key, out, sizeof out) == 0, "a control character in the id: 0");
    char fw[40];
    memset(fw, '9', 32);
    fw[32] = 0;
    CHECK(claim_open_body("02005e10c0de", fw, key, out, sizeof out) > 0, "a 32-character fw fits CLAIM_BODY_MAX");
}

static void test_parse(void)
{
    claim_reply_t r;
    CHECK(claim_reply_parse(REPLY, &r) && strcmp(r.claim_id, "c_0123abcd") == 0 && strcmp(r.code, "482913") == 0 && r.expires_in_s == 600 &&
              r.poll_s == 5,
          "reply parses");
    CHECK(claim_reply_parse("{\"claim_id\":\"c_1\",\"code\":\"000001\"}", &r) && r.expires_in_s == CLAIM_EXPIRES_S_DEFAULT &&
              r.poll_s == CLAIM_POLL_S_DEFAULT,
          "missing timings take the defaults (%d, %d)", r.expires_in_s, r.poll_s);
    CHECK(claim_reply_parse("{\"claim_id\":\"c_1\",\"code\":\"000001\",\"expires_in_s\":99999,\"poll_s\":0}", &r) &&
              r.expires_in_s == CLAIM_EXPIRES_S_MAX && r.poll_s == CLAIM_POLL_S_MIN,
          "timings clamp (%d, %d)", r.expires_in_s, r.poll_s);
    CHECK(claim_reply_parse("{\"claim_id\":\"c_1\",\"code\":\"000001\",\"expires_in_s\":\"x\",\"poll_s\":1000}", &r) &&
              r.expires_in_s == CLAIM_EXPIRES_S_DEFAULT && r.poll_s == CLAIM_POLL_S_MAX,
          "a non-number is the default, a huge poll the maximum (%d, %d)", r.expires_in_s, r.poll_s);
    static const char *const BAD[] = {
        "",
        "[]",
        "{\"code\":\"482913\"}",
        "{\"claim_id\":\"c_1\"}",
        "{\"claim_id\":\"c_1\",\"code\":\"48291\"}",
        "{\"claim_id\":\"c_1\",\"code\":\"4829134\"}",
        "{\"claim_id\":\"c_1\",\"code\":\"48291a\"}",
        "{\"claim_id\":\"c_1\",\"code\":482913}",
        "{\"claim_id\":\"\",\"code\":\"482913\"}",
        "{\"claim_id\":\"c 1\",\"code\":\"482913\"}",
        "{\"claim_id\":\"c_\\\"1\",\"code\":\"482913\"}",
        "{\"claim_id\":\"c_0123456789abcdef0123456789abcdef\",\"code\":\"482913\"}",
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) CHECK(!claim_reply_parse(BAD[i], &r), "bad reply %zu refused: %s", i, BAD[i]);
    CHECK(claim_reply_parse("{\"claim_id\":\"c_0123456789abcdef0123456789abcd\",\"code\":\"482913\"}", &r) && strlen(r.claim_id) == CLAIM_ID_MAX,
          "a 32-character id");

    claim_approved_t a;
    CHECK(claim_poll_parse(PENDING, &a) == CLAIM_POLL_PENDING, "pending");
    memset(&a, 'x', sizeof a);
    CHECK(claim_poll_parse(APPROVED, &a) == CLAIM_POLL_APPROVED && strcmp(a.device_id, "knob-10c0de") == 0 && strcmp(a.name, "Knob 10C0DE") == 0 &&
              strcmp(a.token, "ekd_TEST-token") == 0,
          "approved");
    CHECK(claim_poll_parse("{\"state\":\"approved\",\"device_id\":\"knob-1\",\"token\":\"ekd_x\"}", &a) == CLAIM_POLL_APPROVED && a.name[0] == 0,
          "approved without a name");
    char long_name[160];
    snprintf(long_name, sizeof long_name, "{\"state\":\"approved\",\"device_id\":\"knob-1\",\"token\":\"ekd_x\",\"name\":\"%040d\"}", 0);
    CHECK(claim_poll_parse(long_name, &a) == CLAIM_POLL_APPROVED && a.name[0] == 0, "a name over CFG_NAME_MAX is left out");
    static const char *const FAILED[] = {
        "",
        "{}",
        "{\"state\":\"declined\"}",
        "{\"state\":\"approved\",\"token\":\"ekd_x\"}",
        "{\"state\":\"approved\",\"device_id\":\"knob-1\"}",
        "{\"state\":\"approved\",\"device_id\":\"knob-1\",\"token\":\"\"}",
        "{\"state\":\"approved\",\"device_id\":\"knob-1\",\"token\":\"ekd x\"}",
        "{\"state\":\"approved\",\"device_id\":\"knob 1\",\"token\":\"ekd_x\"}",
        "{\"state\":\"approved\",\"device_id\":\"knob-1\",\"token\":7}",
    };
    for (size_t i = 0; i < sizeof FAILED / sizeof FAILED[0]; i++)
        CHECK(claim_poll_parse(FAILED[i], &a) == CLAIM_POLL_FAILED, "bad poll reply %zu: %s", i, FAILED[i]);
    char big[300];
    snprintf(big, sizeof big, "{\"state\":\"approved\",\"device_id\":\"knob-1\",\"token\":\"%0129d\"}", 0);
    CHECK(claim_poll_parse(big, &a) == CLAIM_POLL_FAILED, "a token over CFG_TOKEN_MAX fails");

    CHECK(claim_server_pairs(VERSION_NEW), "a server with lan_pair");
    CHECK(!claim_server_pairs(VERSION_OLD), "a server without lan_pair");
    CHECK(!claim_server_pairs("{\"binary\":\"ember\"}") && !claim_server_pairs("{\"features\":\"lan_pair\"}") && !claim_server_pairs("") &&
              !claim_server_pairs(NULL) && !claim_server_pairs("{\"features\":[\"lan_pairing\"]}"),
          "no features array, a string, junk, NULL and a longer token are not lan_pair");
}

static int64_t T;

static claim_act_t next(claim_t *c) { return claim_next(c, T); }

static void opened(claim_t *c)
{
    CHECK(next(c) == CLAIM_ACT_OPEN, "open due (phase %d)", c->phase);
    if (claim_needs_key(c)) claim_set_key(c, RND);
    claim_opened(c, 201, REPLY, -1, T);
}

static void start(claim_t *c)
{
    claim_init(c);
    claim_want(c, true, T);
    CHECK(next(c) == CLAIM_ACT_PROBE, "a probe first");
    claim_probed(c, 200, VERSION_NEW, T);
    opened(c);
}

static void test_retry_after(void)
{
    CHECK(claim_retry_after_s("30") == 30 && claim_retry_after_s("0") == 1 && claim_retry_after_s("999999999999") == CLAIM_RETRY_AFTER_S_MAX,
          "Retry-After seconds, clamped");
    CHECK(claim_retry_after_s(NULL) == -1 && claim_retry_after_s("") == -1 && claim_retry_after_s("-5") == -1 &&
              claim_retry_after_s("Wed, 21 Oct 2026 07:28:00 GMT") == -1,
          "absent, negative and a date are -1");
}

static void test_flow(void)
{
    claim_t c;
    char code[CLAIM_CODE_LEN + 1];
    T = 1000;
    claim_init(&c);
    CHECK(next(&c) == CLAIM_ACT_NONE && !claim_code(&c, T, code), "idle does nothing");
    claim_want(&c, true, T);
    CHECK(next(&c) == CLAIM_ACT_PROBE, "want probes the server at once");
    CHECK(!claim_code(&c, T, code), "no code before a claim");
    claim_probed(&c, 200, VERSION_NEW, T);
    CHECK(next(&c) == CLAIM_ACT_OPEN && claim_needs_key(&c), "lan_pair: open at once, a key is needed");
    for (int i = 0; i < CLAIM_KEY_BYTES; i++) RND[i] = (uint8_t)(i * 7 + 1);
    claim_set_key(&c, RND);
    char want_key[CLAIM_KEY_LEN + 1];
    claim_b64url(RND, sizeof RND, want_key, sizeof want_key);
    CHECK(!claim_needs_key(&c) && strcmp(c.key, want_key) == 0, "the key is the random bytes in base64url");
    claim_opened(&c, 201, REPLY, -1, T);
    CHECK(claim_code(&c, T, code) && strcmp(code, "482913") == 0, "the code shows: %s", code);
    CHECK(next(&c) == CLAIM_ACT_NONE, "no poll before poll_s");
    T += 4999;
    CHECK(next(&c) == CLAIM_ACT_NONE, "no poll at 4.999 s");
    T += 1;
    CHECK(next(&c) == CLAIM_ACT_POLL, "a poll at 5 s");
    claim_approved_t a;
    CHECK(claim_polled(&c, 202, PENDING, -1, T, &a) == CLAIM_POLL_PENDING, "pending");
    CHECK(next(&c) == CLAIM_ACT_NONE, "pending waits poll_s");
    T += 5000;
    CHECK(next(&c) == CLAIM_ACT_POLL, "the next poll");
    CHECK(claim_polled(&c, 200, APPROVED, -1, T, &a) == CLAIM_POLL_APPROVED && strcmp(a.token, "ekd_TEST-token") == 0, "approved");
    claim_stored(&c, false);
    CHECK(c.has_key && next(&c) == CLAIM_ACT_NONE, "a failed store keeps the claim and the key");
    T += 5000;
    CHECK(next(&c) == CLAIM_ACT_POLL, "and polls again");
    CHECK(claim_polled(&c, 200, APPROVED, -1, T, &a) == CLAIM_POLL_APPROVED, "the same token again");
    claim_stored(&c, true);
    static const char ZERO[CLAIM_KEY_LEN + 1];
    CHECK(c.phase == CLAIM_IDLE && !c.has_key && memcmp(c.key, ZERO, sizeof ZERO) == 0 && !claim_code(&c, T, code) && next(&c) == CLAIM_ACT_NONE,
          "stored: idle, the key wiped, no code");
}

static void test_expiry(void)
{
    claim_t c;
    char code[CLAIM_CODE_LEN + 1];
    T = 50000;
    start(&c);
    claim_approved_t a;
    T += 5000;
    CHECK(next(&c) == CLAIM_ACT_POLL, "poll");
    CHECK(claim_polled(&c, 202, PENDING, -1, T, &a) == CLAIM_POLL_PENDING, "pending shortens the expiry to 120 s");
    T += 119999;
    CHECK(claim_code(&c, T, code), "the code shows until the server's expiry");
    T += 1;
    CHECK(!claim_code(&c, T, code), "no code once expired");
    CHECK(next(&c) == CLAIM_ACT_OPEN && c.ignored == 1 && claim_needs_key(&c) && c.key[0] == 0,
          "an expired code opens a new claim at once with a new key (ignored %d)", c.ignored);
    claim_set_key(&c, RND);
    claim_opened(&c, 201, REPLY, -1, T);
    T += 600000;
    CHECK(next(&c) == CLAIM_ACT_OPEN && c.ignored == 2, "second ignored code (%d)", c.ignored);
    claim_set_key(&c, RND);
    claim_opened(&c, 201, REPLY, -1, T);
    T += 600000;
    CHECK(next(&c) == CLAIM_ACT_NONE && c.phase == CLAIM_INPUT && c.ignored == 3, "third: waits for input (phase %d)", c.phase);
    T += 24LL * 3600 * 1000;
    CHECK(next(&c) == CLAIM_ACT_NONE && !claim_code(&c, T, code), "a day later still waiting, no code");
    claim_input(&c, T);
    CHECK(next(&c) == CLAIM_ACT_OPEN && c.ignored == 0 && claim_needs_key(&c), "input opens a claim at once, with a new key");
    claim_set_key(&c, RND);
    claim_opened(&c, 201, REPLY, -1, T);
    claim_input(&c, T);
    CHECK(c.phase == CLAIM_SHOW && next(&c) == CLAIM_ACT_NONE, "input while a code shows changes nothing");
    T += 5000;
    CHECK(next(&c) == CLAIM_ACT_POLL, "poll");
    CHECK(claim_polled(&c, 404, "{\"error\":\"claim not found\"}", -1, T, &a) == CLAIM_POLL_GONE && c.ignored == 1 && next(&c) == CLAIM_ACT_OPEN &&
              claim_needs_key(&c) && c.key[0] == 0,
          "a 404 poll (declined, expired, superseded, server restart) opens a new claim at once with a new key, counted as ignored");
    claim_set_key(&c, RND);
    claim_opened(&c, 201, REPLY, -1, T);
    T += 5000;
    next(&c);
    CHECK(claim_polled(&c, 401, "", -1, T, &a) == CLAIM_POLL_GONE && c.ignored == 2, "a 401 poll is gone too");
}

static void test_input_before_three(void)
{
    claim_t c;
    T = 7000;
    start(&c);
    claim_input(&c, T);
    CHECK(c.phase == CLAIM_SHOW, "input with a code up keeps it");
    claim_want(&c, false, T);
    claim_input(&c, T);
    CHECK(c.phase == CLAIM_IDLE && next(&c) == CLAIM_ACT_NONE, "input while not wanted does nothing");
}

static void test_old_server(void)
{
    claim_t c;
    T = 0;
    claim_init(&c);
    claim_want(&c, true, T);
    next(&c);
    claim_probed(&c, 200, VERSION_OLD, T);
    CHECK(next(&c) == CLAIM_ACT_NONE && c.phase == CLAIM_WAIT, "no lan_pair: wait");
    T += CLAIM_OLD_SERVER_MS - 1;
    CHECK(next(&c) == CLAIM_ACT_NONE, "nothing for an hour");
    T += 1;
    CHECK(next(&c) == CLAIM_ACT_PROBE, "probe again after an hour");
    claim_probed(&c, 404, "", T);
    CHECK(next(&c) == CLAIM_ACT_NONE && c.due_ms == T + CLAIM_OLD_SERVER_MS, "a server without /version: an hour");
    claim_input(&c, T);
    CHECK(next(&c) == CLAIM_ACT_NONE, "input does not cut the old-server wait");
    static const int OLD[] = {401, 403, 404, 405};
    for (size_t i = 0; i < sizeof OLD / sizeof OLD[0]; i++) {
        start(&c);
        claim_want(&c, false, T);
        claim_want(&c, true, T);
        next(&c);
        claim_probed(&c, 200, VERSION_NEW, T);
        CHECK(next(&c) == CLAIM_ACT_OPEN, "open");
        claim_opened(&c, OLD[i], "", -1, T);
        CHECK(next(&c) == CLAIM_ACT_NONE && c.phase == CLAIM_WAIT && c.due_ms == T + CLAIM_OLD_SERVER_MS,
              "claim %d: old server, an hour, then a probe", OLD[i]);
        T += CLAIM_OLD_SERVER_MS;
        CHECK(next(&c) == CLAIM_ACT_PROBE, "claim %d: probe after the hour", OLD[i]);
    }
}

static void test_backoff(void)
{
    claim_t c;
    T = 0;
    claim_init(&c);
    claim_want(&c, true, T);
    next(&c);
    claim_probed(&c, -1, NULL, T);
    CHECK(next(&c) == CLAIM_ACT_NONE && c.due_ms == T + CLAIM_BACKOFF_MIN_MS, "probe without an answer: backoff");
    T = c.due_ms;
    CHECK(next(&c) == CLAIM_ACT_PROBE, "probe again");
    claim_probed(&c, 200, VERSION_NEW, T);
    int want = CLAIM_BACKOFF_MIN_MS;
    for (int i = 0; i < 8; i++) {
        CHECK(next(&c) == CLAIM_ACT_OPEN, "open %d", i);
        if (claim_needs_key(&c)) claim_set_key(&c, RND);
        claim_opened(&c, 429, "", -1, T);
        CHECK(c.due_ms == T + want && next(&c) == CLAIM_ACT_NONE && claim_needs_key(&c), "429 #%d without Retry-After waits %d ms (%lld), new key",
              i, want, (long long)(c.due_ms - T));
        want = want * 2 > CLAIM_BACKOFF_MAX_MS ? CLAIM_BACKOFF_MAX_MS : want * 2;
        T = c.due_ms;
    }
    CHECK(next(&c) == CLAIM_ACT_OPEN && claim_needs_key(&c), "open after the backoff, with a new key");
    claim_set_key(&c, RND);
    claim_opened(&c, 201, REPLY, -1, T);
    CHECK(c.backoff_ms == 0, "a claim resets the backoff");
    claim_want(&c, false, T);
    claim_want(&c, true, T);
    next(&c);
    claim_probed(&c, 200, VERSION_NEW, T);
    static const int FAIL[] = {-1, 500, 502, 400, 413};
    for (size_t i = 0; i < sizeof FAIL / sizeof FAIL[0]; i++) {
        CHECK(next(&c) == CLAIM_ACT_OPEN, "open");
        if (claim_needs_key(&c)) claim_set_key(&c, RND);
        claim_opened(&c, FAIL[i], "", -1, T);
        CHECK(c.phase == CLAIM_WAIT && c.due_ms > T, "claim %d backs off", FAIL[i]);
        T = c.due_ms;
    }
    CHECK(next(&c) == CLAIM_ACT_OPEN, "open");
    claim_opened(&c, 201, "{\"claim_id\":\"c_1\",\"code\":\"12345\"}", -1, T);
    CHECK(c.phase == CLAIM_WAIT && c.due_ms > T, "a 201 with a bad code backs off");
    T = c.due_ms;
    CHECK(next(&c) == CLAIM_ACT_OPEN, "open");
    claim_set_key(&c, RND);
    claim_opened(&c, 409, "{\"error\":\"already_paired\"}", -1, T);
    CHECK(next(&c) == CLAIM_ACT_NONE && c.due_ms == T + CLAIM_LIVE_MS && claim_needs_key(&c), "409 already_paired: wait 5 min, new key");
    T = c.due_ms;
    CHECK(next(&c) == CLAIM_ACT_OPEN, "then open again");
    claim_set_key(&c, RND);
    claim_opened(&c, 409, "", 30, T);
    CHECK(c.due_ms == T + 30000 && claim_needs_key(&c), "409 with Retry-After 30");
    T = c.due_ms;
    next(&c);
    claim_set_key(&c, RND);
    claim_opened(&c, 429, "", 120, T);
    CHECK(c.due_ms == T + 120000 && claim_needs_key(&c), "429 with Retry-After 120");
    T = c.due_ms;
    next(&c);
    claim_set_key(&c, RND);
    claim_opened(&c, -1, NULL, -1, T);
    CHECK(c.phase == CLAIM_WAIT && !claim_needs_key(&c), "no answer keeps the key for the re-post");
    T = c.due_ms;
    CHECK(next(&c) == CLAIM_ACT_OPEN && !claim_needs_key(&c), "re-post with the same key");
    claim_opened(&c, 200, REPLY, -1, T);
    CHECK(c.phase == CLAIM_SHOW, "200 (the same claim again) shows it");
    claim_approved_t a;
    T += 5000;
    next(&c);
    char code[CLAIM_CODE_LEN + 1];
    CHECK(claim_polled(&c, 429, "", 45, T, &a) == CLAIM_POLL_FAILED && c.due_ms == T + 45000 && !claim_needs_key(&c),
          "poll 429 waits Retry-After and keeps the claim");
    T = c.due_ms;
    next(&c);
    static const int KEEP[] = {-1, 500, 429};
    for (size_t i = 0; i < sizeof KEEP / sizeof KEEP[0]; i++) {
        CHECK(claim_polled(&c, KEEP[i], "", -1, T, &a) == CLAIM_POLL_FAILED && c.phase == CLAIM_SHOW && claim_code(&c, T, code),
              "poll %d keeps the claim and the code", KEEP[i]);
        CHECK(next(&c) == CLAIM_ACT_NONE, "and waits");
        T = c.due_ms;
        CHECK(next(&c) == CLAIM_ACT_POLL, "then polls again");
    }
    CHECK(claim_polled(&c, 200, "{\"state\":\"approved\",\"token\":\"x\"}", -1, T, &a) == CLAIM_POLL_FAILED && c.phase == CLAIM_SHOW,
          "an unusable approval keeps polling");
    CHECK(claim_polled(&c, 202, "{\"state\":\"pending\",\"expires_in_s\":0}", -1, T, &a) == CLAIM_POLL_PENDING && claim_code(&c, T, code),
          "a pending poll with a bad expiry keeps the local one");
}

static void test_pending_bound(void)
{
    claim_t c;
    char code[CLAIM_CODE_LEN + 1];
    claim_approved_t a;
    T = 0;
    start(&c);
    int64_t opened_at = T;
    int polls = 0;
    while (next(&c) == CLAIM_ACT_NONE || c.phase == CLAIM_SHOW) {
        if (c.phase != CLAIM_SHOW) break;
        if (next(&c) == CLAIM_ACT_POLL) {
            claim_polled(&c, 202, "{\"state\":\"pending\",\"expires_in_s\":600}", -1, T, &a);
            polls++;
        }
        T += 1000;
        if (T > 2 * 3600 * 1000) break;
    }
    CHECK(T <= opened_at + 600000 + 1000 && c.ignored == 1 && !claim_code(&c, T, code),
          "a server that answers pending 600 every poll cannot stretch the claim past its first deadline (ended at %lld ms after %d polls)",
          (long long)(T - opened_at), polls);
}

static void test_poll_backoff_resets(void)
{
    claim_t c;
    claim_approved_t a;
    T = 0;
    start(&c);
    T += 5000;
    next(&c);
    claim_polled(&c, 429, "", -1, T, &a);
    CHECK(c.due_ms == T + CLAIM_BACKOFF_MIN_MS, "first poll 429: 30 s");
    T = c.due_ms;
    next(&c);
    claim_polled(&c, 202, PENDING, -1, T, &a);
    T = c.due_ms;
    next(&c);
    claim_polled(&c, 429, "", -1, T, &a);
    CHECK(c.due_ms == T + CLAIM_BACKOFF_MIN_MS, "a 429 after a good poll starts the backoff again (%lld ms)", (long long)(c.due_ms - T));
}

static void test_reset(void)
{
    claim_t c;
    char code[CLAIM_CODE_LEN + 1];
    T = 0;
    start(&c);
    CHECK(c.has_key && claim_code(&c, T, code), "showing");
    claim_want(&c, true, T + 1);
    CHECK(c.phase == CLAIM_SHOW, "want again keeps the claim");
    claim_want(&c, false, T);
    static const char ZERO[CLAIM_KEY_LEN + 1];
    CHECK(c.phase == CLAIM_IDLE && !c.has_key && memcmp(c.key, ZERO, sizeof ZERO) == 0 && c.id[0] == 0 && c.code[0] == 0 &&
              !claim_code(&c, T, code) && next(&c) == CLAIM_ACT_NONE,
          "not wanted (a USB set_ember, a token): reset, key and code wiped");
    claim_want(&c, true, T);
    CHECK(next(&c) == CLAIM_ACT_PROBE && claim_needs_key(&c), "wanted again: a fresh probe and a new key");
    claim_probed(&c, 200, VERSION_NEW, T);
    opened(&c);
    claim_reset(&c);
    CHECK(c.phase == CLAIM_IDLE && !c.has_key && !claim_code(&c, T, code), "claim_reset");
    claim_t z;
    claim_init(&z);
    claim_opened(&z, 201, REPLY, -1, T);
    claim_approved_t a;
    CHECK(z.phase == CLAIM_IDLE && claim_polled(&z, 200, APPROVED, -1, T, &a) == CLAIM_POLL_FAILED,
          "replies out of turn are ignored");
    claim_want(&z, true, T);
    next(&z);
    claim_probed(&z, 200, VERSION_NEW, T);
    CHECK(next(&z) == CLAIM_ACT_OPEN, "open");
    claim_opened(&z, 201, REPLY, -1, T);
    CHECK(z.phase == CLAIM_WAIT, "a claim reply without a key backs off");
}

static void test_token_wiped(void)
{
    cJSON_Hooks h = {.malloc_fn = hook_malloc, .free_fn = hook_free};
    cJSON_InitHooks(&h);
    claim_approved_t a;
    freed_with_token = 0;
    CHECK(claim_poll_parse(APPROVED, &a) == CLAIM_POLL_APPROVED, "approved");
    CHECK(freed_with_token == 0, "the parsed tree's token is wiped before it is freed (%d blocks still held it)", freed_with_token);
    claim_t c;
    T = 0;
    claim_init(&c);
    claim_want(&c, true, T);
    next(&c);
    claim_probed(&c, 200, VERSION_NEW, T);
    next(&c);
    claim_set_key(&c, RND);
    claim_opened(&c, 201, REPLY, -1, T);
    T += 5000;
    next(&c);
    freed_with_token = 0;
    CHECK(claim_polled(&c, 200, APPROVED, -1, T, &a) == CLAIM_POLL_APPROVED && freed_with_token == 0, "claim_polled wipes it too (%d)",
          freed_with_token);
    static const char *const ODD[] = {
        "{\"state\":\"weird\",\"token\":\"ekd_TEST-token\"}",
        "{\"token\":\"ekd_TEST-token\"}",
        "{\"state\":\"pending\",\"expires_in_s\":60,\"token\":\"ekd_TEST-token\"}",
        "{\"state\":\"approved\",\"device_id\":\"knob 1\",\"token\":\"ekd_TEST-token\"}",
        "{\"state\":\"approved\",\"x\":{\"y\":[\"ekd_TEST-token\"]}}",
        "[\"ekd_TEST-token\"]",
    };
    for (size_t i = 0; i < sizeof ODD / sizeof ODD[0]; i++) {
        freed_with_token = 0;
        claim_poll_parse(ODD[i], &a);
        CHECK(freed_with_token == 0, "a token in any state or place is wiped before the free: %s (%d)", ODD[i], freed_with_token);
    }
    cJSON_InitHooks(NULL);
}

int main(void)
{
    test_b64url();
    test_bodies();
    test_parse();
    test_retry_after();
    test_flow();
    test_expiry();
    test_input_before_three();
    test_old_server();
    test_backoff();
    test_pending_bound();
    test_poll_backoff_resets();
    test_reset();
    test_token_wiped();
    if (failures) {
        printf("claim: %d failure(s)\n", failures);
        return 1;
    }
    printf("claim: all tests passed\n");
    return 0;
}
