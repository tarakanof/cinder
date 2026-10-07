#pragma once

#include "bot_behavior.h"
#include "ember_host.h"
#include "lvgl.h"

#define BOT_VIEW_EYE_SCALE 1.15

void bot_view_create(lv_obj_t *parent);
/* LVGL task only. */
void bot_view_update(const bot_pose_t *p);
double bot_view_radius_px(void);
/* LVGL task only; text "" hides the label. */
void bot_view_set_host(const ember_host_info_t *host);
/* LVGL task only. */
void bot_view_set_options(bool source_label, bool working_ring);
/* t in seconds; LVGL task only. Returns true when it invalidated a step. */
bool bot_view_tick(double t, bool defer);
/* Screen degrees, 0 = 3 o'clock, clockwise. */
bool bot_view_glint_deg(double t, float *deg);
/* Opacity 0..1, offset in px; LVGL task only. */
void bot_view_label_fx(float opa, int shift_x, int shift_y);
/* Before bot_view_update each frame; both false restores a wiped label. LVGL task only. */
void bot_view_chase(bool merge, bool wipe);
