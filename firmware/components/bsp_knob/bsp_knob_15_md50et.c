#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_lcd_panel_io.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst820.h"
#include "esp_lv_adapter.h"
#include "iot_knob.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "knob_rotation.h"

#include "bsp_knob_15_md50et.h"

/* Local copy of viewesmart/bsp_knob_15_md50et 1.0.3 (Apache-2.0, see LICENSE); changes are marked "cinder". */

static const char *TAG = "bsp_knob_15_md50et";

#define LCD_BIT_PER_PIXEL 16
#define LVGL_DRAW_BUF_HEIGHT 60

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_touch_handle_t s_tp;
static lv_display_t *s_disp;
static lv_indev_t *s_touch_indev;
static knob_handle_t s_knob;
static button_handle_t s_btn;
static bsp_knob_15_md50et_knob_cb_t s_knob_cb;
static bsp_knob_15_md50et_button_cb_t s_button_cb;
static atomic_int s_tp_irq = 1;
static bool s_tp_down;
static lv_point_t s_tp_point;
static int64_t s_tp_report_us;
static atomic_uint s_tp_ok;
static atomic_int s_rotation;

static void tp_isr(esp_lcd_touch_handle_t tp)
{
    (void)tp;
    atomic_store(&s_tp_irq, 1);
}

/* cinder (#57): the adapter reads the CST820 only after an INT, so a lift without one stuck the press; poll while pressed (docs/features.md) */
static void tp_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (atomic_exchange(&s_tp_irq, 0) || s_tp_down) {
        esp_lcd_touch_point_data_t pt[1] = {0};
        uint8_t n = 0;
        bool ok = esp_lcd_touch_read_data(s_tp) == ESP_OK && esp_lcd_touch_get_data(s_tp, pt, &n, 1) == ESP_OK;
        if (ok) atomic_fetch_add_explicit(&s_tp_ok, 1, memory_order_relaxed);
        s_tp_down = ok && n > 0;
        if (s_tp_down) {
            int x = pt[0].x, y = pt[0].y;
            kr_touch(atomic_load(&s_rotation), &x, &y);
            s_tp_point.x = x;
            s_tp_point.y = y;
            s_tp_report_us = esp_timer_get_time();
        }
    }
    data->point = s_tp_point;
    data->state = s_tp_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int64_t bsp_knob_15_md50et_touch_report_us(void) { return s_tp_report_us; }
uint32_t bsp_knob_15_md50et_touch_ok(void) { return atomic_load_explicit(&s_tp_ok, memory_order_relaxed); }

/* Vendor init table; the driver default does not work on this panel (docs/llm.md). */
static const co5300_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 0, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 10},   /* cinder: TEON takes 1 byte (M=0: V-blank only); sending 0 bytes is wrong (docs/features.md, QSPI 80 MHz) */
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x51, (uint8_t[]){0x00}, 1, 10},   /* cinder: 0x00, dark until the app sets its startup level (#22) */
    {0x63, (uint8_t[]){0xFF}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xD7}, 4, 0}, /* 0..471: the panel is 472 wide (docs/llm.md) */
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, (uint8_t[]){0x00}, 0, 60},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

static void backlight_set(bool on)
{
#if BSP_KNOB_15_MD50ET_PIN_BK_LIGHT >= 0
    gpio_set_level(BSP_KNOB_15_MD50ET_PIN_BK_LIGHT, on ? BSP_KNOB_15_MD50ET_BK_LIGHT_ON_LEVEL
                                                       : !BSP_KNOB_15_MD50ET_BK_LIGHT_ON_LEVEL);
#else
    (void)on;
#endif
}

static esp_err_t backlight_init(void)
{
#if BSP_KNOB_15_MD50ET_PIN_BK_LIGHT >= 0
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BSP_KNOB_15_MD50ET_PIN_BK_LIGHT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bk_gpio_config), TAG, "bk light gpio");
    /* VCI_EN (GPIO17) must be high before LCD reset/init or the panel stays dark (docs/llm.md) */
    backlight_set(true);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "GPIO%d backlight high before LCD init", (int)BSP_KNOB_15_MD50ET_PIN_BK_LIGHT);
#endif
    return ESP_OK;
}

#define BSP_QSPI_FAST_HZ (80 * 1000 * 1000)
#define QSPI_FALLBACK_MAGIC 0x51F040u
static RTC_NOINIT_ATTR uint32_t s_qspi_fallback;
static int s_qspi_hz;

static bool s_qspi_want_fast = true;

int bsp_knob_15_md50et_qspi_hz(void) { return s_qspi_hz; }
void bsp_knob_15_md50et_set_qspi_fast(bool on) { s_qspi_want_fast = on; }
bool bsp_knob_15_md50et_qspi_fallback_active(void) { return s_qspi_fallback == QSPI_FALLBACK_MAGIC; }

void bsp_knob_15_md50et_qspi_fallback_reboot(void)
{
    s_qspi_fallback = QSPI_FALLBACK_MAGIC;
    esp_restart();
}

static esp_err_t panel_orient(int deg)
{
    kr_panel_t p = kr_panel(deg);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, p.gap_x, p.gap_y), TAG, "gap");
    return esp_lcd_panel_mirror(s_panel, p.mirror_x, p.mirror_y);
}

static esp_err_t lcd_init(void)
{
    ESP_LOGI(TAG, "Initialize QSPI bus");
    const spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(
        BSP_KNOB_15_MD50ET_PIN_LCD_PCLK,
        BSP_KNOB_15_MD50ET_PIN_LCD_DATA0,
        BSP_KNOB_15_MD50ET_PIN_LCD_DATA1,
        BSP_KNOB_15_MD50ET_PIN_LCD_DATA2,
        BSP_KNOB_15_MD50ET_PIN_LCD_DATA3,
        BSP_KNOB_15_MD50ET_H_RES * BSP_KNOB_15_MD50ET_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_KNOB_15_MD50ET_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config =
        CO5300_PANEL_IO_QSPI_CONFIG(BSP_KNOB_15_MD50ET_PIN_LCD_CS, NULL, NULL);
    /* cinder: 80 MHz QSPI is out of the CO5300 write spec but measured stable (docs/features.md) */
    if (esp_reset_reason() != ESP_RST_SW) s_qspi_fallback = 0;
    s_qspi_hz = s_qspi_fallback == QSPI_FALLBACK_MAGIC || !s_qspi_want_fast ? 40 * 1000 * 1000 : BSP_QSPI_FAST_HZ;
    io_config.pclk_hz = s_qspi_hz;
    ESP_LOGI(TAG, "QSPI %d MHz%s", s_qspi_hz / 1000000, s_qspi_fallback == QSPI_FALLBACK_MAGIC ? " (fallback)" : "");
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_KNOB_15_MD50ET_SPI_HOST,
                                                 &io_config, &s_panel_io),
                        TAG, "panel io");

    ESP_LOGI(TAG, "Install CO5300 panel driver");
    co5300_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_KNOB_15_MD50ET_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_panel_io, &panel_config, &s_panel), TAG, "co5300");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(panel_orient(atomic_load(&s_rotation)), TAG, "orient");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");
    return ESP_OK;
}

static esp_err_t touch_init(void)
{
    ESP_LOGI(TAG, "Initialize I2C master bus for touch");
    i2c_master_bus_handle_t i2c_bus = NULL;
    const i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BSP_KNOB_15_MD50ET_TOUCH_I2C_HOST,
        .sda_io_num = BSP_KNOB_15_MD50ET_PIN_TOUCH_SDA,
        .scl_io_num = BSP_KNOB_15_MD50ET_PIN_TOUCH_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_conf, &i2c_bus), TAG, "i2c bus");

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle), TAG, "touch io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_KNOB_15_MD50ET_H_RES,
        .y_max = BSP_KNOB_15_MD50ET_V_RES,
        .rst_gpio_num = BSP_KNOB_15_MD50ET_PIN_TOUCH_RST,
        .int_gpio_num = BSP_KNOB_15_MD50ET_PIN_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_LOGI(TAG, "Initialize CST820 touch");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst820(tp_io_handle, &tp_cfg, &s_tp), TAG, "cst820");
    return ESP_OK;
}

static void knob_event_cb(void *arg, void *data)
{
    (void)arg;
    if (s_knob_cb) {
        s_knob_cb(data);
    }
}

static void button_event_cb(void *arg, void *data)
{
    (void)arg;
    if (s_button_cb) {
        s_button_cb(data);
    }
}

static esp_err_t input_init(void)
{
    knob_config_t cfg = {
        .default_direction = 0,
        .gpio_encoder_a = BSP_KNOB_15_MD50ET_PIN_ENCODER_A,
        .gpio_encoder_b = BSP_KNOB_15_MD50ET_PIN_ENCODER_B,
    };
    s_knob = iot_knob_create(&cfg);
    ESP_RETURN_ON_FALSE(s_knob, ESP_FAIL, TAG, "knob create");
    ESP_RETURN_ON_ERROR(iot_knob_register_cb(s_knob, KNOB_LEFT, knob_event_cb, (void *)KNOB_LEFT), TAG, "knob L");
    ESP_RETURN_ON_ERROR(iot_knob_register_cb(s_knob, KNOB_RIGHT, knob_event_cb, (void *)KNOB_RIGHT), TAG, "knob R");

    button_config_t btn_cfg = {0};
    button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = BSP_KNOB_15_MD50ET_PIN_BUTTON,
        .active_level = 0,
    };
    ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &s_btn), TAG, "button create");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn, BUTTON_PRESS_DOWN, NULL, button_event_cb,
                                               (void *)BUTTON_PRESS_DOWN),
                        TAG, "btn down");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn, BUTTON_PRESS_UP, NULL, button_event_cb,
                                               (void *)BUTTON_PRESS_UP),
                        TAG, "btn up");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn, BUTTON_LONG_PRESS_HOLD, NULL, button_event_cb,
                                               (void *)BUTTON_LONG_PRESS_HOLD),
                        TAG, "btn long hold");
    return ESP_OK;
}

static void area_rounder_cb(lv_area_t *area, void *user_data)
{
    (void)user_data;
    /* CO5300 needs even window coordinates inside the panel, else a green line shows at the right edge (docs/llm.md) */
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
    if (area->x1 < 0) {
        area->x1 = 0;
    }
    if (area->y1 < 0) {
        area->y1 = 0;
    }
    if (area->x2 >= BSP_KNOB_15_MD50ET_H_RES) {
        area->x2 = BSP_KNOB_15_MD50ET_H_RES - 1;
    }
    if (area->y2 >= BSP_KNOB_15_MD50ET_V_RES) {
        area->y2 = BSP_KNOB_15_MD50ET_V_RES - 1;
    }
}

static esp_err_t lvgl_adapter_bringup(void)
{
    ESP_LOGI(TAG, "Initialize LVGL adapter");
    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_core_id = 0;   /* cinder: LVGL on core 0; outline variants render on core 1 */
    adapter_config.task_stack_size = 10496;   /* cinder: worst-case estimate 8896 B + 1.5 KB margin (docs/features.md, stack estimates) */
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), TAG, "adapter init");

    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
        s_panel,
        s_panel_io,
        BSP_KNOB_15_MD50ET_H_RES,
        BSP_KNOB_15_MD50ET_V_RES,
        ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.buffer_height = LVGL_DRAW_BUF_HEIGHT;
    /* cinder: 2 draw buffers in internal DMA RAM (113 KB); the vendor single PSRAM buffer stuttered */
    display_config.profile.use_psram = false;
    display_config.profile.require_double_buffer = true;
    /* No TE sync: GPIO38 floats and TE needs a PSRAM frame buffer the QSPI DMA cannot send (docs/features.md) */

    s_disp = esp_lv_adapter_register_display(&display_config);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "register display");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_set_area_rounder_cb(s_disp, area_rounder_cb, NULL), TAG, "rounder");

    if (s_tp) {
        esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_disp, s_tp);
        s_touch_indev = esp_lv_adapter_register_touch(&touch_config);
        ESP_RETURN_ON_FALSE(s_touch_indev, ESP_FAIL, TAG, "register touch");
        ESP_RETURN_ON_ERROR(esp_lv_adapter_lock(-1), TAG, "lock");
        lv_indev_set_read_cb(s_touch_indev, tp_read);
        esp_lv_adapter_unlock();
        ESP_RETURN_ON_ERROR(esp_lcd_touch_register_interrupt_callback(s_tp, tp_isr), TAG, "touch isr");
    }

    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "adapter start");
    return ESP_OK;
}

esp_err_t bsp_knob_15_md50et_init(bsp_knob_15_md50et_handles_t *out_handles)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(lcd_init(), TAG, "lcd");
    /* cinder (#22): no set_brightness(100); the panel stays at 0 until the app sets its level */
    ESP_RETURN_ON_ERROR(touch_init(), TAG, "touch");
    ESP_RETURN_ON_ERROR(lvgl_adapter_bringup(), TAG, "lvgl");
    ESP_RETURN_ON_ERROR(input_init(), TAG, "input");

    if (out_handles) {
        memset(out_handles, 0, sizeof(*out_handles));
        out_handles->disp = s_disp;
        out_handles->touch = s_touch_indev;
        out_handles->panel = s_panel;
        out_handles->panel_io = s_panel_io;
        out_handles->tp = s_tp;
    }
    ESP_LOGI(TAG, "Board ready: %dx%d CO5300 + CST820 + LVGL%d (via esp_lvgl_adapter)",
             BSP_KNOB_15_MD50ET_H_RES, BSP_KNOB_15_MD50ET_V_RES, (int)LVGL_VERSION_MAJOR);
    return ESP_OK;
}

esp_err_t bsp_knob_15_md50et_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms);
}

void bsp_knob_15_md50et_unlock(void)
{
    esp_lv_adapter_unlock();
}

void bsp_knob_15_md50et_backlight_on(void)
{
    backlight_set(true);
}

void bsp_knob_15_md50et_backlight_off(void)
{
    backlight_set(false);
}

esp_err_t bsp_knob_15_md50et_set_brightness(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(s_panel, ESP_ERR_INVALID_STATE, TAG, "panel not ready");
    if (percent > 100) {
        percent = 100;
    }
    /* QSPI commands must go through the driver encoding, not a raw tx_param(0x51) (docs/llm.md) */
    return esp_lcd_panel_co5300_set_brightness(s_panel, percent);
}


int bsp_knob_15_md50et_rotation(void) { return atomic_load(&s_rotation); }

esp_err_t bsp_knob_15_md50et_set_rotation(int deg)
{
    deg = kr_effective(deg);
    if (!s_panel) {
        atomic_store(&s_rotation, deg);
        return ESP_OK;
    }
    if (deg == atomic_load(&s_rotation)) return ESP_OK;
    esp_err_t err = panel_orient(deg);
    if (err != ESP_OK) {
        panel_orient(atomic_load(&s_rotation));
        return err;
    }
    atomic_store(&s_rotation, deg);
    if (s_disp) {
        lv_obj_invalidate(lv_display_get_screen_active(s_disp));
        lv_obj_invalidate(lv_display_get_layer_top(s_disp));
    }
    return ESP_OK;
}

void bsp_knob_15_md50et_register_knob_cb(bsp_knob_15_md50et_knob_cb_t cb)
{
    s_knob_cb = cb;
}

void bsp_knob_15_md50et_register_button_cb(bsp_knob_15_md50et_button_cb_t cb)
{
    s_button_cb = cb;
}
