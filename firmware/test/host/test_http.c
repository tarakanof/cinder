#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_store.h"
#include "diag.h"
#include "ember_legacy.h"
#include "esp_http_client.h"
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
    int opens;
    int closes;
    int reads_after_cut;
    int requests_ok;
    int requests_failed;
    int64_t now_us;
} F;

static void server(int status, const char *body, int len)
{
    F.status = status;
    F.body = body;
    F.body_len = len;
    F.sent = 0;
    F.cut_short = false;
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
    if (cap) out[0] = 0;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    memset(&F.client, 0, sizeof F.client);
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
    (void)key;
    (void)value;
    return ESP_OK;
}

esp_err_t esp_http_client_delete_header(esp_http_client_handle_t client, const char *key)
{
    (void)client;
    (void)key;
    return ESP_OK;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len)
{
    (void)write_len;
    F.sent = 0;
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
    return len;
}

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client)
{
    (void)client;
    return F.body_len;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    (void)client;
    return F.status;
}

int64_t esp_http_client_get_content_length(esp_http_client_handle_t client)
{
    (void)client;
    return F.body_len;
}

int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int len)
{
    (void)client;
    int end = F.cut_short ? F.body_len / 2 : F.body_len;
    int n = end - F.sent;
    if (n > len) n = len;
    if (n > F.chunk) n = F.chunk;
    if (n <= 0) return 0;
    memcpy(buffer, F.body + F.sent, (size_t)n);
    F.sent += n;
    return n;
}

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client)
{
    (void)client;
    return F.sent == F.body_len;
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

typedef struct {
    int status;
    int body_len;
    link_outcome_t outcome;
} poll_t;

static char s_buf[KNOB_VIEW_BUF];

static poll_t poll_state(http_conn_t *c, link_level_t level)
{
    static ember_host_session_t sess[32];
    ember_host_info_t host = {0};
    poll_t p = {0};
    const http_req_opts_t o = {.idempotent = true, .body_len = &p.body_len};
    p.status = http_conn_req_opts(c, "http://ember.test/state", &o, s_buf, KNOB_VIEW_BUF);
    bool parsed = true;
    if (p.status == 200) parsed = ember_legacy_parse(s_buf, sess, 32, &host) >= 0;
    p.outcome = link_counts_body(level, true, false, p.status, parsed, p.body_len, KNOB_VIEW_BUF);
    return p;
}

static void test_fits(http_conn_t *c)
{
    int len;
    char *body = state_body(2000, &len);
    server(200, body, len);
    int opens = F.opens, closes = F.closes;
    poll_t p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == 200 && p.body_len == len, "small /state: %d, %d B", p.status, p.body_len);
    CHECK(strcmp(s_buf, body) == 0, "small /state read whole");
    CHECK(p.outcome == LINK_OK, "small /state parses: %d", p.outcome);
    CHECK(F.closes == closes && F.opens <= opens + 1, "small /state keeps the socket");
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
    CHECK((int)strlen(s_buf) == KNOB_VIEW_BUF - 1 && memcmp(s_buf, body, KNOB_VIEW_BUF - 1) == 0,
          "drained /state cut at cap - 1");
    CHECK(F.sent == len, "drained /state read to the end");
    CHECK(p.outcome == LINK_SKIP, "drained /state is a skip: %d", p.outcome);
    CHECK(F.closes == closes, "drained /state keeps the socket");
    free(body);
}

static void test_past_drain_limit(http_conn_t *c)
{
    long drain_max = 2L * KNOB_VIEW_BUF + 4096;
    int len;
    char *body = state_body((int)drain_max + 8192, &len);
    server(200, body, len);
    http_conn_stats_t before, after;
    http_conn_get_stats(&before);
    int failed = F.requests_failed, closes = F.closes;
    poll_t p = poll_state(c, LINK_ONLINE);
    http_conn_get_stats(&after);
    CHECK(p.status == 200, "past the drain limit the status still comes back: %d", p.status);
    CHECK(p.body_len > KNOB_VIEW_BUF - 1 && p.body_len < len, "body_len past cap, short of the whole: %d", p.body_len);
    CHECK(F.sent < len, "stops reading past the limit: %d of %d", F.sent, len);
    CHECK((int)strlen(s_buf) == KNOB_VIEW_BUF - 1 && memcmp(s_buf, body, KNOB_VIEW_BUF - 1) == 0, "cut at cap - 1");
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
    F.cut_short = false;
    free(body);
}

static void test_oversized_error(http_conn_t *c)
{
    long drain_max = 2L * KNOB_VIEW_BUF + 4096;
    int len = (int)drain_max + 8192;
    char *body = malloc((size_t)len);
    memset(body, 'x', (size_t)len);
    server(503, body, len);
    poll_t p = poll_state(c, LINK_ONLINE);
    CHECK(p.status == 503, "oversized 503 keeps its status: %d", p.status);
    CHECK(p.outcome == LINK_FAIL, "a 5xx still counts: %d", p.outcome);
    free(body);
}

static void test_counts_body(void)
{
    int n = 3 * KNOB_VIEW_BUF;
    CHECK(link_counts_body(LINK_ONLINE, true, false, 200, false, KNOB_VIEW_BUF - 1, KNOB_VIEW_BUF) == LINK_FAIL,
          "a body that fit and does not parse counts");
    CHECK(link_counts_body(LINK_ONLINE, true, false, 200, false, KNOB_VIEW_BUF, KNOB_VIEW_BUF) == LINK_SKIP,
          "one byte over the buffer is a skip");
    CHECK(link_counts_body(LINK_ONLINE, true, false, 200, true, n, KNOB_VIEW_BUF) == LINK_OK, "parsed stays ok");
    CHECK(link_counts_body(LINK_ONLINE, true, false, 500, false, n, KNOB_VIEW_BUF) == LINK_FAIL, "5xx still counts");
    CHECK(link_counts_body(LINK_ONLINE, true, false, 302, false, n, KNOB_VIEW_BUF) == LINK_FAIL, "3xx still counts");
    CHECK(link_counts_body(LINK_ONLINE, false, false, 200, false, n, KNOB_VIEW_BUF) == LINK_FAIL, "no Wi-Fi counts");
    CHECK(link_counts_body(LINK_ONLINE, true, false, 401, false, n, KNOB_VIEW_BUF) == LINK_SKIP, "401 still skipped");
    CHECK(link_counts_body(LINK_ONLINE, true, false, -1, false, 0, KNOB_VIEW_BUF) == LINK_FAIL, "no answer counts");
}

int main(void)
{
    test_counts_body();
    http_conn_t c;
    for (int chunk = 512; chunk <= 4096; chunk *= 8) {
        F.chunk = chunk;
        http_conn_init(&c, "test");
        test_fits(&c);
        test_drained(&c);
        test_past_drain_limit(&c);
        test_cut_by_server(&c);
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
