#pragma once

#include <stdbool.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

void panel_check_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io);
/* Takes the LVGL lock. Returns mismatching reads (0 good), -1 error, -2 dimmer wrote meanwhile; raw (optional, >= 32 B): level before, written, read back. */
int panel_check_run(int seed, uint8_t *raw);
