#include "sens_ld2410.h"

#include <string.h>

static const uint8_t HEAD[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t FOOT[4] = {0xF8, 0xF7, 0xF6, 0xF5};
static const uint8_t CMD_HEAD[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FOOT[4] = {0x04, 0x03, 0x02, 0x01};

#define BASIC_LEN 13

void ld2410_rx_init(ld2410_rx_t *rx) { memset(rx, 0, sizeof *rx); }

static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void restart(ld2410_rx_t *rx, uint8_t b)
{
    if (rx->n) rx->dropped++;
    rx->n = 0;
    if (b == HEAD[0]) rx->buf[rx->n++] = b;
}

static bool parse(const uint8_t *p, uint16_t len, ld2410_report_t *out)
{
    if (len < BASIC_LEN || (p[0] != 0x01 && p[0] != 0x02) || p[1] != 0xAA) return false;
    if (p[len - 2] != 0x55 || p[len - 1] != 0x00) return false;
    out->engineering = p[0] == 0x01;
    out->state = (ld2410_target_t)(p[2] & 0x03);
    out->move_cm = u16le(p + 3);
    out->move_energy = p[5];
    out->still_cm = u16le(p + 6);
    out->still_energy = p[8];
    out->detect_cm = u16le(p + 9);
    return true;
}

bool ld2410_rx_byte(ld2410_rx_t *rx, uint8_t b, ld2410_report_t *out)
{
    if (rx->n < 4) {
        if (b == HEAD[rx->n]) {
            rx->buf[rx->n++] = b;
        } else {
            restart(rx, b);
        }
        return false;
    }
    rx->buf[rx->n++] = b;
    if (rx->n < 6) return false;
    uint16_t len = u16le(rx->buf + 4);
    if (len < BASIC_LEN || len > LD2410_PAYLOAD_MAX) {
        restart(rx, b);
        return false;
    }
    uint16_t total = (uint16_t)(6 + len + 4);
    if (rx->n < total) return false;
    bool ok = memcmp(rx->buf + 6 + len, FOOT, 4) == 0 && parse(rx->buf + 6, len, out);
    if (ok) {
        rx->frames++;
        rx->n = 0;
    } else {
        rx->dropped++;
        rx->n = 0;
    }
    return ok;
}

size_t ld2410_cmd(uint16_t cmd, const uint8_t *value, size_t value_len, uint8_t *out, size_t cap)
{
    size_t total = 4 + 2 + 2 + value_len + 4;
    if (total > cap || value_len > 0xFFFF - 2) return 0;
    uint16_t len = (uint16_t)(2 + value_len);
    memcpy(out, CMD_HEAD, 4);
    out[4] = (uint8_t)(len & 0xFF);
    out[5] = (uint8_t)(len >> 8);
    out[6] = (uint8_t)(cmd & 0xFF);
    out[7] = (uint8_t)(cmd >> 8);
    if (value_len) memcpy(out + 8, value, value_len);
    memcpy(out + 8 + value_len, CMD_FOOT, 4);
    return total;
}

const char *ld2410_target_name(ld2410_target_t t)
{
    switch (t) {
    case LD2410_MOVING: return "moving";
    case LD2410_STILL: return "still";
    case LD2410_BOTH: return "both";
    default: return "none";
    }
}
