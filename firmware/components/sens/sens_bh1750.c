#include "sens_bh1750.h"

static bool mode_h2(uint8_t mode) { return mode == BH1750_CONT_H2 || mode == BH1750_ONCE_H2; }

static bool mode_l(uint8_t mode) { return mode == BH1750_CONT_L || mode == BH1750_ONCE_L; }

bool bh1750_mt_cmd(uint8_t mt, uint8_t out[2])
{
    if (mt < BH1750_MT_MIN || mt > BH1750_MT_MAX) return false;
    out[0] = (uint8_t)(0x40 | (mt >> 5));
    out[1] = (uint8_t)(0x60 | (mt & 0x1F));
    return true;
}

uint32_t bh1750_wait_ms(uint8_t mode, uint8_t mt)
{
    uint32_t base = mode_l(mode) ? 24 : 180;
    return (base * mt + BH1750_MT_DEFAULT - 1) / BH1750_MT_DEFAULT;
}

float bh1750_lux(const uint8_t raw[2], uint8_t mode, uint8_t mt)
{
    if (!mt) return 0;
    float lux = (float)((raw[0] << 8) | raw[1]) / 1.2f * (float)BH1750_MT_DEFAULT / (float)mt;
    return mode_h2(mode) ? lux / 2 : lux;
}
