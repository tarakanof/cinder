#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "demo.h"
#include "shown.h"
#include "view_wait.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m) ((void)(m))
#define RESP_MAX 256
#define HTTP_CONN_ABORTED (-2)
#define VIEW_ETAG_MAX 16
#define CFG_URL_MAX 96

typedef int portMUX_TYPE;
typedef struct ember_host_info ember_host_info_t;
typedef struct http_conn http_conn_t;
typedef struct { int unused; } pomo_press_t;
typedef struct { char etag[VIEW_ETAG_MAX]; } view_etag_t;
typedef struct { int view; } knob_view_t;
typedef struct { knob_view_t view; } knob_view_state_t;
typedef struct { int poll_ms; } knob_settings_t;
typedef struct { int64_t next_ms; } dev_sched_t;
typedef struct { char etag[VIEW_ETAG_MAX]; char now[24]; char wait[8]; } resp_hdrs_t;
typedef enum { KNOB_STEP_APPLY, KNOB_STEP_RESTORE, KNOB_STEP_UNPARSED, KNOB_STEP_NONE } knob_act_t;
typedef struct { knob_act_t act; bool etag_clear, etag_set; int mood; } knob_step_t;
typedef void (*on_header_fn)(const char *, const char *, void *);
typedef struct {
    bool idempotent, auth;
    const char *if_none_match;
    on_header_fn on_header;
    void *hdr_ctx;
    int read_budget_ms;
    int wake_fd;
    bool (*idle)(void *, int *);
    bool *truncated;
} http_req_opts_t;

typedef struct {
    knob_settings_t ks;
    resp_hdrs_t hdrs;
    view_etag_t etag;
    knob_view_state_t vs;
    int wait_cap;
    int rearm_ms;
    bool waited;
    bool view_failing;
    bool unparsed;
    bool applied;
    char base[CFG_URL_MAX];
    char view_url[CFG_URL_MAX + 32];
    char wait_url[CFG_URL_MAX + 48 + DEMO_QUERY_MAX + SHOWN_QUERY_MAX];
    char demo_q[DEMO_QUERY_MAX];
    char shown_q[SHOWN_QUERY_MAX];
} poll_ctx_t;

static poll_ctx_t ctx;
static poll_ctx_t *P = &ctx;
#define s_ks (P->ks)
static const char *TAG = "t";
static http_conn_t *s_conn;
static int s_wake_fd = -1;
static bool s_plain_poll;
static dev_sched_t s_sched;
static demo_t s_demo;
static shown_t s_shown = SHOWN_INIT;
static portMUX_TYPE s_demo_mux, s_shown_mux;
static int64_t s_now = 100000;
#define atomic_load(p) (*(p))

static int req_status;
static int req_count;
static bool req_saw_sent;
static char req_url[1024];
static int req_due;

static void on_header(const char *k, const char *v, void *c) { (void)k; (void)v; (void)c; }
static bool longpoll_idle(void *c, int *n) { (void)c; (void)n; return false; }
static int64_t now_ms(void) { return s_now; }
static void fade_start(void) {}
static double pomo_client_now(void) { return 0; }
static bool pomo_client_next_action(int a, pomo_press_t *in) { (void)a; (void)in; return false; }
static void pomo_client_run_action(pomo_press_t in, http_conn_t *c, char *b, int n) { (void)in; (void)c; (void)b; (void)n; }
static void pomo_client_log_state(void) {}
static void day_client_run(http_conn_t *c) { (void)c; }
static int s_controls_runs, s_controls_runs_at_get;
static void controls_client_run(http_conn_t *c, const char *base)
{
    (void)c, (void)base;
    s_controls_runs++;
}
static bool s_conditional;
static bool knob_view_conditional(const knob_view_state_t *v) { (void)v; return s_conditional; }
static const char *view_etag_get(const view_etag_t *e) { return e->etag; }
static void view_etag_clear(view_etag_t *e) { e->etag[0] = 0; }
static void view_etag_set(view_etag_t *e, const char *s) { (void)e; (void)s; }
static void ota_client_note_view_ok(void) {}
static void view_parse_now(const char *s, long long *out) { (void)s; *out = 0; }
static knob_step_t knob_view_step(knob_view_state_t *v, int status, const char *buf)
{
    (void)v; (void)status; (void)buf;
    knob_step_t st = {.act = KNOB_STEP_NONE, .mood = -1};
    return st;
}
static void view_compat_publish(void) {}
static void demo_offer_view(void) {}
static void apply_view(ember_host_info_t *h, long long s, double a, double b) { (void)h; (void)s; (void)a; (void)b; }
static int scene_mood(int m) { return m; }
static void view_host(const knob_view_t *v, ember_host_info_t *h) { (void)v; (void)h; }

static int http_conn_req_opts(http_conn_t *c, const char *url, const http_req_opts_t *o, char *buf, int cap)
{
    (void)c; (void)o; (void)buf; (void)cap;
    req_count++;
    s_controls_runs_at_get = s_controls_runs;
    req_due = shown_due_ms(&s_shown, s_now);
    req_saw_sent = s_shown.sent;
    snprintf(req_url, sizeof req_url, "%s", url);
    return req_status;
}

#include VIEW_POLL_SRC

static void reset(void)
{
    memset(&ctx, 0, sizeof ctx);
    ctx.ks.poll_ms = 5000;
    ctx.wait_cap = 25;
    snprintf(ctx.view_url, sizeof ctx.view_url, "http://ember/view");
    s_shown = (shown_t)SHOWN_INIT;
    demo_init(&s_demo);
    s_plain_poll = false;
    req_count = 0;
    req_saw_sent = false;
    req_due = -2;
    req_url[0] = 0;
}

static void run(const char *label, bool plain, int status)
{
    reset();
    s_plain_poll = plain;
    s_conditional = true;
    s_sched.next_ms = s_now + 30000;
    req_status = status;
    shown_note(&s_shown, SHOWN_USAGE, -1, s_now - 1000);
    CHECK(shown_due_ms(&s_shown, s_now) == 0, "%s: the value is due before the poll", label);
    char buf[RESP_MAX];
    int mood;
    int got = view_poll(buf, &mood, NULL);
    CHECK(got == status, "%s: view_poll returns the request status (%d)", label, got);
    CHECK(req_count == 1, "%s: exactly one request (%d)", label, req_count);
    CHECK(strstr(req_url, "shown=usage") != NULL, "%s: the request carries the value (%s)", label, req_url);
    CHECK(plain ? strstr(req_url, "wait=") == NULL : strstr(req_url, "wait=") != NULL, "%s: wait is in the URL on a long-poll only (%s)", label, req_url);
    CHECK(req_saw_sent, "%s: the value is marked sent before the request goes out", label);
    CHECK(req_due == -1, "%s: nothing is due while the request is in flight (%d)", label, req_due);
    CHECK(s_shown.sent, "%s: the value is marked sent after view_poll returns", label);
    CHECK(shown_due_ms(&s_shown, s_now + 1000) == -1, "%s: not due again until it changes", label);
}

int main(void)
{
    (void)TAG;
    run("long-poll 200", false, 200);
    run("plain poll 200", true, 200);
    run("long-poll aborted", false, HTTP_CONN_ABORTED);
    run("long-poll timeout", false, 0);
    CHECK(s_controls_runs_at_get == s_controls_runs && s_controls_runs == 4, "a gesture goes before the view GET: %d", s_controls_runs);
    if (failures) {
        printf("%d view_poll check(s) failed\n", failures);
        return 1;
    }
    printf("view_poll: all checks passed\n");
    return 0;
}
