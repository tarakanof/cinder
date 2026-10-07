#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "pomo.h"

/* LVGL lock held. */
void pomo_view_create(lv_obj_t *parent);
/* now in seconds. LVGL task only. */
void pomo_view_update(const pomo_state_t *s, double now);
void pomo_view_set_note(const char *note);
void pomo_view_show(bool show);
