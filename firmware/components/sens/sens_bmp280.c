#include "sens_bmp280.h"

static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int16_t s16le(const uint8_t *p) { return (int16_t)u16le(p); }

void bmp280_calib(const uint8_t raw[BMP280_CALIB_LEN], bmp280_calib_t *out)
{
    out->t1 = u16le(raw + 0);
    out->t2 = s16le(raw + 2);
    out->t3 = s16le(raw + 4);
    out->p1 = u16le(raw + 6);
    out->p2 = s16le(raw + 8);
    out->p3 = s16le(raw + 10);
    out->p4 = s16le(raw + 12);
    out->p5 = s16le(raw + 14);
    out->p6 = s16le(raw + 16);
    out->p7 = s16le(raw + 18);
    out->p8 = s16le(raw + 20);
    out->p9 = s16le(raw + 22);
}

uint8_t bmp280_forced(uint8_t osrs_t, uint8_t osrs_p) { return (uint8_t)(((osrs_t & 7) << 5) | ((osrs_p & 7) << 2) | 0x01); }

static int32_t raw20(const uint8_t *p) { return (int32_t)(((uint32_t)p[0] << 12) | ((uint32_t)p[1] << 4) | (p[2] >> 4)); }

bool bmp280_compensate(const bmp280_calib_t *c, const uint8_t raw[BMP280_DATA_LEN], bmp280_reading_t *out)
{
    int32_t adc_p = raw20(raw), adc_t = raw20(raw + 3);
    if (adc_t == BMP280_SKIPPED || adc_p == BMP280_SKIPPED) return false;
    int32_t var1 = ((((adc_t >> 3) - ((int32_t)c->t1 << 1))) * ((int32_t)c->t2)) >> 11;
    int32_t var2 = (((((adc_t >> 4) - ((int32_t)c->t1)) * ((adc_t >> 4) - ((int32_t)c->t1))) >> 12) * ((int32_t)c->t3)) >> 14;
    int32_t t_fine = var1 + var2;
    out->temp_centi_c = (t_fine * 5 + 128) >> 8;
    var1 = (t_fine >> 1) - (int32_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t)c->p6);
    var2 = var2 + ((var1 * ((int32_t)c->p5)) << 1);
    var2 = (var2 >> 2) + (((int32_t)c->p4) << 16);
    var1 = (((c->p3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((((int32_t)c->p2) * var1) >> 1)) >> 18;
    var1 = ((32768 + var1) * ((int32_t)c->p1)) >> 15;
    if (var1 == 0) return false;
    uint32_t p = ((uint32_t)(((int32_t)1048576) - adc_p) - (uint32_t)(var2 >> 12)) * 3125;
    if (p < 0x80000000u)
        p = (p << 1) / (uint32_t)var1;
    else
        p = (p / (uint32_t)var1) * 2;
    var1 = (((int32_t)c->p9) * ((int32_t)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
    var2 = (((int32_t)(p >> 2)) * ((int32_t)c->p8)) >> 13;
    out->press_pa = (uint32_t)((int32_t)p + ((var1 + var2 + c->p7) >> 4));
    return true;
}
