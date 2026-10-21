#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "claim.h"
#include "claim_client.h"
#include "config_store.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define CAP 1024
#define TOKEN "ekd_TEST-token-not-real-00000000000000000000000"

static const char *VERSION = "{\"binary\":\"ember\",\"features\":[\"firmware_delete_keep\",\"lan_pair\"]}";
static const char *REPLY = "{\"claim_id\":\"c_0123abcd\",\"code\":\"482913\",\"expires_in_s\":600,\"poll_s\":5}";
static const char *PENDING = "{\"state\":\"pending\",\"expires_in_s\":590}";
static const char *APPROVED = "{\"state\":\"approved\",\"device_id\":\"knob-10c0de\",\"name\":\"Knob 10C0DE\",\"token\":\"" TOKEN "\"}";

typedef enum { R_BODY, R_PARTIAL, R_OVERSIZED } kind_t;

typedef struct {
    int status;
    const char *body;
    kind_t kind;
    bool cancel;
} resp_t;

static struct {
    int64_t now_us;
    uint32_t gen;
    bool has_url, has_device;
    resp_t version, open, poll;
    int requests, polls, anon;
    char last_body[CLAIM_BODY_MAX];
    char logs[16384];
    size_t log_len;
    int pairs;
    esp_err_t pair_err;
    char pair_token[CFG_TOKEN_MAX + 1], pair_id[CFG_DEV_ID_MAX + 1], pair_url[CFG_URL_MAX + 1];
    uint32_t pair_gen;
} F;

static char BUF[CAP];
static http_conn_t CONN;

int64_t esp_timer_get_time(void) { return F.now_us; }

void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)level;
    (void)tag;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(F.logs + F.log_len, sizeof F.logs - F.log_len, fmt, ap);
    va_end(ap);
    if (n > 0) F.log_len += (size_t)n < sizeof F.logs - F.log_len ? (size_t)n : sizeof F.logs - F.log_len - 1;
    if (F.log_len < sizeof F.logs - 1) F.logs[F.log_len++] = '\n';
}

const char *esp_err_to_name(esp_err_t err) { return err == ESP_ERR_INVALID_STATE ? "ESP_ERR_INVALID_STATE" : "ESP_FAIL"; }

void esp_fill_random(void *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) ((uint8_t *)buf)[i] = (uint8_t)(0xA5 ^ i);
}

const esp_app_desc_t *esp_app_get_description(void)
{
    static const esp_app_desc_t d = {.version = "0.9.104"};
    return &d;
}

uint32_t config_store_token_gen(void) { return F.gen; }
bool config_store_has_ember_url(void) { return F.has_url; }
bool config_store_has_device(void) { return F.has_device; }
void config_store_hw_id(char out[13]) { strcpy(out, "02005e10c0de"); }

esp_err_t config_store_pair(uint32_t token_gen, const char *url, const char *dev_id, const char *token, const char *name)
{
    (void)name;
    F.pairs++;
    F.pair_gen = token_gen;
    snprintf(F.pair_token, sizeof F.pair_token, "%s", token);
    snprintf(F.pair_id, sizeof F.pair_id, "%s", dev_id);
    snprintf(F.pair_url, sizeof F.pair_url, "%s", url);
    if (F.pair_err == ESP_OK) {
        F.gen++;
        F.has_device = true;
    }
    return F.pair_err;
}

int http_conn_req_opts(http_conn_t *c, const char *url, const http_req_opts_t *o, char *buf, int cap)
{
    (void)c;
    F.requests++;
    F.anon += o->anon;
    snprintf(F.last_body, sizeof F.last_body, "%s", o->post_body ? o->post_body : "");
    size_t n = strlen(url);
    const resp_t *r = n >= 8 && strcmp(url + n - 8, "/version") == 0         ? &F.version
                      : n >= 5 && strcmp(url + n - 5, "/poll") == 0          ? &F.poll
                      : n >= 6 && strcmp(url + n - 6, "/claim") == 0         ? &F.open
                                                                             : NULL;
    if (!r) return 404;
    if (r == &F.poll) F.polls++;
    memset(buf, 'Q', (size_t)cap);
    if (r->body) memcpy(buf, r->body, strlen(r->body) + 1);
    if (r->cancel) claim_client_cancel();
    if (r->kind == R_PARTIAL) return -1;
    if (r->kind == R_OVERSIZED) buf[0] = 0;
    return r->status;
}

static bool turn(void) { return claim_client_service(&CONN, "http://ember.test:3627", true, false, BUF, CAP); }

static bool buf_zero(void)
{
    for (int i = 0; i < CAP; i++)
        if (BUF[i]) return false;
    return true;
}

static void fresh(void)
{
    F.gen++;
    F.has_url = true;
    F.has_device = false;
    F.version = (resp_t){200, VERSION, R_BODY, false};
    F.open = (resp_t){201, REPLY, R_BODY, false};
    F.poll = (resp_t){202, PENDING, R_BODY, false};
    F.pair_err = ESP_OK;
    turn();
    turn();
    CHECK(claim_client_code() == 482913, "probe then claim: the code is up (%d)", claim_client_code());
    F.now_us += 5000000;
}

static void test_flow(void)
{
    F.requests = F.anon = 0;
    fresh();
    char key[CLAIM_KEY_LEN + 1];
    uint8_t rnd[CLAIM_KEY_BYTES];
    esp_fill_random(rnd, sizeof rnd);
    claim_b64url(rnd, sizeof rnd, key, sizeof key);
    char want[CLAIM_BODY_MAX];
    claim_open_body("02005e10c0de", "0.9.104", key, want, sizeof want);
    CHECK(strcmp(F.last_body, want) == 0, "the claim body: %s", F.last_body);
    CHECK(!turn() && F.polls == 1 && buf_zero(), "a pending poll: the whole buffer wiped");
    F.now_us += 5000000;
    F.poll = (resp_t){200, APPROVED, R_BODY, false};
    int pairs = F.pairs;
    CHECK(turn(), "approved: stored");
    CHECK(F.pairs == pairs + 1 && strcmp(F.pair_token, TOKEN) == 0 && strcmp(F.pair_id, "knob-10c0de") == 0 &&
              strcmp(F.pair_url, "http://ember.test:3627") == 0 && F.pair_gen == F.gen - 1,
          "the pair gets the token, device id, the knob's own URL and the generation the claim started at");
    CHECK(buf_zero() && claim_client_code() == -1, "the reply wiped, no code once paired");
    CHECK(F.anon == F.requests && F.requests == 4, "every claim request goes without a token (%d of %d)", F.anon, F.requests);
}

static void test_wipe_on_failure(void)
{
    static const struct {
        kind_t kind;
        int status;
        const char *what;
    } C[] = {
        {R_PARTIAL, 200, "a reply cut off mid-body (-1)"},
        {R_OVERSIZED, 200, "an oversized reply (buf[0] = 0, the rest still there)"},
        {R_BODY, 500, "a 500 that echoes the token"},
        {R_BODY, 404, "a 404"},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        fresh();
        F.poll = (resp_t){C[i].status, APPROVED, C[i].kind, false};
        int pairs = F.pairs;
        turn();
        CHECK(buf_zero(), "%s: the whole poll buffer is wiped", C[i].what);
        CHECK(F.pairs == pairs || C[i].kind == R_BODY, "%s: no pairing", C[i].what);
    }
}

static void test_cancel(void)
{
    fresh();
    F.poll = (resp_t){202, PENDING, R_BODY, true};
    turn();
    CHECK(claim_client_code() == -1, "a USB cancel during a poll leaves no code on screen (%d)", claim_client_code());
    F.poll = (resp_t){202, PENDING, R_BODY, false};
    int polls = F.polls;
    F.now_us += 5000000;
    turn();
    CHECK(F.polls == polls && claim_client_code() == -1, "the next turn drops the claim: no poll, no code");
}

static void test_pair_refused(void)
{
    fresh();
    F.poll = (resp_t){200, APPROVED, R_BODY, false};
    F.pair_err = ESP_ERR_INVALID_STATE;
    CHECK(!turn() && claim_client_code() == -1, "a pair refused (USB won) drops the claim and its code");
    fresh();
    F.poll = (resp_t){200, APPROVED, R_BODY, false};
    F.pair_err = ESP_FAIL;
    CHECK(!turn() && claim_client_code() == 482913, "a failed store keeps the claim");
    F.now_us += 5000000;
    F.pair_err = ESP_OK;
    CHECK(turn(), "and the next poll stores it");
}

static void test_logs(void)
{
    char key[CLAIM_KEY_LEN + 1];
    uint8_t rnd[CLAIM_KEY_BYTES];
    esp_fill_random(rnd, sizeof rnd);
    claim_b64url(rnd, sizeof rnd, key, sizeof key);
    F.logs[F.log_len] = 0;
    CHECK(F.log_len > 0, "the claim logged something");
    CHECK(!strstr(F.logs, key) && !strstr(F.logs, "ekd_") && !strstr(F.logs, "482913") && !strstr(F.logs, "482 913"),
          "no key, token or code in the logs:\n%s", F.logs);
}

int main(void)
{
    test_flow();
    test_wipe_on_failure();
    test_cancel();
    test_pair_refused();
    test_logs();
    if (failures) {
        printf("claim client: %d failure(s)\n", failures);
        return 1;
    }
    printf("claim client: all tests passed\n");
    return 0;
}
