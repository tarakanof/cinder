#include "sens_ens160.h"

static uint16_t clamp_round(float v, float max)
{
    if (!(v > 0)) return 0;
    if (v > max) v = max;
    return (uint16_t)(v + 0.5f);
}

uint16_t ens160_part_id(const uint8_t raw[2]) { return (uint16_t)(raw[0] | (raw[1] << 8)); }

uint16_t ens160_temp_in(float temp_c)
{
    if (temp_c < -40) temp_c = -40;
    if (temp_c > 85) temp_c = 85;
    return clamp_round((temp_c + 273.15f) * 64.0f, 65535);
}

uint16_t ens160_rh_in(float rh_pct)
{
    if (rh_pct > 100) rh_pct = 100;
    return clamp_round(rh_pct * 512.0f, 65535);
}

void ens160_le16(uint16_t v, uint8_t out[2])
{
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)(v >> 8);
}

ens160_status_t ens160_status(uint8_t reg)
{
    return (ens160_status_t){
        .running = reg & 0x80,
        .error = reg & 0x40,
        .new_data = reg & 0x02,
        .validity = (ens160_validity_t)((reg >> 2) & 0x03),
    };
}

const char *ens160_validity_name(ens160_validity_t v)
{
    switch (v) {
    case ENS160_NORMAL: return "normal";
    case ENS160_WARM_UP: return "warm_up";
    case ENS160_START_UP: return "start_up";
    default: return "invalid";
    }
}

bool ens160_data(const uint8_t raw[ENS160_DATA_LEN], ens160_data_t *out)
{
    uint8_t aqi = raw[0] & 0x07;
    if (aqi < 1 || aqi > 5) return false;
    out->aqi = aqi;
    out->tvoc_ppb = (uint16_t)(raw[1] | (raw[2] << 8));
    out->eco2_ppm = (uint16_t)(raw[3] | (raw[4] << 8));
    return true;
}
