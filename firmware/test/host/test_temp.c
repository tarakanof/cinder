#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_paint.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static wx_obs_t obs(const char *cond, const char *code, float t, int night)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = t, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .has_night = night >= 0, .night = night == 1};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "%s", cond);
    snprintf(o.code, sizeof o.code, "%s", code);
    return o;
}

static void test_scale(void)
{
    static const struct {
        float t;
        uint32_t rgb;
    } C[] = {
        {-30, 0xB070E0}, {-20, 0xB070E0}, {0, 0x66C2EC}, {8, 0x4AC5B9}, {12, 0x8ACF7A}, {15, 0xE8D44A},
        {25, 0xF08A3A},  {35, 0xFF5050},  {40, 0xE0306A}, {60, 0xE0306A}, {31, 0xF96747}, {24, 0xEF913C},
        {34, 0xFE564E},  {9, 0x4CCAA3},   {6, 0x4EC6C6},  {-6, 0x4C84F0}, {-9, 0x5E72EC}, {-2, 0x5CB0F0},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++)
        CHECK(wx_temp_rgb(C[i].t) == C[i].rgb, "scale %.0f: %06X want %06X", C[i].t, (unsigned)wx_temp_rgb(C[i].t),
              (unsigned)C[i].rgb);
    CHECK(wx_temp_rgb(NAN) == 0xB070E0, "NaN clamps low");
}

static void test_uv(void)
{
    CHECK(!wx_uv_advice(0) && !wx_uv_advice(2), "no advice below 3");
    CHECK(!strcmp(wx_uv_advice(3), "Sunglasses on") && wx_uv_rgb(5) == 0xF0E641, "3-5");
    CHECK(!strcmp(wx_uv_advice(6), "Use sunscreen") && wx_uv_rgb(7) == 0xF08A3A, "6-7");
    CHECK(!strcmp(wx_uv_advice(8), "Sunscreen & shade") && wx_uv_rgb(10) == 0xFF5050, "8-10");
    CHECK(!strcmp(wx_uv_advice(11), "Avoid the sun") && wx_uv_rgb(14) == 0xB060E0, "11+");
}

static void test_face_from(void)
{
    wx_temp_t t;
    wx_temp_from(NULL, &t);
    CHECK(!t.have && t.hero == WX_HERO_NONE && !t.has_range && t.lo == -20 && t.hi == 40, "no data");
    wx_obs_t o = obs("clear", "0", 31.2f, 0);
    o.has_temp = false;
    wx_temp_from(&o, &t);
    CHECK(!t.have, "no temperature");

    o = obs("clear", "0", 31.2f, 0);
    o.has_today = true;
    o.min_c = 24;
    o.max_c = 34;
    o.has_cloud = true;
    o.cloud = 5;
    o.has_uv = true;
    o.uv = 8;
    wx_temp_from(&o, &t);
    CHECK(t.have && t.value == 31 && t.has_range && t.lo == 24 && t.hi == 34 && fabsf(t.frac - 0.7f) < 1e-5f, "AT4 range");
    CHECK(t.hero == WX_HERO_SUN && t.cloud_band == 0 && !t.stars && !t.night, "AT4 sun");
    CHECK(!strcmp(t.sub, "Sunscreen & shade") && t.sub_rgb == 0xFF5050, "AT4 advice '%s'", t.sub);
    o.uv = 2;
    wx_temp_from(&o, &t);
    CHECK(!strcmp(t.sub, "Sunny") && t.sub_rgb == 0xBDBDBD, "UV 2: the sky word '%s'", t.sub);

    o = obs("clear", "0", 9.4f, 1);
    o.has_today = true;
    o.min_c = 6;
    o.max_c = 15;
    o.has_cloud = true;
    o.cloud = 0;
    o.has_uv = true;
    o.uv = 5;
    o.has_moon = true;
    o.moon_phase = 80;
    o.moon_illum = 30;
    o.moon_up = 1;
    wx_temp_from(&o, &t);
    CHECK(t.night && t.hero == WX_HERO_MOON && t.stars && t.moon_phase == 80 && t.moon_illum == 30, "AT5 moon");
    CHECK(!strcmp(t.sub, "Clear"), "no UV advice at night: '%s'", t.sub);
    o.moon_up = 0;
    wx_temp_from(&o, &t);
    CHECK(t.hero == WX_HERO_NONE && t.stars, "moon down: stars only");
    o.moon_up = -1;
    wx_temp_from(&o, &t);
    CHECK(t.hero == WX_HERO_NONE && t.stars, "moon.up unknown: no moon");
    o.moon_up = 2;
    wx_temp_from(&o, &t);
    CHECK(t.hero == WX_HERO_NONE, "moon.up invalid: no moon");
    o.moon_up = 1;
    o.has_moon = false;
    wx_temp_from(&o, &t);
    CHECK(t.hero == WX_HERO_NONE && t.stars, "moon missing: no moon");
    o.has_moon = true;
    o.cloud = 80;
    wx_temp_from(&o, &t);
    CHECK(t.hero == WX_HERO_MOON_SMALL && !t.stars && t.cloud_band == 3 && !strcmp(t.sub, "Cloudy"), "AT6 small moon");
    o.cloud = 95;
    wx_temp_from(&o, &t);
    CHECK(t.hero == WX_HERO_NONE && t.cloud_band == 4, "overcast night: no moon");

    o = obs("clouds", "2", -0.5f, -1);
    wx_temp_from(&o, &t);
    CHECK(t.have && t.value == -1 && !t.has_range && t.lo == WX_SCALE_LO && t.hi == WX_SCALE_HI, "legacy: fixed scale");
    CHECK(fabsf(t.frac - 19.0f / 60.0f) < 1e-5f, "legacy frac %.3f", t.frac);
    CHECK(!t.night && t.cloud_band == 2 && t.hero == WX_HERO_NONE && !strcmp(t.sub, "Partly cloudy"), "legacy partly");
    o = obs("clear", "0", 20, 1);
    wx_temp_from(&o, &t);
    CHECK(t.night && t.cloud_band == 0 && t.hero == WX_HERO_NONE && t.stars && !strcmp(t.sub, "Clear"), "legacy night: no moon data");
    o = obs("rain", "65", 20, 0);
    wx_temp_from(&o, &t);
    CHECK(t.cloud_band == 4 && t.hero == WX_HERO_NONE && !strcmp(t.sub, "Heavy rain"), "rain '%s'", t.sub);
    o = obs("snow", "71", -3, 0);
    wx_temp_from(&o, &t);
    CHECK(!strcmp(t.sub, "Light snow") && t.value == -3, "snow");
    o = obs("storm", "95", 20, 0);
    o.has_uv = true;
    o.uv = 9;
    wx_temp_from(&o, &t);
    CHECK(!strcmp(t.sub, "Sunscreen & shade"), "UV advice wins by day");

    o = obs("clear", "0", 50, 0);
    o.has_today = true;
    o.min_c = 10;
    o.max_c = 20;
    wx_temp_from(&o, &t);
    CHECK(t.lo == 10 && t.hi == 50 && t.frac == 1, "range widened to the value");
    o.temp_c = 12;
    o.min_c = o.max_c = 12;
    wx_temp_from(&o, &t);
    CHECK(t.lo == 11 && t.hi == 13 && fabsf(t.frac - 0.5f) < 1e-6f, "flat day: 1 degree each side");
    o.temp_c = 31;
    o.has_today = false;
    wx_temp_from(&o, &t);
    o.stale = true;
    wx_temp_t s;
    wx_temp_from(&o, &s);
    CHECK(!t.still && s.still, "stale greys the value");
}

static void test_same_bake(void)
{
    wx_obs_t o = obs("clouds", "2", 12, 0);
    o.has_today = true;
    o.min_c = 8;
    o.max_c = 14;
    o.has_cloud = true;
    o.cloud = 55;
    wx_temp_t a, b;
    wx_temp_from(&o, &a);
    o.temp_c = 13;
    o.cloud = 60;
    o.has_uv = true;
    o.uv = 6;
    wx_temp_from(&o, &b);
    CHECK(wx_temp_same_bake(&a, &b), "value, sub and cloud inside a band: no bake");
    o.cloud = 75;
    wx_temp_from(&o, &b);
    CHECK(!wx_temp_same_bake(&a, &b), "cloud band");
    o.cloud = 55;
    o.max_c = 15;
    wx_temp_from(&o, &b);
    CHECK(!wx_temp_same_bake(&a, &b), "range");
}

static void test_count(void)
{
    CHECK(wx_count_n(30) == 30 && wx_count_n(-6) == 6 && wx_count_n(0) == 0 && wx_count_n(31) == 23 && wx_count_n(-45) == 23,
          "one step per degree up to 30, then 23");
    CHECK(wx_count_value(30, 0) == 0 && wx_count_value(30, 12) == 12 && wx_count_value(30, 30) == 30, "up");
    static const int AT4[24] = {0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15, 16, 18, 19, 20, 22, 23, 24, 26, 27, 28, 30, 31};
    bool same = true;
    for (int k = 0; k <= 23; k++) same &= wx_count_value(31, k) == AT4[k];
    CHECK(same, "31 counts like the AT4 canvas");
    CHECK(wx_count_value(-6, 3) == -3 && wx_count_value(-6, 6) == -6, "down");
    int last = -1;
    bool distinct = true;
    for (int k = 0; k <= 23; k++) {
        int v = wx_count_value(45, k);
        distinct &= v > last;
        last = v;
    }
    CHECK(distinct && last == 45, "45 in 23 distinct steps");
    CHECK(wx_count_step(30, 0) == 0 && wx_count_step(30, WX_ARRIVE_S) == 30 && wx_count_step(30, 9) == 30, "ends");
    int prev = 0;
    bool mono = true;
    for (double t = 0; t <= WX_ARRIVE_S; t += 1.0 / 60) {
        int k = wx_count_step(30, t);
        mono &= k >= prev;
        prev = k;
    }
    CHECK(mono, "monotonic");
    double t8 = WX_ARRIVE_S * (1 - cbrt(1 - 8.0 / 30));
    CHECK(wx_count_step(30, t8 - 1e-4) == 7 && wx_count_step(30, t8 + 1e-4) == 8, "step k lands at T(1-(1-k/n)^(1/3))");
    CHECK(wx_count_step(30, WX_ARRIVE_S / 2) > 15, "ease-out: past half at half time");
    CHECK(wx_arrive(0) == 0 && wx_arrive(WX_ARRIVE_S) == 1 && fabs(wx_arrive(WX_ARRIVE_S / 2) - 0.875) < 1e-9, "ease");
}

static void test_geometry(void)
{
    float x, y;
    wx_gauge_xy(0, &x, &y);
    CHECK(fabsf(x - 76.02f) < 0.01f && fabsf(y - 389.98f) < 0.01f, "start %.2f,%.2f", x, y);
    wx_gauge_xy(1, &x, &y);
    CHECK(fabsf(x - 389.98f) < 0.01f && fabsf(y - 389.98f) < 0.01f, "end %.2f,%.2f", x, y);
    wx_gauge_xy(0.5f, &x, &y);
    CHECK(fabsf(x - 233) < 0.01f && fabsf(y - 11) < 0.01f, "top");
    wx_gauge_xy(0.7f, &x, &y);
    CHECK(fabsf(x - 412.6f) < 0.02f && fabsf(y - 102.51f) < 0.02f, "AT4 marker %.2f,%.2f", x, y);
    wx_gauge_xy(1.0f / 3, &x, &y);
    CHECK(fabsf(x - 76.02f) < 0.02f && fabsf(y - 76.02f) < 0.02f, "AT5 marker %.2f,%.2f", x, y);
    CHECK(wx_merc_top(31) == 122 && fabsf(wx_merc_top(12) - 144) < 1e-4f && wx_merc_top(60) == 122 && wx_merc_top(-40) == 178,
          "mercury");
    CHECK(fabsf(wx_merc_top(9) - 147.474f) < 0.01f, "AT5 mercury %.3f", wx_merc_top(9));
}

static void test_clouds(void)
{
    wx_clouds_t c;
    wx_clouds_for(0, false, &c);
    CHECK(c.n == 0 && wx_clouds_box(&c).x1 == 0, "clear: none");
    wx_clouds_for(2, false, &c);
    CHECK(c.n == 2 && c.tone == WX_TONE_WHITE && c.fade_from == 175, "two white");
    wx_clouds_for(3, true, &c);
    CHECK(c.n == 4 && c.tone == WX_TONE_NIGHT && c.cl[0].back && !c.cl[3].back, "night tones, back first");
    for (int b = 1; b <= 4; b++) {
        wx_clouds_for(b, false, &c);
        wx_box_t x = wx_clouds_box(&c);
        CHECK(x.x0 >= 0 && x.x1 <= WX_FACE && x.y0 >= 40 && x.y1 <= 202 && !(x.x0 & 1) && !(x.y0 & 1) && !(x.x1 & 1) &&
                  !(x.y1 & 1),
              "band %d box %d,%d-%d,%d", b, x.x0, x.y0, x.x1, x.y1);
    }
}

static uint32_t rgb_of(uint16_t p) { return ((p >> 11) << 19) | (((p >> 5) & 63) << 10) | ((p & 31) << 3); }

static bool near(uint16_t p, uint32_t rgb)
{
    uint32_t a = rgb_of(p);
    for (int s = 0; s <= 16; s += 8)
        if (abs((int)((a >> s) & 0xFF) - (int)((rgb >> s) & 0xFF)) > 8) return false;
    return true;
}

static void test_paint(void)
{
    static uint16_t px[WX_FACE * WX_FACE];
    static uint16_t lut[WX_RING_LUT];
    wx_img_t bg = {.px = px, .w = WX_FACE, .h = WX_FACE};
    wx_paint_clear(&bg);
    wx_paint_ring(&bg, 24, 34, false, lut);
    wx_paint_rays(&bg);
    wx_paint_thermo(&bg);
    float x, y;
    wx_gauge_xy(0, &x, &y);
    CHECK(near(px[(int)y * WX_FACE + (int)x], 0xEF913C), "ring start = low colour %06X", (unsigned)rgb_of(px[(int)y * WX_FACE + (int)x]));
    wx_gauge_xy(1, &x, &y);
    CHECK(near(px[(int)y * WX_FACE + (int)x], 0xFE564E), "ring end = high colour");
    wx_gauge_xy(0.5f, &x, &y);
    CHECK(near(px[(int)y * WX_FACE + (int)x], wx_temp_rgb(29)), "ring top = mid colour");
    CHECK(px[455 * WX_FACE + 233] == 0 && px[440 * WX_FACE + 233] == 0, "open at the bottom");
    CHECK(px[233 * WX_FACE + 233 - 222 - 9] == 0 && px[233 * WX_FACE + 233 - 222 + 9] == 0, "ring 12 px wide");
    CHECK(px[233 * WX_FACE + 233 - 222 + 5] != 0 && px[233 * WX_FACE + 233 - 222 - 5] != 0, "ring body");
    CHECK(px[(int)(160 + WX_THERMO_DY) * WX_FACE + 223] != 0 && px[(int)(150 + WX_THERMO_DY) * WX_FACE + 233] == 0,
          "thermometer: grey outline, black inside");
    int ray_px = 0, ray_far = 0;
    for (int i = 0; i < WX_FACE * WX_FACE; i++) {
        int yy = i / WX_FACE, xx = i % WX_FACE;
        float d = hypotf(xx + 0.5f - 233, yy + 0.5f - 233);
        if (d < 190 && px[i]) ray_px++;
        if (d > 196 && d < 214 && px[i]) ray_far++;
    }
    CHECK(ray_px > 2000 && ray_far == 0, "rays inside the rim fade only: %d, %d past r 196", ray_px, ray_far);
    CHECK(px[245 * WX_FACE + 233] == 0, "rays keep out of the value");

    wx_paint_clear(&bg);
    wx_paint_ring(&bg, 0, 0, true, lut);
    wx_gauge_xy(0.5f, &x, &y);
    CHECK(near(px[(int)y * WX_FACE + (int)x], 0x262626), "grey track");

    static uint16_t hp[WX_HERO_BOX * WX_HERO_BOX];
    static uint8_t ha[WX_HERO_BOX * WX_HERO_BOX];
    wx_img_t h = {.px = hp, .a = ha, .w = WX_HERO_BOX, .h = WX_HERO_BOX};
    wx_box_t b = wx_paint_hero(&h, WX_HERO_SUN, 0, 0);
    CHECK(b.x0 == 34 && b.y0 == 34 && b.x1 == 198, "sun box %d,%d", b.x0, b.y0);
    CHECK(ha[82 * WX_HERO_BOX + 82] > 185 && near(hp[82 * WX_HERO_BOX + 82], 0xFFE07A), "sun core: 0.88 and the rim fade");
    CHECK(ha[0] == 0 && ha[10 * WX_HERO_BOX + 10] == 0, "rim fade: nothing past r 195");
    b = wx_paint_hero(&h, WX_HERO_MOON, 25, 50);
    int r_lit = ha[82 * WX_HERO_BOX + 82 + 30], l_lit = ha[82 * WX_HERO_BOX + 82 - 30];
    CHECK(b.x0 == 63 && r_lit == 255 && l_lit == 255 && near(hp[82 * WX_HERO_BOX + 82 + 30], 0xC9D2DC) &&
              near(hp[82 * WX_HERO_BOX + 82 - 30], 0),
          "first quarter: right half lit (%d, left %d)", r_lit, l_lit);
    wx_paint_hero(&h, WX_HERO_MOON, 75, 50);
    CHECK(near(hp[82 * WX_HERO_BOX + 82 + 30], 0) && !near(hp[82 * WX_HERO_BOX + 82 - 30], 0), "last quarter: left half lit");
    wx_paint_hero(&h, WX_HERO_MOON, 0, 0);
    bool dark = true;
    for (int i = 0; i < WX_HERO_BOX * WX_HERO_BOX; i++) {
        int yy = i / WX_HERO_BOX - 82, xx = i % WX_HERO_BOX - 82;
        if (xx * xx + yy * yy < 45 * 45) dark &= rgb_of(hp[i]) == 0;
    }
    CHECK(dark, "new moon: black disc");
    wx_paint_hero(&h, WX_HERO_MOON, 50, 100);
    CHECK(!near(hp[82 * WX_HERO_BOX + 82 - 40], 0) && !near(hp[82 * WX_HERO_BOX + 82 + 40], 0), "full moon");
    b = wx_paint_hero(&h, WX_HERO_MOON_SMALL, 10, 30);
    CHECK(b.x0 == 218 && b.y0 == -10, "small moon box");

    wx_clouds_t cl;
    wx_clouds_for(4, false, &cl);
    wx_box_t cb = wx_clouds_box(&cl);
    int cw = cb.x1 - cb.x0, ch = cb.y1 - cb.y0;
    uint16_t *cp = calloc((size_t)cw * ch, 2);
    uint8_t *ca = calloc((size_t)cw * ch, 1);
    wx_img_t ci = {.px = cp, .a = ca, .w = cw, .h = ch, .x0 = cb.x0, .y0 = cb.y0};
    wx_paint_clouds(&ci, &cl);
    int ix = 128 - cb.x0, iy = 168 - cb.y0;
    CHECK(ca[iy * cw + ix] > 200 && near(cp[iy * cw + ix], 0x555D68), "front dark fill");
    int past = 0;
    for (int i = 0; i < cw * ch; i++) {
        float d = hypotf(cb.x0 + i % cw + 0.5f - 233, cb.y0 + i / cw + 0.5f - 233);
        if (d > 195.5f && ca[i]) past++;
    }
    CHECK(past == 0, "clouds fade out by r 195: %d", past);
    free(cp);
    free(ca);

    static uint16_t tp[WX_THERMO_W * WX_THERMO_H];
    static uint8_t ta[WX_THERMO_W * WX_THERMO_H];
    wx_img_t th = {.px = tp, .a = ta, .w = WX_THERMO_W, .h = WX_THERMO_H};
    wx_paint_mercury(&th, wx_merc_top(31), 0xF96747);
    int lit = 0;
    for (int ty = 0; ty < WX_THERMO_H; ty++) lit += ta[ty * WX_THERMO_W + 233 - WX_THERMO_X] > 128;
    CHECK(lit >= 66 && lit <= 74, "31: full mercury and bulb, %d rows", lit);
    int tick = (int)(134 + WX_THERMO_DY) - WX_THERMO_Y;
    CHECK(near(tp[tick * WX_THERMO_W + 257 - WX_THERMO_X], 0xF96747), "top tick lit");
    wx_paint_mercury(&th, wx_merc_top(9), 0x4CCAA3);
    CHECK(near(tp[tick * WX_THERMO_W + 257 - WX_THERMO_X], 0x8A8A8A), "top tick grey at 9");
    int t160 = (int)(160 + WX_THERMO_DY) - WX_THERMO_Y;
    CHECK(near(tp[t160 * WX_THERMO_W + 257 - WX_THERMO_X], 0x4CCAA3), "bottom tick lit at 9");

    static uint16_t mp[WX_MARKER * WX_MARKER];
    static uint8_t ma[WX_MARKER * WX_MARKER];
    wx_img_t mk = {.px = mp, .a = ma, .w = WX_MARKER, .h = WX_MARKER};
    wx_paint_marker(&mk);
    CHECK(ma[18 * WX_MARKER + 18] == 255 && mp[18 * WX_MARKER + 18] == 0, "marker: black centre");
    CHECK(ma[18 * WX_MARKER + 28] == 255 && near(mp[18 * WX_MARKER + 28], 0xF4F4F2), "marker ring");
    CHECK(ma[18 * WX_MARKER + 34] >= 250 && mp[18 * WX_MARKER + 34] == 0 && ma[0] == 0, "marker black halo, clear corner");
}

static void test_shift(void)
{
    static const int DX[5] = {0, 2, 2, 0, 0}, DY[5] = {0, 0, 2, 2, 0};
    wx_shift_t s = {0};
    int dx, dy, steps = 0;
    wx_shift_xy(&s, &dx, &dy);
    CHECK(dx == 0 && dy == 0, "burn-in: starts at 0,0");
    for (int k = 1; k <= 4; k++) {
        for (int f = 0; f < 119 * 30; f++) steps += wx_shift_tick(&s, 1.0 / 30);
        CHECK(steps == k - 1, "burn-in: no step before %g s (%d)", WX_SHIFT_S, steps);
        for (int f = 0; f < 31; f++) steps += wx_shift_tick(&s, 1.0 / 30);
        wx_shift_xy(&s, &dx, &dy);
        CHECK(steps == k && dx == DX[k] && dy == DY[k], "burn-in step %d: %d,%d want %d,%d", k, dx, dy, DX[k], DY[k]);
    }
    CHECK(!wx_shift_tick(&s, 0) && !wx_shift_tick(&s, NAN), "burn-in: no step on 0 or NaN");
}

static int px_of(wx_box_t b) { return (b.x1 - b.x0) * (b.y1 - b.y0); }

static void test_bake(void)
{
    static uint16_t bg[WX_FACE * WX_FACE], lut[WX_RING_LUT];
    static uint8_t hero[WX_HERO_BOX * WX_HERO_BOX * 3];
    int still_max = wx_clouds_max_px(false), drift_max = wx_clouds_max_px(true);
    uint8_t *still = malloc((size_t)still_max * 3), *drift = malloc((size_t)drift_max * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .hero = hero, .still = still, .drift = drift};

    int layer_max = 0;
    for (int band = 1; band <= 4; band++) {
        wx_clouds_t all, st, dr;
        wx_clouds_for(band, band == 3, &all);
        wx_clouds_split(&all, &st, &dr);
        wx_box_t ab = wx_clouds_box(&all), sb = wx_clouds_box(&st), db = wx_clouds_box(&dr);
        if (px_of(ab) > layer_max) layer_max = px_of(ab);
        CHECK(dr.n == 1 && st.n == all.n - 1 && !dr.cl[0].back, "band %d: one front cloud drifts", band);
        CHECK(band == 1 ? px_of(db) == px_of(ab) : px_of(db) * 2 < px_of(ab), "band %d: drift box %d px, layer %d px", band, px_of(db), px_of(ab));
        int aw = ab.x1 - ab.x0, ah = ab.y1 - ab.y0;
        uint16_t *ap = calloc((size_t)aw * ah, 2);
        uint8_t *aa = calloc((size_t)aw * ah, 1);
        wx_img_t ai = {.px = ap, .a = aa, .w = aw, .h = ah, .x0 = ab.x0, .y0 = ab.y0};
        wx_paint_clouds(&ai, &all);
        wx_temp_t t = {.cloud_band = band, .night = band == 3, .lo = 0, .hi = 10};
        wx_bake_out_t o;
        wx_bake(&t, &b, &o);
        CHECK(!memcmp(&o.drift, &db, sizeof db) && (st.n ? !memcmp(&o.still, &sb, sizeof sb) : o.still.x1 == 0),
              "band %d: bake boxes", band);
        int sw = o.still.x1 - o.still.x0, dw = o.drift.x1 - o.drift.x0, bad = 0;
        for (int y = ab.y0; y < ab.y1; y++)
            for (int x = ab.x0; x < ab.x1; x++) {
                if (hypotf(x + 0.5f - 233, y + 0.5f - 233) > all.fade_from) continue;
                float ra = 0, rr = 0, rg = 0, rb = 0;
                int boxes[2][4] = {{o.still.x0, o.still.y0, o.still.x1, o.still.y1}, {o.drift.x0, o.drift.y0, o.drift.x1, o.drift.y1}};
                for (int l = 0; l < 2; l++) {
                    int *q = boxes[l];
                    if (x < q[0] || x >= q[2] || y < q[1] || y >= q[3]) continue;
                    int w = l ? dw : sw, h = q[3] - q[1];
                    uint8_t *buf = l ? drift : still;
                    int i = (y - q[1]) * w + (x - q[0]);
                    float a = buf[(size_t)w * h * 2 + i] / 255.0f;
                    uint32_t c = rgb_of(((uint16_t *)buf)[i]);
                    rr = rr * (1 - a) + ((c >> 16) & 0xFF) * a;
                    rg = rg * (1 - a) + ((c >> 8) & 0xFF) * a;
                    rb = rb * (1 - a) + (c & 0xFF) * a;
                    ra = a + ra * (1 - a);
                }
                int i = (y - ab.y0) * aw + (x - ab.x0);
                uint32_t c = rgb_of(ap[i]);
                float a = aa[i] / 255.0f;
                if (fabsf(ra - a) > 0.02f || fabsf(rr - ((c >> 16) & 0xFF) * a) > 10 || fabsf(rg - ((c >> 8) & 0xFF) * a) > 10 ||
                    fabsf(rb - (c & 0xFF) * a) > 10)
                    bad++;
            }
        CHECK(bad == 0, "band %d: still under drift = the old single layer inside the rim fade (%d px off)", band, bad);
        free(ap);
        free(aa);
    }
    printf("temp: clouds still layer <= %d px, drift sprite <= %d px (was one %d px layer)\n", still_max, drift_max,
           layer_max);
    printf("temp: PSRAM while shown %d B\n", WX_FACE * WX_FACE * 2 + WX_RING_LUT * 2 + WX_HERO_BOX * WX_HERO_BOX * 3 +
                                                  (still_max + drift_max + WX_THERMO_W * WX_THERMO_H + WX_MARKER * WX_MARKER) * 3);

    static const struct {
        const char *name;
        wx_temp_t t;
    } CASES[] = {
        {"AT4 sun, one cloud", {.have = true, .lo = 24, .hi = 34, .cloud_band = 1, .hero = WX_HERO_SUN}},
        {"AT5 moon, stars", {.have = true, .lo = 6, .hi = 15, .night = true, .hero = WX_HERO_MOON, .moon_phase = 80, .moon_illum = 30, .stars = true}},
        {"AT6 small moon, light grey", {.have = true, .lo = 6, .hi = 15, .night = true, .cloud_band = 3, .hero = WX_HERO_MOON_SMALL, .moon_phase = 10, .moon_illum = 30}},
        {"overcast", {.have = true, .lo = 6, .hi = 15, .cloud_band = 4}},
    };
    double worst = 0;
    for (size_t k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
        wx_bake_out_t o;
        clock_t c0 = clock();
        wx_bake(&CASES[k].t, &b, &o);
        double ms = (double)(clock() - c0) * 1000 / CLOCKS_PER_SEC;
        if (ms > worst) worst = ms;
        CHECK((CASES[k].t.hero != WX_HERO_NONE) == (o.hero.x1 > o.hero.x0), "%s: hero box", CASES[k].name);
        printf("temp: bake %s %.1f ms on the host (background, hero, clouds)\n", CASES[k].name, ms);
    }
    CHECK(worst > 0, "bake timed");
    free(still);
    free(drift);
}

int main(void)
{
    test_scale();
    test_uv();
    test_face_from();
    test_same_bake();
    test_count();
    test_geometry();
    test_clouds();
    test_paint();
    test_shift();
    test_bake();
    if (failures) {
        printf("temp: %d failure(s)\n", failures);
        return 1;
    }
    printf("temp: all tests passed\n");
    return 0;
}
