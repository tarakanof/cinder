#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "usage.h"

/* LVGL task only, all of them. parent: the screen; the face's objects and buffer exist only while it is open. */
void usage_view_create(lv_obj_t *parent);
/* Every frame on any page, before input: new data, the shown account kept by id, closed when none is left. */
void usage_view_poll(void);
bool usage_view_open(void);
/* Ember unreachable: every account shows stale and the hint is off. */
void usage_view_set_offline(bool offline);
bool usage_view_hint(void);
/* True when the usage screens took the input (open before, or opened by it). */
bool usage_view_input(const usage_in_t *in, double t);
void usage_view_close(void);
/* While open: the idle return and the face; true while still open. t: the frame clock of usage_view_input. */
bool usage_view_frame(double t);
