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
void np_view_long_push(double now);
void np_view_update(double now, bool offline);
