#include "panel_check.h"

#include <stdatomic.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "panel_req.h"
#include "soc/spi_periph.h"

#define CS_GPIO 12
#define POLL_MS 10

static esp_lcd_panel_io_handle_t s_io;
static spi_device_handle_t s_rd;
static panel_req_t s_req;
static atomic_bool s_ready;

void panel_check_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io)
{
    (void)panel;
    s_io = io;
    pr_init(&s_req);
    /* The panel answers reads on SIO0 with no CS of its own: CS is driven by hand, then handed back to FSPICS0. */
    spi_device_interface_config_t c = {.command_bits = 8, .address_bits = 24, .mode = 0,
                                       .clock_speed_hz = 5 * 1000 * 1000, .spics_io_num = -1,
                                       .flags = SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX, .queue_size = 1};
    if (spi_bus_add_device(SPI2_HOST, &c, &s_rd) != ESP_OK) s_rd = NULL;
    gpio_set_direction(CS_GPIO, GPIO_MODE_INPUT_OUTPUT);
    esp_rom_gpio_connect_out_signal(CS_GPIO, spi_periph_signal[SPI2_HOST].spics_out[0], false, false);
    atomic_store(&s_ready, true);
}

static int panel_rd(void *ctx, uint8_t *level)
{
    (void)ctx;
    if (!s_rd) return -1;
    uint8_t b[4] = {0};
    spi_transaction_t t = {.cmd = 0x03, .addr = 0x52 << 8, .rxlength = 8, .rx_buffer = b};
    spi_device_acquire_bus(s_rd, portMAX_DELAY);
    gpio_set_level(CS_GPIO, 0);
    esp_rom_gpio_connect_out_signal(CS_GPIO, SIG_GPIO_OUT_IDX, false, false);
    esp_err_t e = spi_device_polling_transmit(s_rd, &t);
    gpio_set_level(CS_GPIO, 1);
    esp_rom_gpio_connect_out_signal(CS_GPIO, spi_periph_signal[SPI2_HOST].spics_out[0], false, false);
    spi_device_release_bus(s_rd);
    *level = b[0];
    return e == ESP_OK ? 0 : -1;
}

static int panel_wr(void *ctx, uint8_t level)
{
    (void)ctx;
    return esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (0x51 << 8), &level, 1) == ESP_OK ? 0 : -1;
}

static const pr_io_t IO = {panel_rd, panel_wr, NULL};

void panel_check_frame(void)
{
    if (atomic_load(&s_ready)) pr_frame(&s_req, &IO, esp_timer_get_time() / 1000);
}

void panel_check_brightness(uint8_t level) { pr_brightness(&s_req, level); }

/* GRAM readback (RAMRD 0x2E) returns a constant 7F 00 00 here: check a register loopback instead (docs/features.md, QSPI 80 MHz). */
int panel_check_run(int seed, uint8_t *raw, int timeout_ms)
{
    if (raw) memset(raw, 0, 32);
    if (!atomic_load(&s_ready) || !s_rd) return -1;
    uint32_t ticket = pr_check_post(&s_req, seed);
    int bad;
    for (int t = 0; t <= timeout_ms; t += POLL_MS) {
        if (pr_check_result(&s_req, ticket, &bad, raw)) return bad;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return -1;
}
