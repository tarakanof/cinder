#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pages.h"

/* The jump to the print page on a new `started` event: the user's input time, the jump's gate and its hold while page_tick does not run (docs/features.md, print page jump). LVGL task only. */

typedef struct {
    pages_nav_t *nav;
    const uint32_t *nav_gen; /* the settings generation nav was built from (config_store_settings_gen) */
} print_jump_ctx_t;

/* Once in app_main, before the first frame. */
void print_jump_ctl_init(const print_jump_ctx_t *ctx);
/* The user gave input at t: a turn, swipe, push, touch, the long push held, or the screen's wake. */
void print_jump_ctl_input(double t);
/* A frame without page_tick (screen off, the OTA face, the reset screen, setup): notes the event, never jumps. */
void print_jump_ctl_hold(double t);
/* In page_tick after the page's input. real: scene_real and online. True when it switched to the print page. */
bool print_jump_ctl_frame(double t, bool real);
