#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BMP280_ADDR_LOW 0x76
#define BMP280_ADDR_HIGH 0x77
#define BMP280_CHIP_ID 0x58

#define BMP280_REG_CALIB 0x88
#define BMP280_REG_ID 0xD0
#define BMP280_REG_RESET 0xE0
#define BMP280_REG_STATUS 0xF3
#define BMP280_REG_CTRL_MEAS 0xF4
#define BMP280_REG_CONFIG 0xF5
#define BMP280_REG_DATA 0xF7

#define BMP280_CALIB_LEN 24
#define BMP280_DATA_LEN 6
#define BMP280_SKIPPED 0x80000

typedef struct {
    uint16_t t1;
    int16_t t2, t3;
    uint16_t p1;
    int16_t p2, p3, p4, p5, p6, p7, p8, p9;
} bmp280_calib_t;

typedef struct {
    int32_t temp_centi_c;
    uint32_t press_pa;
} bmp280_reading_t;

void bmp280_calib(const uint8_t raw[BMP280_CALIB_LEN], bmp280_calib_t *out);
/* ctrl_meas for one forced conversion at oversampling codes osrs_t / osrs_p (1 = ×1 ... 5 = ×16). */
uint8_t bmp280_forced(uint8_t osrs_t, uint8_t osrs_p);
/* Datasheet 32-bit integer compensation; false when the frame is a skipped measurement or p is undefined. */
bool bmp280_compensate(const bmp280_calib_t *c, const uint8_t raw[BMP280_DATA_LEN], bmp280_reading_t *out);
