#include "j2.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define J2_I2C_PORT I2C_NUM_1
#define DEV_MAX 8
#define XFER_MS 50

typedef struct {
    uint8_t addr;
    uint32_t hz;
    i2c_master_dev_handle_t h;
} bus_dev_t;

static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_mu;
static bus_dev_t s_dev[DEV_MAX];
static size_t s_ndev;

esp_err_t j2_i2c_open(int sda, int scl)
{
    if (!j2_probe_allowed(sda) || !j2_probe_allowed(scl) || sda == scl) return ESP_ERR_INVALID_ARG;
    if (!s_mu && !(s_mu = xSemaphoreCreateMutex())) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (!s_bus) {
        i2c_master_bus_config_t bc = {
            .i2c_port = J2_I2C_PORT,
            .sda_io_num = sda,
            .scl_io_num = scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        err = i2c_new_master_bus(&bc, &s_bus);
        if (err == ESP_OK) i2c_master_bus_reset(s_bus);
    }
    xSemaphoreGive(s_mu);
    return err;
}

void j2_i2c_close(void)
{
    if (!s_mu) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    for (size_t i = 0; i < s_ndev; i++) i2c_master_bus_rm_device(s_dev[i].h);
    s_ndev = 0;
    if (s_bus) i2c_del_master_bus(s_bus);
    s_bus = NULL;
    xSemaphoreGive(s_mu);
}

bool j2_i2c_is_open(void) { return s_bus != NULL; }

static i2c_master_dev_handle_t dev(uint8_t addr, uint32_t hz)
{
    for (size_t i = 0; i < s_ndev; i++)
        if (s_dev[i].addr == addr && s_dev[i].hz == hz) return s_dev[i].h;
    if (s_ndev == DEV_MAX) {
        i2c_master_bus_rm_device(s_dev[0].h);
        for (size_t i = 1; i < s_ndev; i++) s_dev[i - 1] = s_dev[i];
        s_ndev--;
    }
    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = hz};
    i2c_master_dev_handle_t h = NULL;
    if (i2c_master_bus_add_device(s_bus, &dc, &h) != ESP_OK) return NULL;
    s_dev[s_ndev++] = (bus_dev_t){addr, hz, h};
    return h;
}

static esp_err_t xfer(uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{
    if (!s_mu) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    esp_err_t err = s_bus ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
    i2c_master_dev_handle_t h = s_bus ? dev(addr, hz) : NULL;
    if (h) {
        if (wn && rn)
            err = i2c_master_transmit_receive(h, w, wn, r, rn, XFER_MS);
        else if (wn)
            err = i2c_master_transmit(h, w, wn, XFER_MS);
        else
            err = i2c_master_receive(h, r, rn, XFER_MS);
        if (err == ESP_ERR_TIMEOUT) i2c_master_bus_reset(s_bus);
    }
    xSemaphoreGive(s_mu);
    return err;
}

esp_err_t j2_i2c_write(uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn) { return xfer(addr, hz, w, wn, NULL, 0); }

esp_err_t j2_i2c_read(uint8_t addr, uint32_t hz, uint8_t *r, size_t rn) { return xfer(addr, hz, NULL, 0, r, rn); }

esp_err_t j2_i2c_write_read(uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{
    return xfer(addr, hz, w, wn, r, rn);
}

esp_err_t j2_i2c_probe(uint8_t addr)
{
    if (!s_mu) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    esp_err_t err = s_bus ? i2c_master_probe(s_bus, addr, XFER_MS) : ESP_ERR_INVALID_STATE;
    if (err == ESP_ERR_TIMEOUT) i2c_master_bus_reset(s_bus);
    xSemaphoreGive(s_mu);
    return err;
}

size_t j2_i2c_scan(uint8_t *found, size_t cap)
{
    size_t n = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++)
        if (j2_i2c_probe(a) == ESP_OK && n < cap) found[n++] = a;
    return n;
}
