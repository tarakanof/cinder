#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LD2410_BAUD 256000
#define LD2410_PAYLOAD_MAX 64

#define LD2410_CMD_CONFIG_ON 0x00FF
#define LD2410_CMD_CONFIG_OFF 0x00FE
#define LD2410_CMD_ENGINEERING_ON 0x0062
#define LD2410_CMD_ENGINEERING_OFF 0x0063
#define LD2410_CMD_FW_VERSION 0x00A0

typedef enum { LD2410_NONE = 0, LD2410_MOVING = 1, LD2410_STILL = 2, LD2410_BOTH = 3 } ld2410_target_t;

typedef struct {
    bool engineering;
    ld2410_target_t state;
    uint16_t move_cm;
    uint8_t move_energy;
    uint16_t still_cm;
    uint8_t still_energy;
    uint16_t detect_cm;
} ld2410_report_t;

typedef struct {
    uint8_t buf[4 + 2 + LD2410_PAYLOAD_MAX + 4];
    uint16_t n;
    uint32_t frames;
    uint32_t dropped;
} ld2410_rx_t;

void ld2410_rx_init(ld2410_rx_t *rx);
/* Feed one UART byte; true when it completed a valid report frame, written to out. */
bool ld2410_rx_byte(ld2410_rx_t *rx, uint8_t b, ld2410_report_t *out);
/* Command frame for cmd with an optional value; returns its length, 0 when cap is too small. */
size_t ld2410_cmd(uint16_t cmd, const uint8_t *value, size_t value_len, uint8_t *out, size_t cap);
const char *ld2410_target_name(ld2410_target_t t);
