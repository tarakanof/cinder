#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "controls_client.h"
#include "ember_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "link_state.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define MAX_REQS 32

typedef struct {
    char url[96], body[CTL_BODY_MAX], key[CTL_KEY_MAX];
    bool idempotent, anon;
    int64_t at_ms;
} req_t;

static int64_t s_us = 1000000;
static uint32_t s_link;
static bool s_offline, s_unauthorized, s_no_device;
static int s_wakes, s_status = 202, s_randoms;
static const char *s_ans = "{\"status\":\"queued\",\"target\":\"M4\"}";
static const char *s_retry_after;
static req_t s_reqs[MAX_REQS];
static int s_n;
static int64_t s_next_turn_ms;
static int s_turn_every_ms, s_turns_left;
static http_conn_t s_conn;

int64_t esp_timer_get_time(void) { return s_us; }
void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...) { (void)level, (void)tag, (void)fmt; }
uint32_t esp_random(void)
{
    s_randoms++;
    return 0xabcd0000;
}
void ember_client_wake(void) { s_wakes++; }
ember_link_t ember_client_link(void) { return s_unauthorized ? EMBER_LINK_UNAUTHORIZED : EMBER_LINK_OK; }
uint32_t ember_client_link_word(void) { return s_link; }
bool ember_client_offline(void) { return s_offline; }
bool config_store_has_device(void) { return !s_no_device; }

void vTaskDelay(TickType_t ticks)
{
    s_us += ticks * 1000LL;
    if (s_turns_left && s_us / 1000 >= s_next_turn_ms) {
        s_turns_left--;
        s_next_turn_ms += s_turn_every_ms;
        controls_client_send(CTL_TURN, 2);
    }
}

int http_conn_req_opts(http_conn_t *c, const char *url, const http_req_opts_t *o, char *buf, int cap)
{
    CHECK(c == &s_conn, "the ember task's connection");
    CHECK(s_n < MAX_REQS, "too many requests");
    if (s_n >= MAX_REQS) return -1;
    req_t *r = &s_reqs[s_n++];
    snprintf(r->url, sizeof r->url, "%s", url);
    snprintf(r->body, sizeof r->body, "%s", o->post_body ? o->post_body : "");
    snprintf(r->key, sizeof r->key, "%s", o->idem_key ? o->idem_key : "");
    r->idempotent = o->idempotent;
    r->anon = o->anon;
    r->at_ms = s_us / 1000;
    s_us += 5000;
    if (s_retry_after && o->on_header) o->on_header("retry-after", s_retry_after, o->hdr_ctx);
    if (o->on_header) o->on_header("Content-Length", "2", o->hdr_ctx);
    if (s_status < 0) return -1;
    snprintf(buf, (size_t)cap, "%s", s_ans);
    return s_status;
}

static void reset(void)
{
    s_n = 0;
    s_status = 202;
    s_ans = "{\"status\":\"queued\",\"target\":\"M4\"}";
    s_retry_after = NULL;
    s_offline = s_unauthorized = s_no_device = false;
    s_us += 120 * 1000000LL;
    ctl_state_t st;
    controls_client_get(&st);
}

static void run(void) { controls_client_run(&s_conn, "http://ember"); }

static ctl_res_t result(void)
{
    ctl_state_t st;
    controls_client_get(&st);
    return st.res;
}

static void test_send(void)
{
    reset();
    int wakes = s_wakes;
    controls_client_send(CTL_PUSH, 0);
    CHECK(s_wakes == wakes + 1 && controls_client_pending(), "a gesture ends the long-poll");
    run();
    CHECK(s_n == 1 && !controls_client_pending(), "sent once: %d", s_n);
    CHECK(strcmp(s_reqs[0].url, "http://ember/v1/controls/action") == 0, "url %s", s_reqs[0].url);
    CHECK(strcmp(s_reqs[0].body, "{\"gesture\":\"push\"}") == 0, "body %s", s_reqs[0].body);
    CHECK(s_reqs[0].key[0] && s_reqs[0].idempotent, "a key, so http_conn may resend on a stale socket");
    CHECK(!s_reqs[0].anon, "the device token");
    ctl_state_t st;
    controls_client_get(&st);
    CHECK(st.res == CTL_RES_SENT && strcmp(st.target, "M4") == 0, "the reply names the Mac: %d %s", st.res, st.target);
}

static void test_keys(void)
{
    reset();
    int randoms = s_randoms;
    controls_client_send(CTL_PUSH, 0);
    controls_client_send(CTL_LONG_PUSH, 0);
    run();
    CHECK(s_n == 2 && strcmp(s_reqs[1].body, "{\"gesture\":\"long_push\"}") == 0, "two gestures in order");
    CHECK(strcmp(s_reqs[0].key, s_reqs[1].key) != 0, "a key per gesture: %s %s", s_reqs[0].key, s_reqs[1].key);
    CHECK(s_randoms <= randoms + 1, "the boot part is drawn once");
}

static void turn_for(int every_ms, int turns)
{
    s_turn_every_ms = every_ms;
    s_turns_left = turns;
    s_next_turn_ms = s_us / 1000 + every_ms;
}

static void test_turn_burst(void)
{
    reset();
    controls_client_send(CTL_TURN, 2);
    turn_for(250, 1);
    run();
    CHECK(s_n == 2 && strcmp(s_reqs[1].body, "{\"gesture\":\"turn\",\"steps\":2}") == 0, "the next step goes on the same run: %d", s_n);
    CHECK(s_reqs[1].at_ms - s_reqs[0].at_ms < 300, "no view between the steps");

    reset();
    int64_t t0 = s_us;
    controls_client_send(CTL_TURN, 1);
    run();
    CHECK(s_us - t0 >= 700000, "a turn lingers 0.7 s for the next step");

    reset();
    t0 = s_us;
    controls_client_send(CTL_PUSH, 0);
    run();
    CHECK(s_us - t0 < 20000, "a push does not linger");
}

static void test_burst_bounded(void)
{
    reset();
    int64_t t0 = s_us;
    controls_client_send(CTL_TURN, 1);
    turn_for(250, 1000);
    run();
    CHECK(s_us - t0 < 5500000, "a long turn lets the view and the checkin in: %lld ms", (long long)((s_us - t0) / 1000));
    CHECK(s_n >= 19, "and sends meanwhile: %d", s_n);
    s_turns_left = 0;
    run();
}

static void test_gate(void)
{
    reset();
    s_offline = true;
    controls_client_send(CTL_PUSH, 0);
    CHECK(!controls_client_pending() && result() == CTL_RES_OFFLINE, "offline: not queued");

    reset();
    link_state_t ls;
    link_state_init(&ls, 0);
    link_state_note(&ls, LINK_OK, 0);
    s_link = link_state_word(&ls);
    controls_client_send(CTL_PUSH, 0);
    for (int i = 0; i < LINK_OFFLINE_AFTER_FAILS; i++) link_state_note(&ls, LINK_FAIL, 1);
    link_state_note(&ls, LINK_OK, 2);
    s_link = link_state_word(&ls);
    run();
    CHECK(s_n == 0 && result() == CTL_RES_OFFLINE, "a press from before an offline spell is stale: %d sent", s_n);

    reset();
    s_link = link_state_word(&ls);
    controls_client_send(CTL_PUSH, 0);
    run();
    CHECK(s_n == 1, "a press after it goes");
    s_link = 0;

    reset();
    s_unauthorized = true;
    controls_client_send(CTL_PUSH, 0);
    run();
    CHECK(s_n == 0 && result() == CTL_RES_NOT_PAIRED, "unauthorized");

    reset();
    s_no_device = true;
    controls_client_send(CTL_PUSH, 0);
    run();
    CHECK(s_n == 0 && result() == CTL_RES_NOT_PAIRED, "no device");
}

static void test_ttl(void)
{
    reset();
    controls_client_send(CTL_PUSH, 0);
    s_us += (CTL_TTL_MS + 1) * 1000LL;
    run();
    CHECK(s_n == 0 && result() == CTL_RES_NO_CONN, "too old when its turn comes: dropped");
    reset();
    controls_client_send(CTL_PUSH, 0);
    s_us += (CTL_TTL_MS + 1) * 1000LL;
    CHECK(result() == CTL_RES_NO_CONN && !controls_client_pending(), "the frame reports it while the link is stuck");
}

static void test_status(void)
{
    static const struct {
        int status;
        ctl_res_t want;
    } C[] = {{200, CTL_RES_SENT}, {503, CTL_RES_NO_MAC}, {403, CTL_RES_PAGE_OFF}, {401, CTL_RES_NOT_PAIRED}, {500, CTL_RES_ERROR}, {-1, CTL_RES_NO_CONN}};
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        reset();
        s_status = C[i].status;
        s_ans = "{\"status\":\"duplicate\"}";
        controls_client_send(CTL_PUSH, 0);
        run();
        CHECK(s_n == 1 && result() == C[i].want, "HTTP %d: %d requests, result %d", C[i].status, s_n, result());
    }
}

static void test_retry_after(void)
{
    reset();
    s_status = 429;
    s_ans = "{}";
    s_retry_after = "2";
    controls_client_send(CTL_PUSH, 0);
    run();
    CHECK(s_n == 1 && result() == CTL_RES_TOO_FAST, "429, no retry");
    s_status = 202;
    controls_client_send(CTL_PUSH, 0);
    run();
    CHECK(s_n == 1 && result() == CTL_RES_TOO_FAST, "inside Retry-After nothing goes out");
    s_us += 2000000;
    controls_client_send(CTL_PUSH, 0);
    run();
    CHECK(s_n == 2 && result() == CTL_RES_SENT, "after it, sends again");

    reset();
    s_status = 429;
    s_ans = "{}";
    controls_client_send(CTL_PUSH, 0);
    run();
    s_status = 202;
    s_us += 1000000;
    controls_client_send(CTL_PUSH, 0);
    run();
    CHECK(s_n == 2, "no header: 1 s");
}

int main(void)
{
    test_send();
    test_keys();
    test_turn_burst();
    test_burst_bounded();
    test_gate();
    test_ttl();
    test_status();
    test_retry_after();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("controls_client: ok\n");
    return 0;
}
