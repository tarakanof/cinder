# BSP: bsp_knob_15_md50et

| [如何使用 API](#使用示例) | [![Component Registry](https://components.espressif.com/components/viewesmart/bsp_knob_15_md50et/badge.svg)](https://components.espressif.com/components/viewesmart/bsp_knob_15_md50et) |
| --- | --- |

[English](./README.md)

## 概述

**UEDX46460015-MD50ET** 板级支持包（BSP）：基于 ESP32-S3 的 1.5 寸圆形 AMOLED 旋钮屏模组(带触摸)。

* MCU：ESP32-S3（建议 16 MB Flash + Octal PSRAM）
* 显示：1.5" 圆形 AMOLED，CO5300，QSPI
* 触摸：CST820，I2C（`driver/i2c_master.h`）
* 输入：旋转编码器 + 按键（GPIO0）
* GUI：`esp_lvgl_adapter`，支持 LVGL 8.x 或 9.x

| 项目 | 数值 |
| ---- | ---- |
| LCD 分辨率（驱动 / LVGL） | 472 x 466 |
| SquareLine 设计尺寸 | 466 x 466 |
| LCD 接口 | QSPI（`SPI2_HOST`） |
| 触摸接口 | I2C（`I2C_NUM_0`） |
| ESP-IDF | >= 5.5 |
| Target | esp32s3 |

> [!NOTE]
> * GPIO17（面板供电）须在 LCD 复位/初始化**之前**拉高。
> * 本板**不要**调用 `esp_lcd_panel_set_gap()`。
> * 请在应用的 `idf_component.yml` 中锁定 LVGL 主版本（`^8` 或 `^9`），以匹配 UI 导出。
> * 亮度请使用 `esp_lcd_panel_co5300_set_brightness()`（QSPI 需驱动编码命令，勿直接 `esp_lcd_panel_io_tx_param` 写 `0x51`）。

## 能力与依赖

| 可用 | 能力 | 控制器 | 组件 | 版本 |
| :--: | ---- | ------ | ---- | ---- |
| :heavy_check_mark: | DISPLAY | CO5300 | [espressif/esp_lcd_co5300](https://components.espressif.com/components/espressif/esp_lcd_co5300) | ^2.1.0 |
| :heavy_check_mark: | LVGL_PORT | | [espressif/esp_lvgl_adapter](https://components.espressif.com/components/espressif/esp_lvgl_adapter) | ^0.6.3 |
| :heavy_check_mark: | TOUCH | CST820 | [viewesmart/esp_lcd_touch_cst820](https://components.espressif.com/components/viewesmart/esp_lcd_touch_cst820) | ^1.0.3 |
| :heavy_check_mark: | BUTTONS | | [espressif/button](https://components.espressif.com/components/espressif/button) | ^4.0.0 |
| :heavy_check_mark: | KNOB | | [espressif/knob](https://components.espressif.com/components/espressif/knob) | ^1.0.0 |
| :x: | AUDIO | | | |
| :x: | SDCARD | | | |
| :x: | IMU | | | |

## 添加到工程

```
idf.py add-dependency "viewesmart/bsp_knob_15_md50et^1.0.3"
```

或在 `idf_component.yml` 中：

```yaml
dependencies:
  viewesmart/bsp_knob_15_md50et: "^1.0.3"
  lvgl/lvgl:
    version: "^8.3.11"   # 或 "^9"
    public: true
```

更多信息见 [IDF Component Manager](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-guides/tools/idf-component-manager.html)。

## 使用示例

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
    /* 在 lock 内创建 UI，例如 ui_init(); */
    lv_obj_t *label = lv_label_create(lv_scr_act()); /* LVGL 8；LVGL 9 用 lv_screen_active() */
    lv_label_set_text(label, "Hello");
    lv_obj_center(label);
    bsp_knob_15_md50et_unlock();

    /* 可选：面板亮度 0~100 */
    ESP_ERROR_CHECK(bsp_knob_15_md50et_set_brightness(80));
}
```

所有 LVGL API 调用须放在 `bsp_knob_15_md50et_lock()` / `bsp_knob_15_md50et_unlock()` 之间。

### API

| 函数 | 说明 |
| ---- | ---- |
| `bsp_knob_15_md50et_init(handles)` | 初始化供电 GPIO、CO5300 QSPI LCD、CST820（I2C master）、`esp_lvgl_adapter`、旋钮、按键；启动 LVGL 任务 |
| `bsp_knob_15_md50et_lock(timeout_ms)` / `unlock()` | LVGL 线程安全锁（`-1` 永久等待） |
| `bsp_knob_15_md50et_backlight_on()` / `off()` | 控制 GPIO17 面板供电电平 |
| `bsp_knob_15_md50et_set_brightness(0~100)` | 面板亮度（内部调用 `esp_lcd_panel_co5300_set_brightness()`） |
| `bsp_knob_15_md50et_register_knob_cb()` | 旋钮回调；参数为 `(void *)KNOB_LEFT` / `KNOB_RIGHT` |
| `bsp_knob_15_md50et_register_button_cb()` | 按键回调；参数为 `(void *)button_event_t` |

默认注册按键事件：`BUTTON_PRESS_DOWN`、`BUTTON_PRESS_UP`、`BUTTON_LONG_PRESS_HOLD`。

`bsp_knob_15_md50et_handles_t` 字段：`disp`、`touch`（LVGL indev）、`panel`、`panel_io`、`tp`。

### 显示 / SquareLine

| 项目 | 数值 |
| ---- | ---- |
| 驱动 / LVGL 尺寸 | 472 x 466 |
| SquareLine 工程 | 466 x 466 |
| `set_gap` | 不要使用 |
| `mirror` | `(false, false)` |

## 引脚

定义于 `include/bsp_knob_15_md50et_board.h`：

| 功能 | GPIO |
| ---- | ---- |
| LCD CS | 12 |
| LCD PCLK | 10 |
| LCD D0 / D1 / D2 / D3 | 13 / 11 / 14 / 9 |
| LCD RST | 8 |
| 面板供电（LCD init 前须为高） | 17 |
| 触摸 SCL / SDA | 3 / 1 |
| 触摸 RST / INT | 2 / 4 |
| 编码器 A / B | 6 / 5 |
| 按键 | 0 |

## 配置

建议的 `sdkconfig.defaults`：

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

UI 资源较大时，建议自定义 `partitions.csv`，将 `factory` 做到 2-3 MB。

## 排障

| 现象 | 排查 |
| ---- | ---- |
| 黑屏 | GPIO17 须在 LCD init 前拉高（`bsp_knob_15_md50et_init` 已处理）；保留本板 CO5300 厂商 init 表 |
| 左边留白 / 右侧绿条 | 使用 472x466，且不要调用 `set_gap` |
| 亮度无变化 | 使用 `bsp_knob_15_md50et_set_brightness()`（QSPI 下勿直接写 `0x51`） |
| 触摸 / I2C 异常 | 使用新版 I2C master；CST820：[viewesmart/esp_lcd_touch_cst820](https://components.espressif.com/components/viewesmart/esp_lcd_touch_cst820) |
| LVGL API / 链接错误 | 应用 LVGL 主版本须与 UI 导出一致 |

## 许可证

Apache License 2.0，见 [LICENSE](./LICENSE)。
