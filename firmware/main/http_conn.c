#include "http_conn.h"

#include <stdatomic.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "config_store.h"
#include "diag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "http_retry.h"

static const char *TAG = "http";

static atomic_uint s_gen;
static atomic_uint s_requests, s_failures, s_retries, s_connects, s_not_modified, s_rx_bytes, s_tx_bytes;

void http_conn_init(http_conn_t *c, const char *name)
{
    memset(c, 0, sizeof *c);
    c->name = name;
    fs_init(&c->streak);
}

void http_conn_link_down(void) { atomic_fetch_add(&s_gen, 1); }

void http_conn_get_stats(http_conn_stats_t *out)
{
    out->requests = atomic_load(&s_requests);
    out->failures = atomic_load(&s_failures);
    out->retries = atomic_load(&s_retries);
    out->connects = atomic_load(&s_connects);
    out->not_modified = atomic_load(&s_not_modified);
    out->rx_bytes = atomic_load(&s_rx_bytes);
    out->tx_bytes = atomic_load(&s_tx_bytes);
}

/* Approximate bytes esp_http_client sends: request line, Host, User-Agent, Content-Length/Type, Authorization, If-None-Match, Idempotency-Key, blank line, body. */
static unsigned tx_estimate(const char *url, const http_req_opts_t *o, size_t bearer_len, int len)
{
    const char *host = strstr(url, "://");
    host = host ? host + 3 : url;
    const char *path = strchr(host, '/');
    size_t host_len = path ? (size_t)(path - host) : strlen(host);
    size_t n = (o->post_body || o->read ? 5 : 4) + (path ? strlen(path) : 1) + 11;
    n += 8 + host_len + 20 + 2;
    if (bearer_len) n += 17 + bearer_len;
    if (o->post_body || o->read) n += (o->read ? 40 : 32) + 20 + (size_t)len;
    if (o->if_none_match) n += 17 + strlen(o->if_none_match);
    if (o->idem_key) n += 19 + strlen(o->idem_key);
    return (unsigned)n;
}

void http_conn_quiet_idf_logs(void)
{
    esp_log_level_set("esp-tls", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("transport", ESP_LOG_NONE);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
}

static esp_err_t on_event(esp_http_client_event_t *e)
{
    http_conn_t *c = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_CONNECTED) {
        atomic_fetch_add(&s_connects, 1);
    } else if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value) {
        atomic_fetch_add(&s_rx_bytes, strlen(e->header_key) + strlen(e->header_value) + 4);
        if (c && c->on_header) c->on_header(e->header_key, e->header_value, c->hdr_ctx);
    }
    return ESP_OK;
}

static void drop_socket(http_conn_t *c)
{
    esp_http_client_close(c->h);
    c->warm = false;
}

static bool write_all(http_conn_t *c, const char *p, int n)
{
    while (n > 0) {
        int w = esp_http_client_write(c->h, p, n);
        if (w <= 0) return false;
        p += w;
        n -= w;
    }
    return true;
}

static int attempt(http_conn_t *c, const http_req_opts_t *o, const char *body, int len, char *buf, int cap,
                   http_fail_t *why)
{
    buf[0] = 0;
    esp_err_t err = esp_http_client_open(c->h, len);
    if (err != ESP_OK) {
        *why = HTTP_FAIL_OPEN;
        return -1;
    }
    if (o->read) {
        for (int off = 0; off < len;) {
            const char *chunk = NULL;
            int n = o->read(o->read_ctx, off, &chunk);
            if (n <= 0 || n > len - off) return HTTP_CONN_ABORTED;
            if (!write_all(c, chunk, n)) {
                *why = HTTP_FAIL_SEND;
                return -1;
            }
            off += n;
        }
    } else if (len && !write_all(c, body, len)) {
        *why = HTTP_FAIL_SEND;
        return -1;
    }
    int64_t hl;
    if (o->read_budget_ms > 0) {
        int sock = esp_http_client_get_socket(c->h);
        int64_t t0 = esp_timer_get_time();
        int slice = 1000;
        for (;;) {
            int64_t left = o->read_budget_ms - (esp_timer_get_time() - t0) / 1000;
            if (left <= 0 || sock < 0) {
                *why = HTTP_FAIL_TIMEOUT;
                return -1;
            }
            int ms = slice < left ? slice : (int)left;
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(sock, &rd);
            int maxfd = sock;
            if (o->wake_fd >= 0) {
                FD_SET(o->wake_fd, &rd);
                if (o->wake_fd > maxfd) maxfd = o->wake_fd;
            }
            struct timeval tv = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
            int n = select(maxfd + 1, &rd, NULL, NULL, &tv);
            if (n < 0) {
                *why = HTTP_FAIL_CLOSED;
                return -1;
            }
            if (o->wake_fd >= 0 && FD_ISSET(o->wake_fd, &rd)) {
                uint64_t v;
                (void)read(o->wake_fd, &v, sizeof v);
            }
            if (FD_ISSET(sock, &rd)) break;
            if (o->idle && o->idle(o->idle_ctx, &slice)) return HTTP_CONN_ABORTED;
        }
    }
    hl = esp_http_client_fetch_headers(c->h);
    if (hl < 0) {
        *why = hl == -ESP_ERR_HTTP_EAGAIN ? HTTP_FAIL_TIMEOUT : HTTP_FAIL_CLOSED;
        return -1;
    }
    c->head_at_us = esp_timer_get_time();
    int status = esp_http_client_get_status_code(c->h);
    if (status <= 0) {
        *why = HTTP_FAIL_NO_STATUS;
        return -1;
    }
    atomic_fetch_add(&s_rx_bytes, 17 + 2);
    int n = 0, total = 0, r;
    char spill[64];
    long drain_max = 2L * cap + 4096;
    do {
        bool room = n < cap - 1;
        r = esp_http_client_read(c->h, room ? buf + n : spill, room ? cap - 1 - n : (int)sizeof spill);
        if (r > 0 && room) n += r;
        if (r > 0) {
            total += r;
            atomic_fetch_add(&s_rx_bytes, (unsigned)r);
        }
        if (total > drain_max) {
            *why = HTTP_FAIL_BODY;
            return -1;
        }
    } while (r > 0);
    buf[n] = 0;
    if (o->body_len) *o->body_len = total;
    if (!esp_http_client_is_complete_data_received(c->h)) {
        *why = HTTP_FAIL_BODY;
        return -1;
    }
    return status;
}

static void log_streak(http_conn_t *c, fs_log_t what, const char *path, http_fail_t fail)
{
    const char *why = http_fail_name(fail);
    switch (what) {
    case FS_STARTED:
        ESP_LOGW(TAG, "%s: %s failed (%s); quiet until it recovers", c->name, path, why);
        break;
    case FS_STILL:
        ESP_LOGW(TAG, "%s: still failing (%s): %d in a row over %lld s", c->name, why, fs_fails(&c->streak),
                 (long long)((esp_timer_get_time() / 1000 - c->streak.first_ms) / 1000));
        break;
    case FS_RECOVERED:
        ESP_LOGI(TAG, "%s: recovered after %d failed request(s) over %.1f s", c->name, c->streak.ended_fails,
                 c->streak.ended_ms / 1000.0);
        break;
    default:
        break;
    }
}

static bool conn_prepare(http_conn_t *c, const char *url)
{
    uint32_t gen = atomic_load(&s_gen);
    if (!c->h) {
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = HTTP_CONN_TIMEOUT_MS,
            .user_agent = "cinder",
            .event_handler = on_event,
            .user_data = c,
        };
        c->h = esp_http_client_init(&cfg);
        if (!c->h) return false;
        c->warm = false;
    } else {
        if (c->gen != gen) drop_socket(c);
        if (esp_http_client_set_url(c->h, url) != ESP_OK) return false;
    }
    c->gen = gen;
    return true;
}

static void finish_note(http_conn_t *c, const char *url, int status, bool ok, int64_t t_start, http_fail_t why)
{
    if (status < 0 && status != HTTP_CONN_ABORTED) atomic_fetch_add(&s_failures, 1);
    int64_t ms = (esp_timer_get_time() - t_start) / 1000;
    diag_note_request(ok, (uint32_t)(ms > 0 ? ms : 0));
    const char *path = strstr(url, "://");
    path = path ? strchr(path + 3, '/') : NULL;
    if (status != HTTP_CONN_ABORTED)
        log_streak(c, fs_note(&c->streak, status > 0, esp_timer_get_time() / 1000), path ? path : url, why);
}

static size_t set_bearer(http_conn_t *c)
{
    char bearer[CFG_TOKEN_MAX + 8];
    strcpy(bearer, "Bearer ");
    config_store_token(bearer + 7, sizeof bearer - 7);
    size_t n = 0;
    if (bearer[7]) {
        n = strlen(bearer);
        esp_http_client_set_header(c->h, "Authorization", bearer);
    }
    memset(bearer, 0, sizeof bearer);
    return n;
}

static int stream_attempt(http_conn_t *c, const http_stream_opts_t *o, http_fail_t *why, bool *head)
{
    *head = false;
    if (esp_http_client_open(c->h, 0) != ESP_OK) {
        *why = HTTP_FAIL_OPEN;
        return -1;
    }
    int64_t hl = esp_http_client_fetch_headers(c->h);
    if (hl < 0) {
        *why = hl == -ESP_ERR_HTTP_EAGAIN ? HTTP_FAIL_TIMEOUT : HTTP_FAIL_CLOSED;
        return -1;
    }
    c->head_at_us = esp_timer_get_time();
    int status = esp_http_client_get_status_code(c->h);
    if (status <= 0) {
        *why = HTTP_FAIL_NO_STATUS;
        return -1;
    }
    *head = true;
    if (o->status) *o->status = status;
    if (o->content_length) *o->content_length = esp_http_client_get_content_length(c->h);
    atomic_fetch_add(&s_rx_bytes, 17 + 2);
    bool sink = status >= 200 && status < 300 && o->sink;
    int total = 0;
    for (;;) {
        int r = esp_http_client_read(c->h, (char *)o->piece, o->piece_cap);
        if (r < 0) {
            *why = r == -ESP_ERR_HTTP_EAGAIN ? HTTP_FAIL_TIMEOUT : HTTP_FAIL_BODY;
            return -1;
        }
        if (r == 0) break;
        atomic_fetch_add(&s_rx_bytes, (unsigned)r);
        total += r;
        if (sink && !o->sink(o->sink_ctx, o->piece, r)) return HTTP_CONN_ABORTED;
        if (!sink && total > 16 * 1024) {
            *why = HTTP_FAIL_BODY;
            return -1;
        }
    }
    if (!esp_http_client_is_complete_data_received(c->h)) {
        *why = HTTP_FAIL_BODY;
        return -1;
    }
    return status;
}

int http_conn_stream(http_conn_t *c, const char *url, const http_stream_opts_t *o)
{
    if (o->status) *o->status = 0;
    if (c->on_request) c->on_request();
    atomic_fetch_add(&s_requests, 1);
    int64_t t_start = esp_timer_get_time();
    if (!conn_prepare(c, url)) {
        atomic_fetch_add(&s_failures, 1);
        return -1;
    }
    c->on_header = o->on_header;
    c->hdr_ctx = o->hdr_ctx;
    esp_http_client_set_method(c->h, HTTP_METHOD_GET);
    size_t bearer_len = set_bearer(c);
    if (o->range) esp_http_client_set_header(c->h, "Range", o->range);
    if (o->if_range) esp_http_client_set_header(c->h, "If-Range", o->if_range);
    http_fail_t why = HTTP_FAIL_OPEN;
    bool reused = c->warm, head = false;
    int status = stream_attempt(c, o, &why, &head);
    if (status == -1 && !head && reused) {
        drop_socket(c);
        atomic_fetch_add(&s_retries, 1);
        status = stream_attempt(c, o, &why, &head);
    }
    if (status < 0) {
        drop_socket(c);
    } else {
        c->warm = esp_http_client_is_persistent_connection(c->h);
        if (!c->warm) esp_http_client_close(c->h);
    }
    http_req_opts_t est = {.auth = true};
    atomic_fetch_add(&s_tx_bytes, tx_estimate(url, &est, bearer_len, 0) + (o->range ? 9 + strlen(o->range) : 0) +
                                      (o->if_range ? 12 + strlen(o->if_range) : 0));
    if (bearer_len) esp_http_client_delete_header(c->h, "Authorization");
    if (o->range) esp_http_client_delete_header(c->h, "Range");
    if (o->if_range) esp_http_client_delete_header(c->h, "If-Range");
    c->on_header = NULL;
    c->hdr_ctx = NULL;
    finish_note(c, url, status, status >= 200 && status < 300, t_start, why);
    return status;
}

int http_conn_req(http_conn_t *c, const char *url, const char *post_body, bool idempotent, char *buf, int cap,
                  http_header_cb on_header, void *hdr_ctx)
{
    const http_req_opts_t o = {.post_body = post_body, .idempotent = idempotent, .on_header = on_header, .hdr_ctx = hdr_ctx};
    return http_conn_req_opts(c, url, &o, buf, cap);
}

int http_conn_req_opts(http_conn_t *c, const char *url, const http_req_opts_t *o, char *buf, int cap)
{
    const char *post_body = o->post_body;
    bool idempotent = o->idempotent;
    bool has_body = post_body || o->read;
    bool auth = has_body || o->auth;
    if (c->on_request) c->on_request();
    atomic_fetch_add(&s_requests, 1);
    int64_t t_start = esp_timer_get_time();
    if (!conn_prepare(c, url)) {
        atomic_fetch_add(&s_failures, 1);
        return -1;
    }
    c->on_header = o->on_header;
    c->hdr_ctx = o->hdr_ctx;

    int len = o->read ? o->stream_len : post_body ? (int)strlen(post_body) : 0;
    esp_http_client_set_method(c->h, o->read ? HTTP_METHOD_PUT : post_body ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    size_t bearer_len = auth ? set_bearer(c) : 0;
    if (has_body)
        esp_http_client_set_header(c->h, "Content-Type", o->content_type ? o->content_type : "application/json");
    if (o->if_none_match) esp_http_client_set_header(c->h, "If-None-Match", o->if_none_match);
    if (o->idem_key) esp_http_client_set_header(c->h, "Idempotency-Key", o->idem_key);

    http_fail_t why = HTTP_FAIL_OPEN;
    bool reused = c->warm;
    int status = attempt(c, o, post_body, len, buf, cap, &why);
    if (status < 0 && status != HTTP_CONN_ABORTED) {
        drop_socket(c);
        if (http_should_retry_wait(reused, idempotent, o->read_budget_ms > 0, why)) {
            atomic_fetch_add(&s_retries, 1);
            status = attempt(c, o, post_body, len, buf, cap, &why);
            if (status < 0 && status != HTTP_CONN_ABORTED) drop_socket(c);
        }
    }
    if (status == HTTP_CONN_ABORTED) {
        drop_socket(c);
        atomic_fetch_add(&s_tx_bytes, tx_estimate(url, o, bearer_len, len));
        if (bearer_len) esp_http_client_delete_header(c->h, "Authorization");
        if (has_body) esp_http_client_delete_header(c->h, "Content-Type");
        if (o->if_none_match) esp_http_client_delete_header(c->h, "If-None-Match");
        if (o->idem_key) esp_http_client_delete_header(c->h, "Idempotency-Key");
        c->on_header = NULL;
        c->hdr_ctx = NULL;
        return HTTP_CONN_ABORTED;
    }
    if (status > 0) {
        atomic_fetch_add(&s_tx_bytes, tx_estimate(url, o, bearer_len, len));
        c->warm = esp_http_client_is_persistent_connection(c->h);
        if (!c->warm) esp_http_client_close(c->h);
    }
    if (bearer_len) esp_http_client_delete_header(c->h, "Authorization");
    if (has_body) esp_http_client_delete_header(c->h, "Content-Type");
    if (o->if_none_match) esp_http_client_delete_header(c->h, "If-None-Match");
    if (o->idem_key) esp_http_client_delete_header(c->h, "Idempotency-Key");
    if (status == 304) atomic_fetch_add(&s_not_modified, 1);
    c->on_header = NULL;
    c->hdr_ctx = NULL;

    if (status < 0) atomic_fetch_add(&s_failures, 1);
    {
        bool ok = (status >= 200 && status < 300) || status == 304;
        int64_t ms = (esp_timer_get_time() - (o->read_budget_ms > 0 && ok ? c->head_at_us : t_start)) / 1000;
        diag_note_request(ok, (uint32_t)(ms > 0 ? ms : 0));
    }
    const char *path = strstr(url, "://");
    path = path ? strchr(path + 3, '/') : NULL;
    log_streak(c, fs_note(&c->streak, status > 0, esp_timer_get_time() / 1000), path ? path : url, why);
    return status;
}

void http_conn_free(http_conn_t *c)
{
    if (!c->h) return;
    esp_http_client_cleanup(c->h);
    c->h = NULL;
    c->warm = false;
}
