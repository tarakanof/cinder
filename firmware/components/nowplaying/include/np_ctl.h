/* LVGL task only. */
#pragma once

#include <stdbool.h>

#include "np.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NP_VOL_STEP 2
#define NP_VOL_SEND_S 0.25
#define NP_HOLD_S 3.0
#define NP_VOL_SHOW_S 1.5
#define NP_TOUCH_SHOW_S 4.0
#define NP_TOUCH_FADE_S 0.3
#define NP_TOUCH_FADE_STEPS 3
#define NP_FLASH_S 0.8
#define NP_PULSE_S 0.3

typedef enum { NP_CMD_NONE, NP_CMD_PLAY, NP_CMD_PAUSE, NP_CMD_NEXT, NP_CMD_PREVIOUS, NP_CMD_VOLUME } np_cmd_kind_t;

typedef struct {
    np_cmd_kind_t kind;
    int delta;
} np_cmd_t;

typedef enum { NP_FB_NONE, NP_FB_PULSE, NP_FB_BADGE } np_fb_t;

typedef struct {
    bool play_due, next_due, prev_due;
    bool touch_on;
    double touch_t;
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
    bool last_shown;
} np_ctl_t;

void np_ctl_init(np_ctl_t *c);
void np_ctl_server(np_ctl_t *c, np_play_t state, int volume, double now);
void np_ctl_push(np_ctl_t *c, double now);
/* dir > 0 next, < 0 previous. */
void np_ctl_skip(np_ctl_t *c, int dir, double now);
/* A tap on the touch controls: hidden, it only shows them (a command badge still up turns into its control's pulse); shown, cmd NP_CMD_PLAY (play/pause), NP_CMD_NEXT or NP_CMD_PREVIOUS runs, NP_CMD_NONE keeps them up. False when it ran nothing. */
bool np_ctl_tap(np_ctl_t *c, np_cmd_kind_t cmd, double now);
/* Touch controls' opacity 0-255: full for NP_TOUCH_SHOW_S after the last tap, then NP_TOUCH_FADE_STEPS steps down over NP_TOUCH_FADE_S. */
int np_ctl_touch_opa(const np_ctl_t *c, double now);
void np_ctl_turn(np_ctl_t *c, int detents, double now);
int np_ctl_tick(np_ctl_t *c, double now, np_cmd_t *out, int max);
/* Where the last play, pause, next or previous shows: sent while the touch controls showed, a pulse on its control for NP_PULSE_S; else the badge for NP_FLASH_S. *cmd gets the command unless NP_FB_NONE. */
np_fb_t np_ctl_feedback(const np_ctl_t *c, double now, np_cmd_kind_t *cmd);
void np_ctl_failed(np_ctl_t *c);
np_play_t np_ctl_play_shown(const np_ctl_t *c, double now);
int np_ctl_volume_shown(const np_ctl_t *c, double now);
bool np_ctl_volume_active(const np_ctl_t *c, double now);
const char *np_cmd_action(np_cmd_kind_t k);
int np_cmd_body(const np_cmd_t *cmd, const char *source, const char *track_id, char *buf, int cap);

#ifdef __cplusplus
}
#endif
