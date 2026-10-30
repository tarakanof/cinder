#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AHT_ADDR 0x38
#define AHT_STATUS_BUSY 0x80
#define AHT_STATUS_CAL 0x08
#define AHT_STATUS_READY_MASK 0x18
#define AHT_CMD_TRIGGER 0xAC
#define AHT_CMD_INIT 0xBE
#define AHT_CMD_RESET 0xBA
#define AHT_POWER_ON_MS 100
#define AHT_MEASURE_MS 80
#define AHT_FRAME_LEN 7

typedef enum { AHT_OK, AHT_BUSY, AHT_CRC } aht_result_t;

typedef struct {
    float temp_c;
    float rh_pct;
} aht_reading_t;

uint8_t aht_crc8(const uint8_t *p, size_t n);
/* Aosong V1.0 datasheets: when (status & 0x18) != 0x18, reinitialise registers 0x1B, 0x1C and 0x1E. */
bool aht_needs_reg_init(uint8_t status);
/* The write that follows reading 3 bytes back from reg in that routine. */
void aht_reg_init_write(uint8_t reg, const uint8_t read[3], uint8_t out[3]);
/* f is the 7-byte frame (status, 5 data bytes, CRC) read after a trigger. */
aht_result_t aht_parse(const uint8_t f[AHT_FRAME_LEN], aht_reading_t *out);
