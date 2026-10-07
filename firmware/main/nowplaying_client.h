/* The finished picture set goes to the LVGL task, which owns and frees it. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "http_conn.h"
#include "np.h"
#include "np_ctl.h"

typedef struct {
    bool have;
    np_info_t info;
    np_anchor_t anchor; /* position on the pomo_client_now() clock */
} np_snapshot_t;

/* NULL = that picture does not exist or failed to decode. */
typedef struct {
    char version[NP_ARTV_MAX + 1];
    uint16_t *backdrop;   /* 466 x 466 RGB565, masked to the face disk */
    uint8_t *album;       /* 240 x 240 RGB565A8 (circle) */
    uint8_t *artist;      /* 64 x 64 RGB565A8 (circle) */
} np_art_set_t;

#define NP_CMD_TTL_MS 3000
/* LVGL task. A full queue drops the oldest; a command older than NP_CMD_TTL_MS is dropped. */
void np_client_control(const np_cmd_t *cmd, const char *source, const char *track_id);
/* Any task. */
bool np_client_visible(void);
/* 0 none/ok, HTTP status, or -1 (network); seq counts answers. */
typedef struct {
    int status;
    uint32_t seq;
} np_ctl_result_t;
void np_client_control_result(np_ctl_result_t *out);

/* Ember task. */
bool np_client_cmds_pending(void);
void np_client_run_controls(http_conn_t *conn, const char *base);

/* Once, before the ember task starts. */
void np_client_init(void);

/* Ember task. np NULL: the page is off in Ember. */
void np_client_feed(const np_info_t *np);
/* Ember task. */
bool np_client_pending(void);
/* Ember task. base: Ember's URL. */
void np_client_service(http_conn_t *conn, const char *base);

/* LVGL task. Hiding drops a set not yet taken. */
void np_client_set_visible(bool on);
/* LVGL task. */
void np_client_forget_art(void);
/* LVGL task. */
bool np_client_get(np_snapshot_t *out, uint32_t *gen);
/* LVGL task; the caller owns the set. */
bool np_client_take_art(np_art_set_t *out);
void np_art_set_free(np_art_set_t *s);
