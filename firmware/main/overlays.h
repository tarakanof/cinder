#pragma once

#include <stdbool.h>

#include "pages.h"
#include "reset_gesture.h"

/* The factory reset screen and the long push ring on the top layer (docs/features.md, main.c split). LVGL task only. */

/* Once in app_main, setup face included. */
void overlays_create(void);
/* Every frame, with input_route_reset_tick's answer. Starts the erase on RG_CONFIRMED. True on the frame the screen closes, so the page redraws. */
bool overlays_reset_frame(rg_state_t st, int progress);
/* The OTA face: hides the reset screen and forgets it. */
void overlays_pause(void);
/* Every frame. held: input_route_take's answer, ms held while the ring may show, else -1. Calls input_route_hold_counting while the page has a label. */
void overlays_hold_tick(const page_desc_t *pg, long long held);
