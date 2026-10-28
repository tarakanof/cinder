#pragma once

#include <stdbool.h>

#include "day.h"
#include "http_conn.h"

#define DAY_ACTION_OFFLINE (-2)
#define DAY_ACT_TTL_S 10.0

typedef struct {
    bool fed;              /* a view was applied since boot */
    bool has;              /* the last view carried a day block */
    day_block_t b;
    double fed_at;         /* pomo_client_now() clock */
    bool have_srv, has_utc_offset;
    double srv_offset;     /* server Unix s = pomo_client_now() + srv_offset */
    int utc_offset;        /* s, the weather location's */
    bool has_day_offset;
    int day_offset_min;    /* Ember's day-clock zone, learned from a running day's block */
    unsigned gen;          /* applied views */
    int queued;
    bool sending;
    int status;            /* last action: 0 ok, HTTP status, -1 network error or expired, DAY_ACTION_OFFLINE dropped */
    double status_at;
    unsigned done_gen;     /* gen when the last action succeeded */
} day_snapshot_t;

/* Once, before the ember task starts. */
void day_client_init(void);
/* Ember task, every applied view: b NULL when the view has no day block. */
void day_client_feed(const day_block_t *b, bool has_utc_offset, int utc_offset);
/* LVGL task. Offline: dropped with the offline note. A STOP_AT replaces a queued STOP_AT. */
void day_client_act(const day_act_t *a);
/* Any task. */
bool day_client_pending(void);
/* Ember task, before a view request: sends the queued actions in order (idempotent on Ember's side). */
void day_client_run(http_conn_t *conn);
/* Any task; drops actions older than DAY_ACT_TTL_S. */
void day_client_get(day_snapshot_t *out);
/* now: pomo_client_now(). exact: from Ember's day-clock zone; else the weather zone, for display only. */
int day_client_now_min(const day_snapshot_t *s, double now, bool *exact);
bool day_client_busy(const day_snapshot_t *s, double now);
const char *day_client_note(const day_snapshot_t *s, double now);
