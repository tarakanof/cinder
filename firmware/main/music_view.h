#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "music_moment.h"

/* The bot page's music moment: headphones, notes and the cups' bob (docs/features.md, bot music moments). LVGL task. */
void music_view_create(lv_obj_t *parent);
/* Plays MM_PRE_S after t; the first start paints the sprites into a PSRAM arena kept from then on. False while one
   plays, under the OTA hold, or when the arena cannot be allocated (retried after 5 s). */
bool music_view_start(double t);
/* The moment's time in s from its first tick; -1 while none plays (also during the pre-roll). */
double music_view_poll(double t);
/* Screen px the eyes bob at t, positive down; 0 while none plays. */
float music_view_eye_px(double t);
/* Bot page frames: redraws the box mm_diff gives and ends the moment after its last tick. */
void music_view_frame(double t);
/* Cut short: the phones leave from where they are (mm_abort_tick). */
void music_view_abort(double t);
/* Gone at once. */
void music_view_stop(void);
bool music_view_busy(void);
void music_view_hold(bool hold);
/* For the stats log: moments shown since boot, the paint's ms, starts that found no arena. */
void music_view_stats(int *shown, int *paint_ms, int *failed);
