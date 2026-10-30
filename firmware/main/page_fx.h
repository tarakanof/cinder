#pragma once

#include "pages.h"

/* The page wipe and the page dots on the top layer (docs/features.md, main.c split). LVGL task only. */

/* Once in app_main, after the pages. The setup face never creates them. */
void page_fx_create(void);
/* dir: 1 towards the next page, -1 back. */
void wipe_start(int dir, double t);
/* Steps the page; no wipe and no dots when it stays. */
void page_go(pages_nav_t *nav, int steps, double t);
/* Every frame: ends the dots and moves the wipe. */
void page_fx_frame(double t);
/* The page list changed: rebuilds dots that show. */
void dots_refresh(const pages_nav_t *nav);
/* The OTA face: stops the wipe, drops the dots. */
void page_fx_pause(void);
