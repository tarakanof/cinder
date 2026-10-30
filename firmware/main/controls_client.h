#pragma once

#include <stdbool.h>

#include "controls.h"
#include "http_conn.h"

typedef struct {
    ctl_res_t res;
    double at;                     /* esp_timer seconds, as pomo_client_now() */
    char target[CTL_TARGET_MAX];   /* the Mac in the last 202; empty before one */
} ctl_state_t;

/* LVGL task. Offline: dropped with the result CTL_RES_OFFLINE. */
void controls_client_send(ctl_gesture_t g, int steps);
/* Any task. */
void controls_client_get(ctl_state_t *out);
/* Any task. */
bool controls_client_pending(void);
/* Ember task: sends the queue on conn, then stays on it through a turn. */
void controls_client_run(http_conn_t *conn, const char *base);
