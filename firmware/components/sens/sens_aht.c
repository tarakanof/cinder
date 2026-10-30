#include "sens_aht.h"

uint8_t aht_crc8(const uint8_t *p, size_t n)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (uint8_t)(crc & 0x80 ? (crc << 1) ^ 0x31 : crc << 1);
    }
    return crc;
}

bool aht_needs_reg_init(uint8_t status) { return (status & AHT_STATUS_READY_MASK) != AHT_STATUS_READY_MASK; }

void aht_reg_init_write(uint8_t reg, const uint8_t read[3], uint8_t out[3])
{
    out[0] = (uint8_t)(0xB0 | reg);
    out[1] = read[1];
    out[2] = read[2];
}

aht_result_t aht_parse(const uint8_t f[AHT_FRAME_LEN], aht_reading_t *out)
{
    if (f[0] & AHT_STATUS_BUSY) return AHT_BUSY;
    if (aht_crc8(f, 6) != f[6]) return AHT_CRC;
    uint32_t rh = ((uint32_t)f[1] << 12) | ((uint32_t)f[2] << 4) | (f[3] >> 4);
    uint32_t t = ((uint32_t)(f[3] & 0x0F) << 16) | ((uint32_t)f[4] << 8) | f[5];
    out->rh_pct = (float)rh / 1048576.0f * 100.0f;
    out->temp_c = (float)t / 1048576.0f * 200.0f - 50.0f;
    return AHT_OK;
}
