#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

/* The LVGL task is the only panel owner after boot: other tasks queue brightness writes and link checks here. */
void panel_check_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io);
/* LVGL task, LVGL lock held, every frame (also while the screen is frozen). */
void panel_check_frame(void);
/* Any task, never blocks; the newest level is written at the next frame (after a check in progress). A level queued before panel_check_init is kept. */
void panel_check_brightness(uint8_t level);
/* One task only (link). Waits at most timeout_ms for the LVGL task; returns mismatching reads (0 good) or -1 (error, or
   the LVGL task did not run the check in time); raw (optional, >= 32 B): level before, written, read back. */
int panel_check_run(int seed, uint8_t *raw, int timeout_ms);
