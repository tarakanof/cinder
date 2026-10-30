#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ENS160_ADDR_LOW 0x52
#define ENS160_ADDR_HIGH 0x53
#define ENS160_PART_ID 0x0160

#define ENS160_REG_PART_ID 0x00
#define ENS160_REG_OPMODE 0x10
#define ENS160_REG_CONFIG 0x11
#define ENS160_REG_COMMAND 0x12
#define ENS160_REG_TEMP_IN 0x13
#define ENS160_REG_RH_IN 0x15
#define ENS160_REG_STATUS 0x20
#define ENS160_REG_DATA_AQI 0x21
#define ENS160_REG_DATA_T 0x30

#define ENS160_OP_DEEP_SLEEP 0x00
#define ENS160_OP_IDLE 0x01
#define ENS160_OP_STANDARD 0x02
#define ENS160_OP_RESET 0xF0

#define ENS160_DATA_LEN 5

typedef enum { ENS160_NORMAL = 0, ENS160_WARM_UP = 1, ENS160_START_UP = 2, ENS160_INVALID = 3 } ens160_validity_t;

typedef struct {
    bool running;
    bool error;
    bool new_data;
    ens160_validity_t validity;
} ens160_status_t;

typedef struct {
    uint8_t aqi;
    uint16_t tvoc_ppb;
    uint16_t eco2_ppm;
} ens160_data_t;

uint16_t ens160_part_id(const uint8_t raw[2]);
/* TEMP_IN / RH_IN register values, little-endian on the wire: (°C + 273.15) × 64 and %RH × 512, clamped. */
uint16_t ens160_temp_in(float temp_c);
uint16_t ens160_rh_in(float rh_pct);
void ens160_le16(uint16_t v, uint8_t out[2]);
ens160_status_t ens160_status(uint8_t reg);
const char *ens160_validity_name(ens160_validity_t v);
/* raw is the burst read from DATA_AQI (0x21): AQI, TVOC LE, eCO2 LE. False on an AQI outside 1-5. */
bool ens160_data(const uint8_t raw[ENS160_DATA_LEN], ens160_data_t *out);
