#include "panel_check.h"

#include <string.h>

#include "bsp_knob_15_md50et.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/spi_periph.h"

#define CS_GPIO 12

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static spi_device_handle_t s_rd;

void panel_check_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io)
{
    s_panel = panel;
    s_io = io;
    /* The panel answers reads on SIO0 with no CS of its own: CS is driven by hand, then handed back to FSPICS0. */
    spi_device_interface_config_t c = {.command_bits = 8, .address_bits = 24, .mode = 0,
                                       .clock_speed_hz = 5 * 1000 * 1000, .spics_io_num = -1,
                                       .flags = SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX, .queue_size = 1};
    if (spi_bus_add_device(SPI2_HOST, &c, &s_rd) != ESP_OK) s_rd = NULL;
    gpio_set_direction(CS_GPIO, GPIO_MODE_INPUT_OUTPUT);
    esp_rom_gpio_connect_out_signal(CS_GPIO, spi_periph_signal[SPI2_HOST].spics_out[0], false, false);
}

static esp_err_t rd(int reg, uint8_t *b, int n)
{
    spi_transaction_t t = {.cmd = 0x03, .addr = (uint32_t)reg << 8, .rxlength = (size_t)n * 8, .rx_buffer = b};
    spi_device_acquire_bus(s_rd, portMAX_DELAY);
    gpio_set_level(CS_GPIO, 0);
    esp_rom_gpio_connect_out_signal(CS_GPIO, SIG_GPIO_OUT_IDX, false, false);
    esp_err_t e = spi_device_polling_transmit(s_rd, &t);
    gpio_set_level(CS_GPIO, 1);
    esp_rom_gpio_connect_out_signal(CS_GPIO, spi_periph_signal[SPI2_HOST].spics_out[0], false, false);
    spi_device_release_bus(s_rd);
    return e;
}

static esp_err_t cmd(int c, const uint8_t *p, int n)
{
    return esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (c << 8), p, n);
}

/* GRAM readback (RAMRD 0x2E) returns a constant 7F 00 00 here: check a register loopback instead (docs/features.md, QSPI 80 MHz). */
int panel_check_run(int seed, uint8_t *raw)
{
    if (!s_rd) return -1;
    if (bsp_knob_15_md50et_lock(1000) != ESP_OK) return -1;
    uint8_t cur[2] = {0}, got[2] = {0};
    esp_err_t e = rd(0x52, cur, 1);
    int bad = 0;
    uint8_t vals[2] = {(uint8_t)(cur[0] ^ (1 + (seed & 1))), cur[0]};
    for (int k = 0; k < 2 && e == ESP_OK; k++) {
        e = cmd(0x51, &vals[k], 1);
        uint32_t w = bsp_knob_15_md50et_brightness_writes();
        bsp_knob_15_md50et_unlock();
        vTaskDelay(pdMS_TO_TICKS(60));
        if (bsp_knob_15_md50et_lock(1000) != ESP_OK) return -1;
        if (bsp_knob_15_md50et_brightness_writes() != w) {
            bsp_knob_15_md50et_unlock();
            if (raw) memset(raw, 0, 32);
            return -2;
        }
        if (e == ESP_OK) e = rd(0x52, got, 1);
        bad += got[0] != vals[k];
    }
    bsp_knob_15_md50et_unlock();
    if (raw) {
        memset(raw, 0, 32);
        raw[0] = cur[0];
        raw[1] = vals[0];
        raw[2] = got[0];
    }
    return e == ESP_OK ? bad : -1;
}
