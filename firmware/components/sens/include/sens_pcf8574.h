#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PCF8574_ADDR_MIN 0x20
#define PCF8574_ADDR_MAX 0x27
#define PCF8574A_ADDR_MIN 0x38
#define PCF8574A_ADDR_MAX 0x3F
#define PCF8574_ALL_INPUT 0xFF
#define PCF8574_HZ 100000

static inline bool pcf8574_addr(uint8_t a) { return (a >= PCF8574_ADDR_MIN && a <= PCF8574_ADDR_MAX) || (a >= PCF8574A_ADDR_MIN && a <= PCF8574A_ADDR_MAX); }

static inline bool pcf8574_bit(uint8_t port, int bit) { return bit >= 0 && bit < 8 && (port >> bit) & 1; }
