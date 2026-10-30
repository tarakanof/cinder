#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "pages.h"
#include "reset_gesture.h"

/* Turns hardware and dev-tool events into per-frame counts (docs/features.md, main.c split). */

/* Once in app_main, before the callbacks are registered. */
void input_route_init(void);

/* esp_timer task (iot_knob, iot_button). */
void input_route_knob_cb(void *event);
void input_route_button_cb(void *event);
/* LVGL task: the screen's PRESSED, PRESSING, RELEASED and PRESS_LOST events. */
void input_route_touch_cb(lv_event_t *e);
/* kind 0 turn (n detents, clockwise positive), 1 push, 2 long push, 3 touch, 4 push and turn (n pages), 5/6 swipe up/down, 7/8 swipe left/right, 9/10/11 tap on previous/play/next; any task. */
void input_route_inject(int kind, int n);

/* LVGL task, once a frame. Takes every count but page steps and swipes, and fires a long push held to PR_HOLD_MS. Returns ms held while the hold ring may show, else -1. */
long long input_route_take(page_input_t *out);
/* LVGL task: page steps and swipes, taken apart from input_route_take, before the page changes. */
void input_route_take_steps(int *steps, int *swipes);
/* LVGL task. Any input not yet taken, then drop it all. */
bool input_route_pending(void);
void input_route_drop(void);
/* Any task: the button or a touch is down. */
bool input_route_held(void);
/* LVGL task: the ring shows for this press, so letting go cancels it. */
void input_route_hold_counting(void);

/* LVGL task: rg_tick and the gesture's progress under its lock. */
rg_state_t input_route_reset_tick(double t, int *progress);
/* LVGL task: forget the gesture (the OTA face). */
void input_route_reset_clear(void);

/* main.c, LVGL task, on a touch press: decide tap or swipe on release, else the press is a tap at once. */
bool input_route_classify_touch(void);
