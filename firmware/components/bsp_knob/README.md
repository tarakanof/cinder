# BSP: bsp_knob_15_md50et

> **Modified copy.** Vendored from VIEWE's `viewesmart/bsp_knob_15_md50et` 1.0.3 (Apache-2.0) and changed for cinder: panel init, brightness, window rounding and power-up order (see `docs/llm.md`). The original licence is in `LICENSE`.

| [HOW TO USE API](#example-use) | [![Component Registry](https://components.espressif.com/components/viewesmart/bsp_knob_15_md50et/badge.svg)](https://components.espressif.com/components/viewesmart/bsp_knob_15_md50et) |
| --- | --- |

[中文](./README_CN.md)

## Overview

Board support package for **UEDX46460015-MD50ET**, a 1.5" round AMOLED knob display module based on ESP32-S3.

* MCU: ESP32-S3 (16 MB Flash + Octal PSRAM recommended)
* Display: 1.5" round AMOLED, CO5300, QSPI
* Touch: CST820, I2C (`driver/i2c_master.h`)
* Input: rotary encoder + button (GPIO0)
* GUI: `esp_lvgl_adapter` with LVGL 8.x or 9.x

| Item | Value |
| ---- | ----- |
| LCD resolution (driver / LVGL) | 472 x 466 |
| SquareLine design size | 466 x 466 |
| LCD interface | QSPI (`SPI2_HOST`) |
| Touch interface | I2C (`I2C_NUM_0`) |
| ESP-IDF | >= 5.5 |
| Target | esp32s3 |

> [!NOTE]
> * GPIO17 (panel power) is driven **HIGH before** LCD reset/init.
> * Do **not** call `esp_lcd_panel_set_gap()` on this board.
> * Pin LVGL major version (`^8` or `^9`) in the application `idf_component.yml` to match your UI export.
> * Brightness uses `esp_lcd_panel_co5300_set_brightness()` (QSPI command encoding is required; do not raw-write `0x51` via `esp_lcd_panel_io_tx_param`).

## Capabilities and dependencies

| Available | Capability | Controller | Component | Version |
| :-------: | ---------- | ---------- | --------- | ------- |
| :heavy_check_mark: | DISPLAY | CO5300 | [espressif/esp_lcd_co5300](https://components.espressif.com/components/espressif/esp_lcd_co5300) | ^2.1.0 |
| :heavy_check_mark: | LVGL_PORT | | [espressif/esp_lvgl_adapter](https://components.espressif.com/components/espressif/esp_lvgl_adapter) | ^0.6.3 |
| :heavy_check_mark: | TOUCH | CST820 | [viewesmart/esp_lcd_touch_cst820](https://components.espressif.com/components/viewesmart/esp_lcd_touch_cst820) | ^1.0.3 |
| :heavy_check_mark: | BUTTONS | | [espressif/button](https://components.espressif.com/components/espressif/button) | ^4.0.0 |
| :heavy_check_mark: | KNOB | | [espressif/knob](https://components.espressif.com/components/espressif/knob) | ^1.0.0 |
| :x: | AUDIO | | | |
| :x: | SDCARD | | | |
| :x: | IMU | | | |

## Add to project

```
idf.py add-dependency "viewesmart/bsp_knob_15_md50et^1.0.3"
```

or in `idf_component.yml`:

```yaml
dependencies:
  viewesmart/bsp_knob_15_md50et: "^1.0.3"
  lvgl/lvgl:
    version: "^8.3.11"   # or "^9"
    public: true
```

More: [IDF Component Manager](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-component-manager.html).

## Example use

```c
#include "bsp_knob_15_md50et.h"
#include "lvgl.h"

void app_main(void)
{
    bsp_knob_15_md50et_handles_t handles = {0};
    ESP_ERROR_CHECK(bsp_knob_15_md50et_init(&handles));

    bsp_knob_15_md50et_register_knob_cb(my_knob_cb);
    bsp_knob_15_md50et_register_button_cb(my_button_cb);

    ESP_ERROR_CHECK(bsp_knob_15_md50et_lock(-1));
    /* create UI under lock, e.g. ui_init(); */
    lv_obj_t *label = lv_label_create(lv_scr_act()); /* LVGL 8; use lv_screen_active() on LVGL 9 */
    lv_label_set_text(label, "Hello");
    lv_obj_center(label);
    bsp_knob_15_md50et_unlock();

    /* optional: panel brightness 0~100 */
    ESP_ERROR_CHECK(bsp_knob_15_md50et_set_brightness(80));
}
```

All LVGL API calls must be between `bsp_knob_15_md50et_lock()` and `bsp_knob_15_md50et_unlock()`.

### API

| Function | Description |
| -------- | ----------- |
| `bsp_knob_15_md50et_init(handles)` | Init power GPIO, CO5300 QSPI LCD, CST820 (I2C master), `esp_lvgl_adapter`, knob, button; start LVGL task |
| `bsp_knob_15_md50et_lock(timeout_ms)` / `unlock()` | LVGL thread-safety lock (`-1` = wait forever) |
| `bsp_knob_15_md50et_backlight_on()` / `off()` | Drive GPIO17 panel power level |
| `bsp_knob_15_md50et_set_brightness(0~100)` | Panel brightness via `esp_lcd_panel_co5300_set_brightness()` |
| `bsp_knob_15_md50et_register_knob_cb()` | Knob callback; arg is `(void *)KNOB_LEFT` / `KNOB_RIGHT` |
| `bsp_knob_15_md50et_register_button_cb()` | Button callback; arg is `(void *)button_event_t` |

Default button events: `BUTTON_PRESS_DOWN`, `BUTTON_PRESS_UP`, `BUTTON_LONG_PRESS_HOLD`.

`bsp_knob_15_md50et_handles_t` fields: `disp`, `touch` (LVGL indev), `panel`, `panel_io`, `tp`.

### Display / SquareLine

| Item | Value |
| ---- | ----- |
| Driver / LVGL size | 472 x 466 |
| SquareLine project | 466 x 466 |
| `set_gap` | Do not use |
| `mirror` | `(false, false)` |

## Pinout

Defined in `include/bsp_knob_15_md50et_board.h`:

| Function | GPIO |
| -------- | ---- |
| LCD CS | 12 |
| LCD PCLK | 10 |
| LCD D0 / D1 / D2 / D3 | 13 / 11 / 14 / 9 |
| LCD RST | 8 |
| Panel power (HIGH before LCD init) | 17 |
| Touch SCL / SDA | 3 / 1 |
| Touch RST / INT | 2 / 4 |
| Encoder A / B | 6 / 5 |
| Button | 0 |

## Configuration

Suggested `sdkconfig.defaults`:

```text
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_LV_COLOR_DEPTH_16=y
CONFIG_LV_COLOR_16_SWAP=y
```

For large UI assets, use a custom `partitions.csv` with a 2-3 MB `factory` app slot.

## Troubleshooting

| Symptom | What to check |
| ------- | ------------- |
| Black screen | GPIO17 high before LCD init (done in `bsp_knob_15_md50et_init`); keep the board CO5300 vendor init table |
| Left blank / right green line | Use 472x466 and do not call `set_gap` |
| Brightness no effect | Use `bsp_knob_15_md50et_set_brightness()` (not raw `0x51` on QSPI IO) |
| Touch / I2C errors | Use new I2C master; CST820: [viewesmart/esp_lcd_touch_cst820](https://components.espressif.com/components/viewesmart/esp_lcd_touch_cst820) |
| LVGL API / link errors | App LVGL major must match UI export |

## License

Apache License 2.0, see [LICENSE](./LICENSE).
