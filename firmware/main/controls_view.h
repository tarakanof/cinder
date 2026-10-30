/* All functions: LVGL task only. */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

/* Keeps parent; the page's objects exist only while it shows. */
void controls_view_create(lv_obj_t *parent);
void controls_view_show(bool on);
/* target: the Mac, may be empty. note: a string that outlives the view. */
void controls_view_update(const char *target, const char *note);
