#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum { HWT_UI_ROOM, HWT_UI_PROBE, HWT_UI_BUSY } hwt_ui_mode_t;

/* Any task: the LVGL task copies this state on its own timer, so callers never take the display lock. */
esp_err_t hwt_ui_start(void);
void hwt_ui_room(const char *pins_line);
void hwt_ui_probe(int gpio, int index, int count);
void hwt_ui_busy(const char *headline, const char *detail);
