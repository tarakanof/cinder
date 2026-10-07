/* One owner task per handle; no close from other tasks. A request ends with the whole body read. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_http_client.h"
#include "fail_streak.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_CONN_TIMEOUT_MS 5000
#define HTTP_CONN_ABORTED (-2)

typedef void (*http_header_cb)(const char *key, const char *value, void *ctx);

typedef struct {
    const char *name;
    esp_http_client_handle_t h;
    uint32_t gen;
    bool warm;
    int64_t head_at_us;
    http_header_cb on_header;
    void *hdr_ctx;
    fail_streak_t streak;
    /* Owner task, at the start of every request. */
    void (*on_request)(void);
} http_conn_t;

typedef struct {
    uint32_t requests;
    uint32_t failures;
    uint32_t retries;
    uint32_t connects;
    uint32_t not_modified;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
} http_conn_stats_t;

void http_conn_init(http_conn_t *c, const char *name);

/* Body NUL-terminated, cut at cap - 1; returns the HTTP status or -1 on a network error. */
int http_conn_req(http_conn_t *c, const char *url, const char *post_body, bool idempotent, char *buf, int cap,
                  http_header_cb on_header, void *hdr_ctx);

typedef struct {
    const char *post_body;
    bool idempotent;
    bool auth;
    const char *if_none_match;
    http_header_cb on_header;
    void *hdr_ctx;
    /* read_budget_ms > 0: long-poll; idle() runs on the caller task each slice, true gives the request up (HTTP_CONN_ABORTED). */
    int read_budget_ms;
    int wake_fd;
    bool (*idle)(void *ctx, int *next_slice_ms);
    void *idle_ctx;
    const char *idem_key;
    /* Length in bytes, counted past cap - 1. */
    int *body_len;
    /* PUT of stream_len bytes: read() returns the next chunk at off (> 0) or <= 0 to abort (HTTP_CONN_ABORTED); off restarts at 0 on a retry. */
    int stream_len;
    int (*read)(void *ctx, int off, const char **chunk);
    void *read_ctx;
    const char *content_type;
} http_req_opts_t;
int http_conn_req_opts(http_conn_t *c, const char *url, const http_req_opts_t *o, char *buf, int cap);

typedef struct {
    const char *range;
    const char *if_range;
    http_header_cb on_header;
    void *hdr_ctx;
    /* 2xx bodies only, piece by piece; false gives the request up (HTTP_CONN_ABORTED). */
    bool (*sink)(void *ctx, const uint8_t *p, int n);
    void *sink_ctx;
    uint8_t *piece;
    int piece_cap;
    int64_t *content_length;
    /* The status line, also when the body fails later (-1). */
    int *status;
} http_stream_opts_t;
/* GET with the bearer; a non-2xx body is read and dropped. */
int http_conn_stream(http_conn_t *c, const char *url, const http_stream_opts_t *o);

/* Owner task only. */
void http_conn_free(http_conn_t *c);

/* Safe from any task. */
void http_conn_link_down(void);

void http_conn_get_stats(http_conn_stats_t *out);

void http_conn_quiet_idf_logs(void);

#ifdef __cplusplus
}
#endif
