/* All functions: LVGL task only. */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

void np_view_create(lv_obj_t *parent);
/* Hiding frees every picture buffer. */
void np_view_show(bool show);
void np_view_input(double now);
void np_view_turn(int detents, double now);
void np_view_push(double now);
/* zone: a ts_zone_t (touch_swipe.h). */
void np_view_tap(int zone, double now);
/* steps > 0 next, < 0 previous. */
void np_view_skip(int steps, double now);
void np_view_update(double now, bool offline);
