#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "room_face.h"
#include "room_report.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NOW 1000000
#define C ROOM_FACE_CLIMATE
#define A ROOM_FACE_AIR
#define L ROOM_FACE_LIGHT
#define ALL (ROOM_HAS_CLIMATE | ROOM_HAS_AIR | ROOM_HAS_LIGHT | ROOM_HAS_PRESENCE | ROOM_HAS_PRESSURE)

static room_t snap(void)
{
    return (room_t){.has = ALL, .temp_c = 22.4f, .rh_pct = 43.6f, .climate_ms = NOW - 5000,
                    .aqi = 2, .tvoc_ppb = 60, .eco2_ppm = 612, .air_validity = ENS160_NORMAL, .air_ms = NOW - 1000,
                    .lux = 312.4f, .light_ms = NOW, .present = true, .presence_ms = NOW};
}

static void test_list(void)
{
    CHECK(room_faces_mask(0) == 0 && room_faces_n(0) == 0, "no chip: no face");
    CHECK(room_faces_mask(ROOM_HAS_PRESENCE | ROOM_HAS_PRESSURE) == 0, "the radar and the BMP280 alone give no face");
    CHECK(room_faces_mask(ROOM_HAS_CLIMATE) == 1u << C, "AHT2x: Climate");
    CHECK(room_faces_mask(ROOM_HAS_AIR | ROOM_HAS_PRESENCE) == 1u << A, "ENS160: Air");
    CHECK(room_faces_mask(ROOM_HAS_LIGHT) == 1u << L, "BH1750: Light");
    unsigned all = room_faces_mask(ALL), cl = room_faces_mask(ROOM_HAS_CLIMATE | ROOM_HAS_LIGHT);
    CHECK(all == 7 && room_faces_n(all) == 3 && room_faces_n(cl) == 2, "one face per chip");
    CHECK(room_face_pos(all, C) == 0 && room_face_pos(all, A) == 1 && room_face_pos(all, L) == 2, "turn order: Climate, Air, Light");
    CHECK(room_face_pos(cl, L) == 1 && room_face_pos(cl, A) == -1 && room_face_pos(cl, 7) == -1 && room_face_pos(cl, -1) == -1,
          "the dot follows the faces found");

    CHECK(room_face_step(all, C, 1) == A && room_face_step(all, A, 1) == L && room_face_step(all, L, 1) == C, "next wraps");
    CHECK(room_face_step(all, C, -1) == L && room_face_step(all, L, -2) == C, "previous wraps");
    CHECK(room_face_step(all, A, 3) == A && room_face_step(all, A, 1000000) == L && room_face_step(all, A, -1000000) == C,
          "several detents, a huge count");
    CHECK(room_face_step(cl, C, 1) == L && room_face_step(cl, L, 1) == C, "a missing chip is skipped");
    CHECK(room_face_step(cl, A, 1) == C && room_face_step(cl, A, -1) == L && room_face_step(cl, A, 0) == C,
          "a face outside the list enters at the first going forward, the last going back");
    CHECK(room_face_step(0, C, 1) == -1, "no face: none");
}

static void test_auto(void)
{
    unsigned all = room_faces_mask(ALL);
    room_t r = snap();
    for (int aqi = 1; aqi <= 5; aqi++) {
        r.aqi = (uint8_t)aqi;
        CHECK(room_air_poor(&r, NOW, false) == (aqi >= 4), "opening: poor from AQI 4 (aqi %d)", aqi);
        CHECK(room_air_poor(&r, NOW, true) == (aqi >= 3), "shown: kept until it falls to 2 (aqi %d)", aqi);
    }
    r.aqi = 5;
    r.air_validity = ENS160_WARM_UP;
    CHECK(!room_air_poor(&r, NOW, true), "warming up: not poor");
    r = snap();
    r.aqi = 5;
    r.air_ms = NOW - ROOM_FRESH_MS;
    CHECK(!room_air_poor(&r, NOW, true), "a 60 s old reading: not poor");
    r.air_ms = 0;
    CHECK(!room_air_poor(&r, NOW, false), "never read: not poor");
    r = snap();
    r.aqi = 0;
    CHECK(!room_air_poor(&r, NOW, true), "AQI outside 1-5: not poor");
    r.aqi = 5;
    r.has &= ~ROOM_HAS_AIR;
    CHECK(!room_air_poor(&r, NOW, false), "no ENS160: not poor");

    CHECK(room_face_auto(all, true) == A && room_face_auto(all, false) == C, "Air while poor, else Climate");
    CHECK(room_face_auto(room_faces_mask(ROOM_HAS_LIGHT | ROOM_HAS_CLIMATE), true) == C, "poor without an Air face: Climate");
    CHECK(room_face_auto(room_faces_mask(ROOM_HAS_AIR | ROOM_HAS_LIGHT), false) == A, "no Climate: the first face");
    CHECK(room_face_auto(room_faces_mask(ROOM_HAS_LIGHT), true) == L, "Light alone");
    CHECK(room_face_auto(0, true) == -1, "no face");

    room_faces_t f;
    room_faces_open(&f);
    CHECK(f.pick == ROOM_FACE_AUTO && f.face == -1, "open: auto, nothing resolved");
    r = snap();
    r.aqi = 3;
    CHECK(room_faces_resolve(&f, all, &r, NOW) == C, "AQI 3 on open: Climate");
    r.aqi = 4;
    CHECK(room_faces_resolve(&f, all, &r, NOW) == A, "AQI 4: Air");
    r.aqi = 3;
    CHECK(room_faces_resolve(&f, all, &r, NOW) == A, "back to 3: Air is kept");
    r.aqi = 2;
    CHECK(room_faces_resolve(&f, all, &r, NOW) == C && f.face == C, "falls to 2: Climate");
    r.aqi = 3;
    CHECK(room_faces_resolve(&f, all, &r, NOW) == C, "3 again: Climate stays");
    r.aqi = 5;
    room_faces_open(&f);
    CHECK(room_faces_resolve(&f, all, &r, NOW) == A, "open while 5: Air");
    room_faces_turn(&f, all, 1);
    CHECK(f.pick == L && room_faces_resolve(&f, all, &r, NOW) == L, "a turn picks Light, whatever the air");
    room_faces_turn(&f, all, 1);
    CHECK(f.pick == C, "a turn steps from the pick");
    r.aqi = 2;
    room_faces_turn(&f, all, -2);
    CHECK(f.pick == A && room_faces_resolve(&f, all, &r, NOW) == A, "picked Air on clean air stays Air");
    room_faces_open(&f);
    CHECK(room_faces_resolve(&f, all, &r, NOW) == C, "the next open is auto again");
    room_faces_turn(&f, all, 2);
    CHECK(f.pick == L, "two detents from the face resolved last frame");
    room_faces_open(&f);
    room_faces_turn(&f, all, 1);
    CHECK(f.pick == A, "a turn before any frame steps from the first face");
    room_faces_open(&f);
    room_faces_turn(&f, 0, 1);
    CHECK(f.pick == ROOM_FACE_AUTO && room_faces_resolve(&f, 0, &r, NOW) == -1, "no face: no pick, nothing resolves");
    unsigned light = room_faces_mask(ROOM_HAS_LIGHT);
    room_faces_open(&f);
    CHECK(room_faces_resolve(&f, light, &r, NOW) == L, "Light alone");
    room_faces_turn(&f, light, 1);
    CHECK(f.pick == L, "one face: a turn stays on it");
    room_faces_open(&f);
    f.pick = A;
    CHECK(room_faces_resolve(&f, light, &r, NOW) == L, "a pick outside the list: the auto face");
}

static void text(const room_view_t *v, int value, const char *want)
{
    char buf[16];
    room_value_text(v, value, buf, sizeof buf);
    CHECK(strcmp(buf, want) == 0, "value %d: '%s', want '%s'", value, buf, want);
}

static void test_views(void)
{
    room_t r = snap();
    room_view_t v;
    room_view(&r, NOW, C, 128, &v);
    CHECK(v.have && v.value == 22 && strcmp(v.line2, "44 %") == 0 && v.line2_rgb == ROOM_TEXT_RGB && !v.line3[0],
          "Climate: whole degrees, humidity: %d '%s'", v.value, v.line2);
    CHECK(fabsf(v.frac - 6.4f / 12) < 1e-5f, "Climate marker from the reading on 16-28°: %f", v.frac);
    text(&v, 22, "22\xC2\xB0");
    text(&v, -5, "\xE2\x88\x92" "5\xC2\xB0");
    CHECK(room_value_rgb(&v, 22) == wx_temp_rgb(22) && room_value_rgb(&v, 0) == wx_temp_rgb(0), "Climate: the temperature scale");
    r.temp_c = 99.6f;
    r.rh_pct = 120;
    room_view(&r, NOW, C, 128, &v);
    CHECK(v.value == 85 && v.frac == 1 && strcmp(v.line2, "100 %") == 0, "Climate clamps to the AHT range");
    r.temp_c = -60;
    r.rh_pct = -3;
    room_view(&r, NOW, C, 128, &v);
    CHECK(v.value == -40 && v.frac == 0 && strcmp(v.line2, "0 %") == 0, "and below it");
    r.temp_c = NAN;
    room_view(&r, NOW, C, 128, &v);
    CHECK(!v.have && !v.line2[0], "not a number: no reading");
    r = snap();
    r.climate_ms = NOW - ROOM_FRESH_MS;
    room_view(&r, NOW, C, 128, &v);
    CHECK(!v.have && !v.line2[0], "60 s old: no reading");
    text(&v, 0, "\xE2\x88\x92\xE2\x88\x92\xC2\xB0");
    CHECK(room_value_rgb(&v, 0) == ROOM_GREY_RGB, "no reading: grey");

    r = snap();
    room_view(&r, NOW, A, 128, &v);
    CHECK(v.have && !v.warm && v.value == 612 && v.aqi == 2 && strcmp(v.line2, "ppm eCO2") == 0 && v.line2_rgb == ROOM_UNIT_RGB &&
              strcmp(v.line3, "Good air") == 0 && v.line3_rgb == 0x50CCAA,
          "Air: eCO2, its unit and the band word in the band colour");
    CHECK(fabsf(v.frac - 212.0f / 1600) < 1e-5f, "Air marker on 400-2000 ppm: %f", v.frac);
    CHECK(room_value_rgb(&v, 0) == 0x50CCAA && room_value_rgb(&v, 612) == 0x50CCAA, "the value in the AQI band colour, counting too");
    text(&v, 612, "612");
    static const char *const WORDS[] = {"Excellent air", "Good air", "Moderate air", "Poor air", "Unhealthy air"};
    static const uint32_t BANDS[] = {0x4FD6CC, 0x50CCAA, 0xF0E641, 0xFF5050, 0xD03060};
    for (int aqi = 1; aqi <= 5; aqi++)
        CHECK(strcmp(room_air_word(aqi), WORDS[aqi - 1]) == 0 && room_air_rgb(aqi) == BANDS[aqi - 1], "band %d", aqi);
    CHECK(!room_air_word(0)[0] && !room_air_word(6)[0] && room_air_rgb(6) == ROOM_GREY_RGB, "outside 1-5");
    r.eco2_ppm = 65000;
    room_view(&r, NOW, A, 128, &v);
    CHECK(v.value == ROOM_VALUE_MAX && v.frac == 1, "eCO2 clamps to what fits: %d", v.value);
    r.eco2_ppm = 380;
    room_view(&r, NOW, A, 128, &v);
    CHECK(v.value == 380 && v.frac == 0, "below the gauge: the marker at its start");
    static const ens160_validity_t WARM[] = {ENS160_WARM_UP, ENS160_START_UP, ENS160_INVALID};
    for (int i = 0; i < 3; i++) {
        r = snap();
        r.air_validity = WARM[i];
        room_view(&r, NOW, A, 128, &v);
        CHECK(!v.have && v.warm && strcmp(v.line2, "warming up") == 0 && !v.line3[0], "not normal (%d): warming up, no word", (int)WARM[i]);
        text(&v, 0, "\xE2\x88\x92\xE2\x88\x92");
        CHECK(room_value_rgb(&v, 0) == ROOM_GREY_RGB, "warming up: grey");
    }
    r = snap();
    r.air_ms = NOW - ROOM_FRESH_MS;
    r.air_validity = ENS160_WARM_UP;
    room_view(&r, NOW, A, 128, &v);
    CHECK(!v.have && !v.warm && strcmp(v.line2, "ppm eCO2") == 0, "an old reading: no reading, not warming up");
    r = snap();
    r.aqi = 0;
    room_view(&r, NOW, A, 128, &v);
    CHECK(!v.have && !v.warm, "a normal reading without its AQI: no reading");

    r = snap();
    room_view(&r, NOW, L, 128, &v);
    CHECK(v.have && v.value == 312 && strcmp(v.line2, "lux") == 0 && strcmp(v.line3, "Panel 50 %") == 0 && v.line3_rgb == ROOM_TEXT_RGB,
          "Light: lux and the panel level: %d '%s'", v.value, v.line3);
    CHECK(fabsf(v.frac - logf(312.4f) / logf(2000)) < 1e-5f, "Light marker on the log gauge");
    room_view(&r, NOW, L, -1, &v);
    CHECK(!v.line3[0], "no panel level yet: no Panel line");
    room_view(&r, NOW, L, 255, &v);
    CHECK(strcmp(v.line3, "Panel 100 %") == 0, "full panel");
    r.lux = 125000;
    room_view(&r, NOW, L, 0, &v);
    CHECK(v.value == ROOM_VALUE_MAX && v.frac == 1 && strcmp(v.line3, "Panel 0 %") == 0, "lux clamps to what fits");
    r.lux = 0.4f;
    room_view(&r, NOW, L, 0, &v);
    CHECK(v.have && v.value == 0 && v.frac == 0, "dark");
    r.lux = -1;
    room_view(&r, NOW, L, 0, &v);
    CHECK(!v.have && strcmp(v.line2, "lux") == 0 && v.line3[0], "a bad reading: none, the Panel line stays");
    r = snap();
    r.has = ROOM_HAS_CLIMATE;
    room_view(&r, NOW, L, 0, &v);
    CHECK(!v.have, "a reading without its chip: none");
    CHECK(room_value_rgb(&(room_view_t){.face = L, .have = true}, 2000) == 0xF4F0C0 &&
              room_value_rgb(&(room_view_t){.face = L, .have = true}, 1) == 0x3A3F66,
          "Light colours along the gauge");

    CHECK(room_panel_pct(-1) == 0 && room_panel_pct(0) == 0 && room_panel_pct(1) == 0 && room_panel_pct(2) == 1 &&
              room_panel_pct(128) == 50 && room_panel_pct(254) == 100 && room_panel_pct(300) == 100,
          "panel percentage, rounded");
    CHECK(room_lux_frac(0) == 0 && room_lux_frac(1) == 0 && room_lux_frac(2000) == 1 && room_lux_frac(1e9f) == 1 && room_lux_frac(NAN) == 0,
          "log gauge ends");
}

static void test_ring(void)
{
    uint16_t lut[WX_RING_LUT];
    room_ring_lut(C, false, lut);
    CHECK(lut[0] == wx_rgb565(wx_temp_rgb(16)) && lut[WX_RING_LUT - 1] == wx_rgb565(wx_temp_rgb(28)), "Climate: 16-28° on the temperature scale");
    uint16_t want[WX_RING_LUT];
    wx_ring_t *ring = calloc(1, sizeof *ring);
    for (int i = 0; i < WX_RING_LUT; i++) want[i] = wx_rgb565(wx_temp_rgb(16 + 12.0f * i / (WX_RING_LUT - 1)));
    CHECK(memcmp(lut, want, sizeof lut) == 0, "the weather gauge's own colours for that range");
    room_ring_lut(A, false, lut);
    CHECK(lut[0] == wx_rgb565(0x4FD6CC) && lut[WX_RING_LUT - 1] == wx_rgb565(0xD03060), "Air: 400 to 2000 ppm");
    CHECK(room_ring_rgb(A, 200.0f / 1600) == 0x50CCAA && room_ring_rgb(A, 400.0f / 1600) == 0xF0E641 && room_ring_rgb(A, 800.0f / 1600) == 0xFF5050,
          "Air band stops at 600, 800, 1200 ppm");
    room_ring_lut(L, false, lut);
    CHECK(lut[0] == wx_rgb565(0x3A3F66) && lut[WX_RING_LUT / 2] == wx_rgb565(0xB8A868) && lut[WX_RING_LUT - 1] == wx_rgb565(0xF4F0C0),
          "Light: dusk to daylight");
    for (int f = 0; f < ROOM_FACE_N; f++) {
        room_ring_lut(f, true, lut);
        bool grey = true;
        for (int i = 0; i < WX_RING_LUT; i++) grey &= lut[i] == wx_rgb565(ROOM_TRACK_RGB);
        CHECK(grey, "face %d without a reading: the weather's grey track", f);
    }

    room_ring_lut(C, false, lut);
    wx_paint_ring_lut(ring, lut);
    wx_ring_t *ref = calloc(1, sizeof *ref);
    uint16_t scratch[WX_RING_LUT];
    wx_paint_ring(ref, ROOM_TEMP_LO, ROOM_TEMP_HI, false, scratch);
    CHECK(ring->nrun == ref->nrun && ring->npx == ref->npx && memcmp(ring->run, ref->run, sizeof ring->run[0] * (size_t)ring->nrun) == 0 &&
              memcmp(ring->px, ref->px, sizeof ring->px[0] * (size_t)ring->npx) == 0,
          "wx_paint_ring_lut is the weather rasteriser: the Climate ring equals wx_paint_ring 16..28");
    CHECK(ring->npx > 15000 && ring->nrun > 400, "the ring has its annulus: %d px, %d runs", ring->npx, ring->nrun);
    free(ring);
    free(ref);
}

int main(void)
{
    test_list();
    test_auto();
    test_views();
    test_ring();
    if (failures) {
        printf("room_face: %d failure(s)\n", failures);
        return 1;
    }
    printf("room_face: all tests passed\n");
    return 0;
}
