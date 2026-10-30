#include "j2.h"

#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define RADAR_UART UART_NUM_1
#define RX_BUF 1024

esp_err_t j2_radar_listen(int rx_gpio, uint32_t ms, j2_radar_cb_t cb, void *arg, ld2410_rx_t *stats)
{
    ld2410_rx_init(stats);
    if (!j2_probe_allowed(rx_gpio)) return ESP_ERR_INVALID_ARG;
    uart_config_t uc = {
        .baud_rate = LD2410_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(RADAR_UART, RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(RADAR_UART, &uc);
    if (err == ESP_OK) err = uart_set_pin(RADAR_UART, UART_PIN_NO_CHANGE, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    uint8_t buf[128];
    while (err == ESP_OK && esp_timer_get_time() < end) {
        int n = uart_read_bytes(RADAR_UART, buf, sizeof buf, pdMS_TO_TICKS(50));
        for (int i = 0; i < n; i++) {
            ld2410_report_t r;
            if (ld2410_rx_byte(stats, buf[i], &r) && cb) cb(&r, arg);
        }
    }
    uart_driver_delete(RADAR_UART);
    return err;
}
