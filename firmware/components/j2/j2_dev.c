#include "j2.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sens_bh1750.h"
#include "sens_pcf8574.h"

#define FAST_HZ 400000

static void sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }

static esp_err_t wr(uint8_t addr, const uint8_t *w, size_t n) { return j2_i2c_write(addr, FAST_HZ, w, n); }

static esp_err_t reg_read(uint8_t addr, uint8_t reg, uint8_t *r, size_t n) { return j2_i2c_write_read(addr, FAST_HZ, &reg, 1, r, n); }

static esp_err_t reg_write(uint8_t addr, uint8_t reg, uint8_t v) { return wr(addr, (const uint8_t[]){reg, v}, 2); }

esp_err_t j2_aht_init(void)
{
    uint8_t st = 0;
    esp_err_t err = j2_i2c_read(AHT_ADDR, FAST_HZ, &st, 1);
    if (err != ESP_OK) return err;
    if (aht_needs_reg_init(st)) {
        static const uint8_t REGS[3] = {0x1B, 0x1C, 0x1E};
        for (int i = 0; i < 3 && err == ESP_OK; i++) {
            uint8_t back[3] = {0}, w[3];
            err = wr(AHT_ADDR, (const uint8_t[]){REGS[i], 0x00, 0x00}, 3);
            sleep_ms(5);
            if (err == ESP_OK) err = j2_i2c_read(AHT_ADDR, FAST_HZ, back, 3);
            sleep_ms(10);
            aht_reg_init_write(REGS[i], back, w);
            if (err == ESP_OK) err = wr(AHT_ADDR, w, 3);
        }
        sleep_ms(10);
        if (err == ESP_OK) err = j2_i2c_read(AHT_ADDR, FAST_HZ, &st, 1);
    }
    if (err == ESP_OK && !(st & AHT_STATUS_CAL)) {
        err = wr(AHT_ADDR, (const uint8_t[]){AHT_CMD_INIT, 0x08, 0x00}, 3);
        sleep_ms(10);
    }
    return err;
}

esp_err_t j2_aht_read(aht_reading_t *out)
{
    esp_err_t err = wr(AHT_ADDR, (const uint8_t[]){AHT_CMD_TRIGGER, 0x33, 0x00}, 3);
    if (err != ESP_OK) return err;
    sleep_ms(AHT_MEASURE_MS);
    for (int tries = 0; tries < 4; tries++) {
        uint8_t f[AHT_FRAME_LEN];
        err = j2_i2c_read(AHT_ADDR, FAST_HZ, f, sizeof f);
        if (err != ESP_OK) return err;
        aht_result_t r = aht_parse(f, out);
        if (r == AHT_OK) return ESP_OK;
        if (r == AHT_CRC) return ESP_ERR_INVALID_CRC;
        sleep_ms(20);
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t j2_ens160_find(uint8_t *addr)
{
    const uint8_t cand[2] = {ENS160_ADDR_HIGH, ENS160_ADDR_LOW};
    for (int i = 0; i < 2; i++) {
        uint8_t id[2];
        if (j2_i2c_probe(cand[i]) != ESP_OK) continue;
        if (reg_read(cand[i], ENS160_REG_PART_ID, id, 2) == ESP_OK && ens160_part_id(id) == ENS160_PART_ID) {
            *addr = cand[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t j2_ens160_start(uint8_t addr)
{
    esp_err_t err = reg_write(addr, ENS160_REG_OPMODE, ENS160_OP_RESET);
    sleep_ms(10);
    if (err == ESP_OK) err = reg_write(addr, ENS160_REG_OPMODE, ENS160_OP_IDLE);
    sleep_ms(10);
    if (err == ESP_OK) err = reg_write(addr, ENS160_REG_OPMODE, ENS160_OP_STANDARD);
    sleep_ms(50);
    return err;
}

esp_err_t j2_ens160_read(uint8_t addr, ens160_status_t *st, ens160_data_t *d, bool *fresh)
{
    uint8_t raw[1 + ENS160_DATA_LEN];
    *fresh = false;
    esp_err_t err = reg_read(addr, ENS160_REG_STATUS, raw, sizeof raw);
    if (err != ESP_OK) return err;
    *st = ens160_status(raw[0]);
    if (st->error) return ESP_FAIL;
    if (st->new_data) *fresh = ens160_data(raw + 1, d);
    return ESP_OK;
}

esp_err_t j2_ens160_comp(uint8_t addr, float temp_c, float rh_pct)
{
    uint8_t w[5] = {ENS160_REG_TEMP_IN};
    ens160_le16(ens160_temp_in(temp_c), w + 1);
    ens160_le16(ens160_rh_in(rh_pct), w + 3);
    return wr(addr, w, sizeof w);
}

esp_err_t j2_bh1750_find(uint8_t *addr)
{
    const uint8_t cand[2] = {BH1750_ADDR_LOW, BH1750_ADDR_HIGH};
    for (int i = 0; i < 2; i++) {
        if (j2_i2c_probe(cand[i]) != ESP_OK) continue;
        uint8_t on = BH1750_POWER_ON;
        if (wr(cand[i], &on, 1) == ESP_OK) {
            *addr = cand[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t j2_bh1750_start(uint8_t addr)
{
    uint8_t c = BH1750_POWER_ON;
    esp_err_t err = wr(addr, &c, 1);
    c = BH1750_CONT_H;
    if (err == ESP_OK) err = wr(addr, &c, 1);
    if (err == ESP_OK) sleep_ms(bh1750_wait_ms(BH1750_CONT_H, BH1750_MT_DEFAULT));
    return err;
}

esp_err_t j2_bh1750_read(uint8_t addr, float *lux)
{
    uint8_t raw[2];
    esp_err_t err = j2_i2c_read(addr, FAST_HZ, raw, 2);
    if (err == ESP_OK) *lux = bh1750_lux(raw, BH1750_CONT_H, BH1750_MT_DEFAULT);
    return err;
}

esp_err_t j2_bmp280_find(uint8_t *addr, bmp280_calib_t *c)
{
    const uint8_t cand[2] = {BMP280_ADDR_HIGH, BMP280_ADDR_LOW};
    for (int i = 0; i < 2; i++) {
        uint8_t id = 0, cal[BMP280_CALIB_LEN];
        if (j2_i2c_probe(cand[i]) != ESP_OK) continue;
        if (reg_read(cand[i], BMP280_REG_ID, &id, 1) != ESP_OK || id != BMP280_CHIP_ID) continue;
        if (reg_read(cand[i], BMP280_REG_CALIB, cal, sizeof cal) != ESP_OK) continue;
        bmp280_calib(cal, c);
        *addr = cand[i];
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t j2_bmp280_read(uint8_t addr, const bmp280_calib_t *c, bmp280_reading_t *out)
{
    esp_err_t err = reg_write(addr, BMP280_REG_CTRL_MEAS, bmp280_forced(1, 3));
    if (err != ESP_OK) return err;
    sleep_ms(14);
    for (int tries = 0; tries < 4; tries++) {
        uint8_t st = 0;
        if ((err = reg_read(addr, BMP280_REG_STATUS, &st, 1)) != ESP_OK) return err;
        if (!(st & 0x08)) break;
        sleep_ms(5);
    }
    uint8_t raw[BMP280_DATA_LEN];
    if ((err = reg_read(addr, BMP280_REG_DATA, raw, sizeof raw)) != ESP_OK) return err;
    return bmp280_compensate(c, raw, out) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static bool pcf_try(uint8_t a)
{
    uint8_t all = PCF8574_ALL_INPUT, port;
    return j2_i2c_probe(a) == ESP_OK && j2_i2c_write(a, PCF8574_HZ, &all, 1) == ESP_OK && j2_i2c_read(a, PCF8574_HZ, &port, 1) == ESP_OK;
}

static bool taken_by(uint8_t a, const uint8_t *taken, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (taken[i] == a) return true;
    return false;
}

esp_err_t j2_pcf_find(uint8_t want, const uint8_t *taken, size_t n_taken, uint8_t *addr)
{
    if (want) {
        if (!pcf8574_addr(want) || taken_by(want, taken, n_taken) || !pcf_try(want)) return ESP_ERR_NOT_FOUND;
        *addr = want;
        return ESP_OK;
    }
    for (uint8_t a = PCF8574_ADDR_MIN; a <= PCF8574A_ADDR_MAX; a++) {
        if (!pcf8574_addr(a) || a == BH1750_ADDR_LOW || a == AHT_ADDR || taken_by(a, taken, n_taken)) continue;
        if (pcf_try(a)) return *addr = a, ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t j2_pcf_read(uint8_t addr, uint8_t *port) { return j2_i2c_read(addr, PCF8574_HZ, port, 1); }
