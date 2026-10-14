#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "music_moment.h"

/* LVGL task only, except music_view_stats. */
void music_view_create(lv_obj_t *parent);
/* Plays MM_PRE_S after t; false while one plays or under the hold. */
bool music_view_start(double t);
/* Seconds since the first tick; -1 while none plays, also during the pre-roll. */
double music_view_poll(double t);
/* Screen px, positive down. */
float music_view_eye_px(double t);
void music_view_frame(double t);
void music_view_abort(double t);
void music_view_stop(void);
bool music_view_busy(void);
void music_view_hold(bool hold);
/* Any task. */
void music_view_stats(int *shown, int *paint_ms);
