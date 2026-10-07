#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "http_conn.h"
#include "pomo.h"

typedef struct {
    pomo_clock_t clock;
    bool online;
    bool disabled;
    int action_error;     /* last action: 0 ok/none, HTTP status, or -1 network error */
    double action_at;     /* pomo_client_now() clock */
} pomo_snapshot_t;

/* Call once at boot, before the display. */
void pomo_client_init(void);

/* Ember task, every legacy round. */
void pomo_client_legacy(bool on);

/* Ember task only. s NULL: Ember's Pomodoro is off. server_now: X-Ember-Now (0 none); sent/received: pomo_client_now() times. */
void pomo_client_feed(const pomo_state_t *s, bool counting, long long ends_at, long long server_now, double sent,
                      double received);
/* Ember task only. */
void pomo_client_note_clock(long long server_now, double sent, double received);
/* server = local + offset, seconds, pomo_client_now() clock. False before the first X-Ember-Now. Ember task only. */
bool pomo_client_srv_offset(double *offset);
void pomo_client_feed_failed(void);

void pomo_client_log_state(void);
int pomo_client_view_poll_ms(int idle_ms);

/* Ember task, view mode. */
bool pomo_client_next_action(uint32_t wait_ms, pomo_input_t *in);
/* conn: the ember task's (view) or the pomo task's (legacy). buf/cap: scratch for the answer. */
void pomo_client_run_action(pomo_input_t in, http_conn_t *conn, char *buf, int cap);

/* Any task; never blocks. */
void pomo_client_action(pomo_input_t in);

/* Thread-safe. */
bool pomo_client_get(pomo_snapshot_t *out);

/* Seconds on the esp_timer clock. */
double pomo_client_now(void);

const char *pomo_client_note(const pomo_snapshot_t *snap, double now);
