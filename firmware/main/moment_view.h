#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "wx_moment.h"

/* The bot page's weather moments: the sprite box and the text line (docs/features.md, bot weather moments). LVGL task
   only, except the bake task this starts. */
void moment_view_create(lv_obj_t *parent);
/* Starts baking m's sprite; it plays once baked, the full variant not before WXM_PRE_S after t (the eyes' glide back to
   the viewer). False while a moment bakes or plays, or under the OTA hold. */
bool moment_view_start(const wxm_moment_t *m, double t);
/* The playing moment's time in s, from 0 on the call that starts it playing; -1 while none plays. */
double moment_view_poll(double t);
/* Bot page frames: shows the playing moment's frame at t and ends it after its last frame. */
void moment_view_frame(double t);
/* Cut short: the sprite and the text leave from where they are (wxm_abort_frame). */
void moment_view_abort(double t);
/* Hides it at once; a bake in flight is dropped when it lands. */
void moment_view_stop(void);
bool moment_view_busy(void);
/* OTA: stops the moment and starts no bake until released. */
void moment_view_hold(bool hold);
/* For the stats log: moments shown since boot, the last bake's ms. */
void moment_view_stats(int *shown, int *bake_ms);
