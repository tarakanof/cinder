#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "pomo.h"

/* LVGL lock held. */
void pomo_view_create(lv_obj_t *parent);
/* now in seconds. LVGL task only. */
void pomo_view_update(const pomo_est_t *e, double now);
/* Ring and phase colours as 0xRRGGBB for FOCUS and the breaks; the next update paints them. */
void pomo_view_set_colors(uint32_t focus_rgb, uint32_t break_rgb);
void pomo_view_set_note(const char *note);
void pomo_view_show(bool show);
