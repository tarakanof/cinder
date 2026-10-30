#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sens_aht.h"
#include "sens_bmp280.h"
#include "sens_ens160.h"
#include "sens_ld2410.h"
#include "sens_room.h"

typedef struct {
    int sda, scl;
    int bclk, lrc, din;
    int radar_rx;
    int radar_bit;
    uint8_t pcf_addr;
    uint32_t presence_off_ms;
} j2_pins_t;

void j2_pins_default(j2_pins_t *p);
/* Every pin is -1 or a J2 probe GPIO, and the I2C and I2S pins are distinct; the radar RX may share one (read on its own). */
bool j2_pins_valid(const j2_pins_t *p);

/* GPIOs the pin probe may drive: the J2 candidates that are not wired to anything on the knob (docs/hardware-expansion.md). */
extern const int J2_PROBE_GPIOS[];
extern const size_t J2_PROBE_N;
bool j2_probe_allowed(int gpio);
esp_err_t j2_probe_drive(int gpio, bool high);
esp_err_t j2_probe_float(int gpio);
/* Input with the internal pull-down: 1 means something outside pulls the pin up (a module's I2C pull-ups). */
int j2_probe_level(int gpio);

/* One sensor bus on I2C_NUM_1. Open, probe and use it from core-1 tasks only (the ISR sits on the opening core; esp-idf#19204). */
esp_err_t j2_i2c_open(int sda, int scl);
void j2_i2c_close(void);
bool j2_i2c_is_open(void);
esp_err_t j2_i2c_probe(uint8_t addr);
esp_err_t j2_i2c_write(uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn);
esp_err_t j2_i2c_read(uint8_t addr, uint32_t hz, uint8_t *r, size_t rn);
esp_err_t j2_i2c_write_read(uint8_t addr, uint32_t hz, const uint8_t *w, size_t wn, uint8_t *r, size_t rn);
/* Fills found[] with every address that ACKs in 0x08-0x77; returns the count. */
size_t j2_i2c_scan(uint8_t *found, size_t cap);

/* Chip drivers on the open bus; each blocks the caller for the conversion time. */
esp_err_t j2_aht_init(void);
esp_err_t j2_aht_read(aht_reading_t *out);
esp_err_t j2_ens160_find(uint8_t *addr);
esp_err_t j2_ens160_start(uint8_t addr);
esp_err_t j2_ens160_read(uint8_t addr, ens160_status_t *st, ens160_data_t *d, bool *fresh);
esp_err_t j2_ens160_comp(uint8_t addr, float temp_c, float rh_pct);
esp_err_t j2_bh1750_find(uint8_t *addr);
esp_err_t j2_bh1750_start(uint8_t addr);
esp_err_t j2_bh1750_read(uint8_t addr, float *lux);
esp_err_t j2_bmp280_find(uint8_t *addr, bmp280_calib_t *c);
esp_err_t j2_bmp280_read(uint8_t addr, const bmp280_calib_t *c, bmp280_reading_t *out);
/* want 0 searches 0x20-0x27 and 0x39-0x3F, skipping 0x23 (BH1750) and the taken addresses. */
esp_err_t j2_pcf_find(uint8_t want, const uint8_t *taken, size_t n_taken, uint8_t *addr);
esp_err_t j2_pcf_read(uint8_t addr, uint8_t *port);

/* Room service: one core-1 task (PSRAM stack) polls the sensors and keeps a snapshot. */
esp_err_t j2_room_start(const j2_pins_t *pins);
void j2_room_stop(void);
bool j2_room_running(void);
void j2_room_get(room_t *out);
/* ROOM_HAS_* bits of the chips the last detection found. */
uint32_t j2_room_found(void);

/* Blocking 16 kHz mono playback on the MAX98357A; the I2S channel and its internal DMA buffers exist only while it plays. */
esp_err_t j2_amp_tone(const j2_pins_t *pins, uint32_t hz, uint32_t ms, uint8_t vol_pct);
#define J2_AMP_RATE 16000

typedef void (*j2_radar_cb_t)(const ld2410_report_t *r, void *arg);
/* Reads LD2410 UART reports on UART_NUM_1 for ms; the driver is installed for the call only. */
esp_err_t j2_radar_listen(int rx_gpio, uint32_t ms, j2_radar_cb_t cb, void *arg, ld2410_rx_t *stats);
