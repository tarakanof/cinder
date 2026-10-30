#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BH1750_ADDR_LOW 0x23
#define BH1750_ADDR_HIGH 0x5C

#define BH1750_POWER_DOWN 0x00
#define BH1750_POWER_ON 0x01
#define BH1750_RESET 0x07
#define BH1750_CONT_H 0x10
#define BH1750_CONT_H2 0x11
#define BH1750_CONT_L 0x13
#define BH1750_ONCE_H 0x20
#define BH1750_ONCE_H2 0x21
#define BH1750_ONCE_L 0x23

#define BH1750_MT_DEFAULT 69
#define BH1750_MT_MIN 31
#define BH1750_MT_MAX 254

bool bh1750_mt_cmd(uint8_t mt, uint8_t out[2]);
/* Worst-case conversion time in ms for a mode opcode at MTreg mt (datasheet max, scaled by mt/69). */
uint32_t bh1750_wait_ms(uint8_t mode, uint8_t mt);
/* Lux from the 2-byte big-endian result; mode is the opcode the reading was taken with. */
float bh1750_lux(const uint8_t raw[2], uint8_t mode, uint8_t mt);
