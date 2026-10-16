#pragma once

#include "bot_behavior.h"
#include "ember_host.h"
#include "lvgl.h"

#define BOT_VIEW_EYE_SCALE 1.15
#define BOT_VIEW_OFFLINE_DY 140
#define BOT_VIEW_LABEL_W 340
#define BOT_VIEW_LABEL_H 116
#define BOT_VIEW_LABEL_Y 312

void bot_view_create(lv_obj_t *parent);
/* LVGL task only; t: the frame's time, bot_pose's clock. */
void bot_view_update(const bot_pose_t *p, double t);
double bot_view_radius_px(void);
/* LVGL task only; text "" hides the label. */
void bot_view_set_host(const ember_host_info_t *host);
/* LVGL task only. */
void bot_view_set_options(bool source_label, bool working_ring);
/* LVGL task only: the usage limit hint, a short red arc on the round outline's rim; it hides while the outline is not
   the round base (squash variants, the error triangle). */
void bot_view_set_hint(bool on);
/* LVGL task only: the link glyph under the face. */
void bot_view_set_offline(bool on);
/* t in seconds; LVGL task only. Returns true when it invalidated a step. */
bool bot_view_tick(double t, bool defer);
/* LVGL task only: a demo's chase shows the glint in any mood with the round outline, working ring setting or not. */
void bot_view_glint_force(bool on);
/* Screen degrees, 0 = 3 o'clock, clockwise. */
bool bot_view_glint_deg(double t, float *deg);
/* Opacity 0..1, offset in px; LVGL task only. */
void bot_view_label_fx(float opa, int shift_x, int shift_y);
/* Before bot_view_update each frame; both false restores a wiped label. LVGL task only. */
void bot_view_chase(bool merge, bool wipe);
/* LVGL task only: the tool icon of the host label steps aside (off) or returns. a: its screen box, even-aligned; false
   (a empty) when the label shows no icon. Redraws nothing: bot_view_icon_redraw(a) does. */
bool bot_view_icon_off(bool off, lv_area_t *a);
void bot_view_icon_redraw(const lv_area_t *a);
/* LVGL task only, every frame on any page before bot_pose: bot_rim_ready's input; shown: the bot page shows (off it, bases only). */
bool bot_view_rim_feed(bot_mood_t m, bool asleep, bool shown);
/* LVGL task only, every frame on any page right after bot_pose: the outline follows the pose's face (swaps while hidden too). */
void bot_view_rim_pose(const bot_pose_t *p, double t);
/* LVGL task only: true drops the background outline job until false, which redoes it. */
void bot_view_rim_hold(bool hold);
