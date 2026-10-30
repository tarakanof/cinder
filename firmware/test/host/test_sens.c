#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sens_aht.h"
#include "sens_bh1750.h"
#include "sens_bmp280.h"
#include "sens_ens160.h"
#include "sens_ld2410.h"
#include "sens_pcf8574.h"
#include "sens_room.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))

static void test_bh1750(void)
{
    uint8_t mt[2];
    CHECK(bh1750_mt_cmd(69, mt) && mt[0] == 0x42 && mt[1] == 0x65, "MTreg 69 -> 42 65: %02X %02X", mt[0], mt[1]);
    CHECK(bh1750_mt_cmd(254, mt) && mt[0] == 0x47 && mt[1] == 0x7E, "MTreg 254 -> 47 7E: %02X %02X", mt[0], mt[1]);
    CHECK(!bh1750_mt_cmd(30, mt) && !bh1750_mt_cmd(255, mt), "MTreg outside 31-254 rejected");
    CHECK(bh1750_wait_ms(BH1750_CONT_H, 69) == 180, "H at 69: 180 ms");
    CHECK(bh1750_wait_ms(BH1750_ONCE_H2, 138) == 360, "H2 at 138: 360 ms");
    CHECK(bh1750_wait_ms(BH1750_CONT_L, 69) == 24, "L at 69: 24 ms");
    CHECK(bh1750_wait_ms(BH1750_CONT_H, 31) == 81, "H at 31 rounds up: %u", (unsigned)bh1750_wait_ms(BH1750_CONT_H, 31));
    const uint8_t raw[2] = {0x83, 0x90};
    CHECK(NEAR(bh1750_lux(raw, BH1750_CONT_H, 69), 28067.0 , 1), "datasheet example 0x8390 -> 28067 lx: %.1f", bh1750_lux(raw, BH1750_CONT_H, 69));
    CHECK(NEAR(bh1750_lux(raw, BH1750_CONT_H2, 69), 14033.5, 1), "H2 halves");
    CHECK(NEAR(bh1750_lux(raw, BH1750_CONT_H, 138), 14033.5, 1), "double MTreg halves");
    CHECK(bh1750_lux(raw, BH1750_CONT_H, 0) == 0, "MTreg 0 gives 0, no division by zero");
}

static void test_aht(void)
{
    const uint8_t sens[2] = {0xBE, 0xEF};
    CHECK(aht_crc8(sens, 2) == 0x92, "CRC-8 0x31/0xFF of BEEF is 0x92: %02X", aht_crc8(sens, 2));
    uint8_t f[AHT_FRAME_LEN] = {0x1C, 0x80, 0x00, 0x06, 0x00, 0x00, 0x4E};
    aht_reading_t r;
    CHECK(aht_parse(f, &r) == AHT_OK, "valid frame");
    CHECK(NEAR(r.rh_pct, 50, 0.001) && NEAR(r.temp_c, 25, 0.001), "50 %% / 25 C: %.3f %.3f", r.rh_pct, r.temp_c);
    f[6] ^= 1;
    CHECK(aht_parse(f, &r) == AHT_CRC, "bad CRC");
    f[6] ^= 1;
    f[0] |= AHT_STATUS_BUSY;
    CHECK(aht_parse(f, &r) == AHT_BUSY, "busy wins over CRC");
    uint8_t zero[AHT_FRAME_LEN] = {0x18, 0, 0, 0, 0, 0, 0};
    zero[6] = aht_crc8(zero, 6);
    CHECK(aht_parse(zero, &r) == AHT_OK && NEAR(r.temp_c, -50, 0.001) && r.rh_pct == 0, "raw 0: -50 C, 0 %%");
    CHECK(!aht_needs_reg_init(0x18) && !aht_needs_reg_init(0x1C) && aht_needs_reg_init(0x08) && aht_needs_reg_init(0x10) && aht_needs_reg_init(0x00),
          "0x18 mask");
    const uint8_t back[3] = {0x11, 0x22, 0x33};
    uint8_t w[3];
    aht_reg_init_write(0x1B, back, w);
    CHECK(w[0] == 0xBB && w[1] == 0x22 && w[2] == 0x33, "reg 0x1B write-back: %02X %02X %02X", w[0], w[1], w[2]);
}

static void test_ens160(void)
{
    const uint8_t id[2] = {0x60, 0x01};
    CHECK(ens160_part_id(id) == ENS160_PART_ID, "part id LE");
    CHECK(ens160_temp_in(25) == 0x4A8A, "25 C -> 0x4A8A: %04X", ens160_temp_in(25));
    CHECK(ens160_rh_in(50) == 0x6400, "50 %% -> 0x6400: %04X", ens160_rh_in(50));
    CHECK(ens160_rh_in(150) == 51200 && ens160_rh_in(-5) == 0, "RH clamped to 0-100");
    CHECK(ens160_temp_in(200) == ens160_temp_in(85) && ens160_temp_in(-100) == ens160_temp_in(-40), "T clamped to -40..85");
    CHECK(ens160_rh_in(NAN) == 0, "NaN RH gives 0");
    uint8_t le[2];
    ens160_le16(0x4A8A, le);
    CHECK(le[0] == 0x8A && le[1] == 0x4A, "LSB first");
    ens160_status_t s = ens160_status(0x82);
    CHECK(s.running && !s.error && s.new_data && s.validity == ENS160_NORMAL, "0x82 running, new data, normal");
    s = ens160_status(0x44);
    CHECK(!s.running && s.error && !s.new_data && s.validity == ENS160_WARM_UP, "0x44 error, warm-up");
    CHECK(ens160_status(0x08).validity == ENS160_START_UP && ens160_status(0x0C).validity == ENS160_INVALID, "validity 2, 3");
    CHECK(!strcmp(ens160_validity_name(ENS160_WARM_UP), "warm_up") && !strcmp(ens160_validity_name(ENS160_INVALID), "invalid"), "names");
    const uint8_t raw[ENS160_DATA_LEN] = {0x02, 0x2C, 0x01, 0x58, 0x02};
    ens160_data_t d;
    CHECK(ens160_data(raw, &d) && d.aqi == 2 && d.tvoc_ppb == 300 && d.eco2_ppm == 600, "AQI 2, 300 ppb, 600 ppm");
    const uint8_t bad[ENS160_DATA_LEN] = {0x00, 0, 0, 0, 0};
    CHECK(!ens160_data(bad, &d), "AQI 0 rejected");
    const uint8_t hi[ENS160_DATA_LEN] = {0xFD, 0, 0, 0, 0};
    CHECK(ens160_data(hi, &d) && d.aqi == 5, "only bits 2:0 are the AQI");
}

static void test_bmp280(void)
{
    const uint8_t cal[BMP280_CALIB_LEN] = {0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC, 0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B,
                                           0x27, 0x0B, 0x8C, 0x00, 0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6, 0x70, 0x17};
    bmp280_calib_t c;
    bmp280_calib(cal, &c);
    CHECK(c.t1 == 27504 && c.t2 == 26435 && c.t3 == -1000 && c.p1 == 36477 && c.p2 == -10685 && c.p9 == 6000, "datasheet calibration");
    const uint8_t raw[BMP280_DATA_LEN] = {0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00};
    bmp280_reading_t r;
    CHECK(bmp280_compensate(&c, raw, &r), "compensates");
    CHECK(r.temp_centi_c == 2508, "datasheet 25.08 C: %d", (int)r.temp_centi_c);
    CHECK(r.press_pa == 100656, "datasheet 32-bit 100656 Pa: %u", (unsigned)r.press_pa);
    const uint8_t skipped[BMP280_DATA_LEN] = {0x80, 0x00, 0x00, 0x7E, 0xED, 0x00};
    CHECK(!bmp280_compensate(&c, skipped, &r), "skipped pressure");
    bmp280_calib_t zero = c;
    zero.p1 = 0;
    CHECK(!bmp280_compensate(&zero, raw, &r), "var1 0 is no reading");
    CHECK(bmp280_forced(1, 3) == 0x2D, "osrs_t x1, osrs_p x4, forced: %02X", bmp280_forced(1, 3));
}

static const uint8_t FRAME[] = {0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02, 0xAA, 0x03, 0x78, 0x00, 0x2D,
                                0x50, 0x00, 0x3C, 0x96, 0x00, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};

static int feed(ld2410_rx_t *rx, const uint8_t *p, size_t n, ld2410_report_t *last)
{
    int got = 0;
    for (size_t i = 0; i < n; i++) got += ld2410_rx_byte(rx, p[i], last);
    return got;
}

static void test_ld2410(void)
{
    ld2410_rx_t rx;
    ld2410_report_t r;
    ld2410_rx_init(&rx);
    CHECK(feed(&rx, FRAME, sizeof FRAME, &r) == 1, "one basic frame");
    CHECK(!r.engineering && r.state == LD2410_BOTH && r.move_cm == 120 && r.move_energy == 45 && r.still_cm == 80 && r.still_energy == 60 &&
              r.detect_cm == 150, "fields");
    const uint8_t junk[] = {0x00, 0xF4, 0xF4, 0xF3, 0x12, 0xF4, 0xF3, 0xF2, 0x00, 0xFD, 0xFC};
    ld2410_rx_init(&rx);
    CHECK(feed(&rx, junk, sizeof junk, &r) == 0 && feed(&rx, FRAME, sizeof FRAME, &r) == 1, "resyncs after a broken header");
    CHECK(feed(&rx, FRAME, 10, &r) == 0 && feed(&rx, FRAME + 10, sizeof FRAME - 10, &r) == 1, "split across reads");
    uint8_t bad[sizeof FRAME];
    memcpy(bad, FRAME, sizeof FRAME);
    bad[sizeof bad - 1] = 0x00;
    uint32_t dropped = rx.dropped;
    CHECK(feed(&rx, bad, sizeof bad, &r) == 0 && rx.dropped == dropped + 1, "bad footer dropped");
    CHECK(feed(&rx, FRAME, sizeof FRAME, &r) == 1, "next frame fine");
    memcpy(bad, FRAME, sizeof FRAME);
    bad[17] = 0x54;
    CHECK(feed(&rx, bad, sizeof bad, &r) == 0, "bad intra-frame tail");
    const uint8_t huge[] = {0xF4, 0xF3, 0xF2, 0xF1, 0xFF, 0x00};
    CHECK(feed(&rx, huge, sizeof huge, &r) == 0 && rx.n == 0, "oversized length restarts");
    CHECK(feed(&rx, FRAME, sizeof FRAME, &r) == 1, "and recovers");
    uint8_t eng[4 + 2 + 35 + 4] = {0xF4, 0xF3, 0xF2, 0xF1, 35, 0x00, 0x01, 0xAA, 0x01, 0x40, 0x00, 0x30, 0x00, 0x00, 0x00, 0x40, 0x00};
    eng[6 + 33] = 0x55;
    eng[6 + 34] = 0x00;
    memcpy(eng + 6 + 35, (const uint8_t[]){0xF8, 0xF7, 0xF6, 0xF5}, 4);
    CHECK(feed(&rx, eng, sizeof eng, &r) == 1 && r.engineering && r.state == LD2410_MOVING && r.move_cm == 64 && r.detect_cm == 64, "engineering frame");
    const uint8_t ack[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    CHECK(feed(&rx, ack, sizeof ack, &r) == 0 && feed(&rx, FRAME, sizeof FRAME, &r) == 1, "command ACKs are skipped");
    uint8_t cmd[16];
    const uint8_t one[2] = {0x01, 0x00};
    size_t n = ld2410_cmd(LD2410_CMD_CONFIG_ON, one, 2, cmd, sizeof cmd);
    const uint8_t want[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    CHECK(n == sizeof want && !memcmp(cmd, want, n), "enable config frame");
    n = ld2410_cmd(LD2410_CMD_CONFIG_OFF, NULL, 0, cmd, sizeof cmd);
    CHECK(n == 12 && cmd[4] == 2 && cmd[6] == 0xFE && cmd[7] == 0x00, "end config frame");
    CHECK(ld2410_cmd(LD2410_CMD_CONFIG_ON, one, 2, cmd, 13) == 0, "too small a buffer");
    CHECK(!strcmp(ld2410_target_name(LD2410_STILL), "still") && !strcmp(ld2410_target_name((ld2410_target_t)7), "none"), "names");
}

static void test_pcf8574(void)
{
    CHECK(pcf8574_addr(0x20) && pcf8574_addr(0x27) && pcf8574_addr(0x38) && pcf8574_addr(0x3F), "both families");
    CHECK(!pcf8574_addr(0x28) && !pcf8574_addr(0x37) && !pcf8574_addr(0x40), "outside");
    CHECK(pcf8574_bit(0x01, 0) && !pcf8574_bit(0xFE, 0) && pcf8574_bit(0x80, 7) && !pcf8574_bit(0xFF, 8) && !pcf8574_bit(0xFF, -1), "bits");
}

static void test_presence(void)
{
    room_presence_t p;
    room_presence_init(&p, 0, 5000, 0);
    CHECK(!p.present, "starts absent");
    CHECK(room_presence_feed(&p, true, 100) && p.present, "on at once with on_ms 0");
    CHECK(!room_presence_feed(&p, false, 200) && p.present, "a drop is held");
    CHECK(!room_presence_feed(&p, true, 3000) && !room_presence_feed(&p, false, 3100), "a blip resets the hold");
    CHECK(!room_presence_feed(&p, false, 8000) && p.present, "4.9 s after the last drop: still present");
    CHECK(room_presence_feed(&p, false, 8100) && !p.present, "5 s low: absent");
    room_presence_init(&p, 1000, 0, 0);
    CHECK(!room_presence_feed(&p, true, 10) && !room_presence_feed(&p, true, 900), "on_ms debounces");
    CHECK(room_presence_feed(&p, true, 1010) && p.present, "on after 1 s high");
    CHECK(room_presence_feed(&p, false, 1020) && !p.present, "off at once with off_ms 0");
}

static void test_comp(void)
{
    room_comp_t c = {0};
    CHECK(room_comp_due(&c, 22, 40, 0), "first reading");
    room_comp_sent(&c, 22, 40, 0);
    CHECK(!room_comp_due(&c, 22.4f, 41.9f, 1000), "small change");
    CHECK(room_comp_due(&c, 22.5f, 40, 1000) && room_comp_due(&c, 22, 37.9f, 1000), "step reached");
    CHECK(room_comp_due(&c, 22, 40, ROOM_COMP_MAX_AGE_MS), "refresh after the max age");
}

int main(void)
{
    test_bh1750();
    test_aht();
    test_ens160();
    test_bmp280();
    test_ld2410();
    test_pcf8574();
    test_presence();
    test_comp();
    if (failures) {
        printf("test_sens: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_sens: ok (bh1750, aht, ens160, bmp280, ld2410, pcf8574, presence, compensation)\n");
    return 0;
}
