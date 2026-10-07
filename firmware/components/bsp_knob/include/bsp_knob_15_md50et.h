#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"
#include "bsp_knob_15_md50et_board.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_display_t *disp;
    lv_indev_t *touch;
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_touch_handle_t tp;
} bsp_knob_15_md50et_handles_t;

typedef void (*bsp_knob_15_md50et_knob_cb_t)(void *event);

typedef void (*bsp_knob_15_md50et_button_cb_t)(void *event);

/* cinder: the panel comes up at brightness 0; set the level with bsp_knob_15_md50et_set_brightness_level() once the UI is built. */
esp_err_t bsp_knob_15_md50et_init(bsp_knob_15_md50et_handles_t *out_handles);

esp_err_t bsp_knob_15_md50et_lock(int timeout_ms);
/* cinder (#23) additions: qspi_*, set_qspi_fast, brightness_writes. */
int bsp_knob_15_md50et_qspi_hz(void);
/* esp_timer us of the last touch read that found a finger (0 before any); LVGL task only. */
int64_t bsp_knob_15_md50et_touch_report_us(void);
void bsp_knob_15_md50et_qspi_fallback_reboot(void);
/* cinder: call before bsp_knob_15_md50et_init(); the SPI clock is fixed at creation. */
void bsp_knob_15_md50et_set_qspi_fast(bool on);
bool bsp_knob_15_md50et_qspi_fallback_active(void);
uint32_t bsp_knob_15_md50et_brightness_writes(void);
void bsp_knob_15_md50et_unlock(void);

void bsp_knob_15_md50et_backlight_on(void);
void bsp_knob_15_md50et_backlight_off(void);

esp_err_t bsp_knob_15_md50et_set_brightness(uint8_t percent);

/** cinder: raw CO5300 WRDISBV level 0-255; takes the LVGL lock, call after init, safe from any task. */
esp_err_t bsp_knob_15_md50et_set_brightness_level(uint8_t level);

void bsp_knob_15_md50et_register_knob_cb(bsp_knob_15_md50et_knob_cb_t cb);
void bsp_knob_15_md50et_register_button_cb(bsp_knob_15_md50et_button_cb_t cb);

#ifdef __cplusplus
}
#endif
