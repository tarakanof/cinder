/* LVGL task only. */
#pragma once

#include <stdbool.h>

#include "np.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NP_DOUBLE_S 0.35
#define NP_VOL_STEP 2
#define NP_VOL_SEND_S 0.25
#define NP_HOLD_S 3.0
#define NP_VOL_SHOW_S 1.5

typedef enum { NP_CMD_NONE, NP_CMD_PLAY, NP_CMD_PAUSE, NP_CMD_NEXT, NP_CMD_PREVIOUS, NP_CMD_VOLUME } np_cmd_kind_t;

typedef struct {
    np_cmd_kind_t kind;
    int delta;
} np_cmd_t;

typedef struct {
    bool push_waiting;
    double push_t;
    bool next_due, prev_due;
    np_play_t server_state;
    int server_vol;
    bool play_set;
    np_play_t play;
    double play_t;
    int vol;
    int vol_unsent;
    int vol_gesture;
    double vol_t;
    double vol_sent_t;
    bool vol_ever;
    np_cmd_kind_t last;
    double last_t;
} np_ctl_t;

void np_ctl_init(np_ctl_t *c);
void np_ctl_server(np_ctl_t *c, np_play_t state, int volume, double now);
void np_ctl_push(np_ctl_t *c, double now);
void np_ctl_long_push(np_ctl_t *c, double now);
void np_ctl_turn(np_ctl_t *c, int detents, double now);
int np_ctl_tick(np_ctl_t *c, double now, np_cmd_t *out, int max);
void np_ctl_failed(np_ctl_t *c);
np_play_t np_ctl_play_shown(const np_ctl_t *c, double now);
int np_ctl_volume_shown(const np_ctl_t *c, double now);
bool np_ctl_volume_active(const np_ctl_t *c, double now);
const char *np_cmd_action(np_cmd_kind_t k);
int np_cmd_body(const np_cmd_t *cmd, const char *source, const char *track_id, char *buf, int cap);

#ifdef __cplusplus
}
#endif
