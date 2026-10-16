#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_cast.h"
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

static void test_bake_key(void)
{
    static const int KEEP[][3] = {
        {72, -1, 3}, {72, 2, 2}, {74, 2, 2}, {75, 2, 3}, {36, 2, 2}, {35, 2, 2}, {34, 2, 1}, {14, 0, 0},
        {15, 0, 1}, {85, 4, 4}, {84, 4, 3}, {5, 1, 1}, {4, 1, 0}, {94, 3, 3}, {95, 3, 4}, {44, 1, 1}, {45, 1, 2},
    };
    for (size_t i = 0; i < sizeof KEEP / sizeof KEEP[0]; i++)
        CHECK(wx_cloud_band_keep(KEEP[i][0], KEEP[i][1]) == KEEP[i][2], "cloud %d%% shown band %d: band %d, want %d",
              KEEP[i][0], KEEP[i][1], wx_cloud_band_keep(KEEP[i][0], KEEP[i][1]), KEEP[i][2]);

    wx_obs_t o = obs("clouds", "2", 12, 0);
    o.has_cloud = true;
    o.cloud = 38;
    wx_temp_t shown, t;
    wx_temp_from(&o, &shown);
    CHECK(shown.cloud_band == 1 && shown.hero == WX_HERO_SUN, "38 %%: one cloud, sun");
    for (int c = 38; c <= 44; c++) {
        o.cloud = c;
        wx_temp_from_shown(&o, &shown, &t);
        CHECK(t.cloud_band == 1 && t.hero == WX_HERO_SUN && !strcmp(t.sub, "Mostly sunny") && wx_temp_bake_kind(&t, &shown) == 0,
              "cloud %d%% next to the 40 %% edge keeps the shown band: no re-bake ('%s')", c, t.sub);
    }
    o.cloud = 45;
    wx_temp_from_shown(&o, &shown, &t);
    CHECK(t.cloud_band == 2 && wx_temp_bake_kind(&t, &shown) == WX_BAKE_FULL, "45 %%: the band moves, full re-bake");
    wx_temp_from(&o, &shown);
    o.cloud = 36;
    wx_temp_from_shown(&o, &shown, &t);
    CHECK(t.cloud_band == 2, "and stays down to 35 %%");
    o.cloud = 42;
    wx_temp_from_shown(&o, NULL, &t);
    CHECK(t.cloud_band == 2, "nothing shown: plain bands");
    shown.have = false;
    o.cloud = 38;
    wx_temp_from_shown(&o, &shown, &t);
    CHECK(t.cloud_band == 1, "a shown face without data keeps no band");

    o = obs("clear", "0", 8, 1);
    o.has_moon = true;
    o.moon_up = 1;
    o.moon_phase = 24;
    static const int ILLUM[][2] = {{0, 0}, {2, 0}, {3, 5}, {46, 45}, {47, 45}, {48, 50}, {98, 100}, {100, 100}, {130, 100}, {-4, 0}};
    for (size_t i = 0; i < sizeof ILLUM / sizeof ILLUM[0]; i++) {
        o.moon_illum = ILLUM[i][0];
        wx_temp_from(&o, &t);
        CHECK(t.moon_illum == ILLUM[i][1], "illum %d%% -> %d, want %d", ILLUM[i][0], t.moon_illum, ILLUM[i][1]);
    }
    o.moon_illum = 46;
    wx_temp_from(&o, &shown);
    o.moon_illum = 47;
    o.moon_phase = 30;
    o.temp_c = 9;
    wx_temp_from(&o, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == 0, "illum 46 -> 47, phase 24 -> 30 (still waxing), a new value: no re-bake");
    o.moon_illum = 48;
    wx_temp_from(&o, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == WX_BAKE_HERO, "illum to the next 5 %% step: hero only");
    o.moon_illum = 46;
    o.moon_phase = 60;
    wx_temp_from(&o, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == WX_BAKE_HERO, "waxing to waning: hero only");
    o.moon_phase = 24;
    o.moon_up = 0;
    wx_temp_from(&o, &t);
    CHECK(t.hero == WX_HERO_NONE && wx_temp_bake_kind(&t, &shown) == WX_BAKE_HERO, "moonset: hero only");
    o.moon_up = 1;
    o.has_today = true;
    o.min_c = 2;
    o.max_c = 7;
    wx_temp_from(&o, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == WX_BAKE_RING, "a range: ring only");
    wx_temp_t r = t;
    r.has_range = !t.has_range;
    CHECK(r.lo == t.lo && r.hi == t.hi && wx_temp_bake_kind(&r, &t) == WX_BAKE_RING,
          "the range comes or goes with the same lo and hi: ring only");
    o.moon_illum = 60;
    wx_temp_from(&o, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == (WX_BAKE_RING | WX_BAKE_HERO), "range and moon: ring and hero");
    o.has_today = false;
    o.moon_illum = 46;
    o.night = false;
    wx_temp_from(&o, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == WX_BAKE_FULL, "dawn: full");
    o.night = true;
    o.has_cloud = true;
    o.cloud = 50;
    wx_temp_from(&o, &t);
    CHECK(!t.stars && wx_temp_bake_kind(&t, &shown) == WX_BAKE_FULL, "clouds at night (stars off): full");
    o.has_temp = false;
    wx_temp_from(&o, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == WX_BAKE_FULL, "data lost: full");
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

static float off_puff(const wx_circle_t *a, const wx_circle_t *b, float t, float *x, float *y)
{
    *x = a->x + a->r * cosf(t);
    *y = a->y + a->r * sinf(t);
    return hypotf(*x - b->x, *y - b->y) - b->r;
}

static void upper_cross(const wx_circle_t *a, const wx_circle_t *b, float *cx, float *cy)
{
    *cx = a->x;
    *cy = 1e9f;
    const int N = 3600;
    for (int i = 0; i < N; i++) {
        float lo = -(float)M_PI + 2 * (float)M_PI * i / N, hi = -(float)M_PI + 2 * (float)M_PI * (i + 1) / N, x, y;
        if ((off_puff(a, b, lo, &x, &y) > 0) == (off_puff(a, b, hi, &x, &y) > 0)) continue;
        for (int k = 0; k < 40; k++) {
            float mid = (lo + hi) / 2;
            if ((off_puff(a, b, mid, &x, &y) > 0) == (off_puff(a, b, lo, &x, &y) > 0)) lo = mid;
            else hi = mid;
        }
        off_puff(a, b, lo, &x, &y);
        if (y < *cy) {
            *cx = x;
            *cy = y;
        }
    }
}

static float base_end(const wx_circle_t *c, float base, float side)
{
    return c->x + side * sqrtf(c->r * c->r - (c->y - base) * (c->y - base));
}

static int s_under_n;

static void check_cloud_path(const char *what, const wx_clouds_t *all)
{
    static const uint32_t TONE[4][3] = {{0xE6ECF2, 0xE6ECF2, 0xC4CCD4},
                                        {0x8E96A0, 0x737B86, 0xA8B0BA},
                                        {0x555D68, 0x3A4048, 0x6A7480},
                                        {0x3E4652, 0x2E343D, 0x56606C}};
    for (int i = 0; i < all->n; i++) {
        wx_clouds_t one = *all;
        one.n = 1;
        one.cl[0] = all->cl[i];
        const wx_cloud_t *cl = &one.cl[0];
        const uint16_t fill = wx_rgb565(TONE[one.tone][cl->back ? 1 : 0]), line = wx_rgb565(TONE[one.tone][2]);
        wx_box_t b = wx_clouds_box(&one);
        int w = b.x1 - b.x0, h = b.y1 - b.y0;
        uint16_t *px = calloc((size_t)w * h, 2);
        uint8_t *a = calloc((size_t)w * h, 1);
        wx_img_t im = {.px = px, .a = a, .w = w, .h = h, .x0 = b.x0, .y0 = b.y0};
        wx_paint_clouds(&im, &one);
        const float lim = one.fade_from - 1;
#define AT(x, y) (((x) - b.x0) + ((y) - b.y0) * w)
#define CLEAR(x, y) (hypotf((float)(x) + 0.5f - 233, (float)(y) + 0.5f - 233) <= lim)
        int under_bad = 0, under_n = 0, base_bad = 0, base_n = 0, edge_bad = 0, edge_n = 0;
        for (int k = 0; k + 1 < cl->n; k++) {
            float cx, cy;
            upper_cross(&cl->c[k], &cl->c[k + 1], &cx, &cy);
            int x = (int)floorf(cx);
            for (int y = (int)ceilf(cy + 3); (float)y + 0.5f <= cl->base - 3.5f; y++) {
                if (!CLEAR(x, y) || x < b.x0 || x >= b.x1) continue;
                under_n++;
                under_bad += a[AT(x, y)] != 255 || px[AT(x, y)] != fill;
            }
        }
        const float xl = base_end(&cl->c[0], cl->base, -1) + 3, xr = base_end(&cl->c[cl->n - 1], cl->base, 1) - 3;
        const int yb = (int)floorf(cl->base + 1.5f), ye = (int)floorf(cl->base + 2.5f);
        for (int x = (int)ceilf(xl); (float)x + 1 <= xr; x++) {
            if (x < b.x0 || x >= b.x1 || !CLEAR(x, yb) || !CLEAR(x, ye)) continue;
            base_n++;
            base_bad += yb >= b.y1 || a[AT(x, yb)] != 255 || px[AT(x, yb)] != line;
            edge_n++;
            edge_bad += ye >= b.y1 || a[AT(x, ye)] == 0 || a[AT(x, ye)] == 255;
        }
        s_under_n += under_n;
        CHECK(under_bad == 0, "%s cloud %d: under each puff junction the fill reaches the base stroke (%d of %d px)",
              what, i, under_bad, under_n);
        CHECK(base_n > 20 && base_bad == 0, "%s cloud %d: one straight base stroke end to end (%d of %d px missing)",
              what, i, base_bad, base_n);
        CHECK(edge_bad == 0 && b.y1 >= (int)ceilf(cl->base + 2.75f),
              "%s cloud %d: the base stroke's soft lower edge inside the box (y1 %d, base %.0f, %d of %d px)", what, i,
              b.y1, cl->base, edge_bad, edge_n);
        wx_cloud_shape_t sh;
        wx_cloud_shape(cl, &sh);
        int out = 0;
        for (int y = b.y0 - 8; y < b.y1 + 8; y++)
            for (int x = b.x0 - 8; x < b.x1 + 8; x++) {
                bool in = x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1;
                bool face = x >= 0 && y >= 0 && x < WX_FACE && y < WX_FACE;
                if (!in && face && wx_cloud_sd(&sh, (float)x + 0.5f, (float)y + 0.5f) < 2.75f) out++;
            }
        CHECK(out == 0, "%s cloud %d: no ink outside its box (%d px)", what, i, out);
#undef AT
#undef CLEAR
        free(px);
        free(a);
    }
}

static void test_cloud_path(void)
{
    char what[32];
    for (int band = 1; band <= 4; band++)
        for (int night = 0; night <= 1; night++) {
            wx_clouds_t c;
            wx_clouds_for(band, night, &c);
            snprintf(what, sizeof what, "band %d%s", band, night ? " night" : "");
            check_cloud_path(what, &c);
        }
    for (int v = 1; v <= 3; v++)
        for (int s = 0; s < wx_cast_sides(v); s++) {
            wx_clouds_t c;
            wx_cast_clouds(v, s, &c);
            snprintf(what, sizeof what, "forecast %d side %d", v, s);
            check_cloud_path(what, &c);
        }
    CHECK(s_under_n > 500, "junction columns checked: %d px", s_under_n);
    wx_cloud_t two = {false, 170, 2, {{100, 150, 25}, {140, 150, 25}}};
    wx_cloud_shape_t sh;
    wx_cloud_shape(&two, &sh);
    CHECK(fabsf(sh.p[1][0] - 120) < 1e-4f && fabsf(sh.p[1][1] - 135) < 1e-4f, "two puffs cross at 120,135");
    CHECK(fabsf(wx_cloud_sd(&sh, 120, 168) + 2) < 1e-3f, "under the crossing, 2 px above the base: 2 px in");
    CHECK(fabsf(wx_cloud_sd(&sh, 120, 173) - 3) < 1e-3f, "3 px under the base: 3 px out");
    CHECK(fabsf(wx_cloud_sd(&sh, 120, 137) + 2) < 1e-3f, "2 px under the crossing: 2 px in, a round join");
    CHECK(fabsf(wx_cloud_sd(&sh, 100, 122) - 3) < 1e-3f, "above a puff: the distance to its arc");
    CHECK(fabsf(wx_cloud_sd(&sh, 75 - 2, 150) - 2) < 1e-3f, "beside the left puff: the distance to its arc");
    CHECK(fabsf(wx_cloud_sd(&sh, 83, 173) - sqrtf(13)) < 1e-3f, "off the base corner: round");
}

static void test_paint(void)
{
    static uint16_t px[WX_FACE * WX_FACE];
    static uint16_t lut[WX_RING_LUT];
    static wx_ring_t ring;
    wx_img_t bg = {.px = px, .w = WX_FACE, .h = WX_FACE};
    wx_paint_clear(&bg);
    wx_paint_ring(&ring, 24, 34, false, lut);
    CHECK(ring.npx == WX_RING_PX && ring.nrun == WX_RING_RUNS, "ring strip: %d px in %d runs", ring.npx, ring.nrun);
    wx_ring_blit(&ring, px);
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
    wx_paint_ring(&ring, 0, 0, true, lut);
    wx_ring_blit(&ring, px);
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
    CHECK(!wx_shift_tick(&s, 0) && !wx_shift_tick(&s, NAN) && !wx_shift_tick(&s, INFINITY) && !wx_shift_tick(&s, -1e9),
          "burn-in: no step on 0, NaN, infinity or a negative dt");
    steps = 0;
    for (int f = 0; f < 121 * 30; f++) steps += wx_shift_tick(&s, 1.0 / 30);
    wx_shift_xy(&s, &dx, &dy);
    CHECK(steps == 1 && dx == DX[1] && dy == DY[1], "burn-in: steps again after a NaN dt (%d steps, at %d,%d)", steps, dx,
          dy);
}

static int px_of(wx_box_t b) { return (b.x1 - b.x0) * (b.y1 - b.y0); }

static void over(const uint8_t *buf, wx_box_t q, int x, int y, float acc[4])
{
    if (x < q.x0 || x >= q.x1 || y < q.y0 || y >= q.y1) return;
    int w = q.x1 - q.x0, i = (y - q.y0) * w + (x - q.x0);
    float a = buf[(size_t)w * (q.y1 - q.y0) * 2 + i] / 255.0f;
    uint32_t c = rgb_of(((const uint16_t *)buf)[i]);
    acc[0] = acc[0] * (1 - a) + ((c >> 16) & 0xFF) * a;
    acc[1] = acc[1] * (1 - a) + ((c >> 8) & 0xFF) * a;
    acc[2] = acc[2] * (1 - a) + (c & 0xFF) * a;
    acc[3] = a + acc[3] * (1 - a);
}

static uint8_t *paint_layer(const wx_clouds_t *c, wx_box_t *box)
{
    *box = wx_clouds_box(c);
    if (!c->n || box->x1 <= box->x0) {
        *box = (wx_box_t){0, 0, 0, 0};
        return calloc(1, 3);
    }
    int w = box->x1 - box->x0, h = box->y1 - box->y0;
    uint8_t *buf = calloc((size_t)w * h, 3);
    wx_img_t im = {.px = (uint16_t *)buf, .a = buf + (size_t)w * h * 2, .w = w, .h = h, .x0 = box->x0, .y0 = box->y0};
    wx_paint_clouds(&im, c);
    return buf;
}

static bool acc_near(const float a[4], const float b[4], float da, float dc)
{
    return fabsf(a[3] - b[3]) <= da && fabsf(a[0] - b[0]) <= dc && fabsf(a[1] - b[1]) <= dc && fabsf(a[2] - b[2]) <= dc;
}

static void test_bake(void)
{
    static uint16_t bg[WX_FACE * WX_FACE], lut[WX_RING_LUT];
    static uint8_t hero[WX_HERO_BOX * WX_HERO_BOX * 3];
    static wx_ring_t ring;
    int scene_px = wx_scene_px();
    uint8_t *scene = malloc((size_t)scene_px * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .scene = scene};

    int layer_max = 0, sprites_max = 0, old_max = 0;
    for (int band = 1; band <= 4; band++)
        for (int night = 0; night < 2; night++) {
            wx_clouds_t all, one, back;
            wx_clouds_for(band, night, &all);
            wx_box_t ab, sb, db;
            uint8_t *whole = paint_layer(&all, &ab);
            back = all;
            back.n = all.n - 1;
            wx_cloud_one(&all, all.n - 1, &one);
            uint8_t *old_still = paint_layer(&back, &sb), *old_drift = paint_layer(&one, &db);
            if (px_of(ab) > layer_max) layer_max = px_of(ab);
            if (px_of(sb) + px_of(db) > old_max) old_max = px_of(sb) + px_of(db);
            wx_temp_t t = {.cloud_band = band, .night = night, .lo = 0, .hi = 10};
            wx_bake_out_t o;
            memset(&o, 0x5A, sizeof o);
            wx_bake(&t, WX_BAKE_FULL, &b, &o);
            CHECK(o.nclouds == all.n, "band %d night %d: one sprite per cloud (%d of %d)", band, night, o.nclouds, all.n);
            int at = 0, boxes = 0, own = 0, aligned = 0;
            for (int i = 0; i < o.nclouds; i++) {
                wx_cloud_one(&all, i, &one);
                wx_box_t want;
                uint8_t *ref = paint_layer(&one, &want);
                boxes += !memcmp(&o.cloud[i], &want, sizeof want) && o.cloud_at[i] >= at && o.cloud_at[i] < at + WX_SCENE_ALIGN &&
                         o.cloud_back[i] == all.cl[i].back;
                aligned += o.cloud_at[i] % WX_SCENE_ALIGN == 0 && (o.cloud_at[i] + px_of(want) * 2) % WX_SCENE_ALIGN == 0;
                own += !memcmp(scene + o.cloud_at[i], ref, (size_t)px_of(want) * 3);
                at = o.cloud_at[i] + px_of(want) * 3;
                free(ref);
            }
            for (int i = o.nclouds; i < WX_CLOUDS_MAX; i++) boxes += o.cloud[i].x1 == 0 && o.cloud_at[i] == 0 && !o.cloud_back[i];
            CHECK(aligned == o.nclouds, "band %d night %d: every sprite and its alpha plane start on a %d-byte boundary (%d of %d)", band, night,
                  WX_SCENE_ALIGN, aligned, o.nclouds);
            CHECK(boxes == WX_CLOUDS_MAX && own == o.nclouds && at <= scene_px * 3,
                  "band %d night %d: sprites back to back in the scene buffer, each its own cloud, depth and rim fade (%d of %d B)", band,
                  night, at, scene_px * 3);
            if (at / 3 > sprites_max) sprites_max = at / 3;
            int off_layer = 0, off_old = 0, rim_overlap = 0, rim_single = 0;
            float rim_da = 0;
            for (int y = ab.y0; y < ab.y1; y++)
                for (int x = ab.x0; x < ab.x1; x++) {
                    float r = hypotf(x + 0.5f - 233, y + 0.5f - 233);
                    if (r > 175) continue;
                    float got[4] = {0}, layer[4] = {0}, old[4] = {0};
                    for (int i = 0; i < o.nclouds; i++) over(scene + o.cloud_at[i], o.cloud[i], x, y, got);
                    over(whole, ab, x, y, layer);
                    over(old_still, sb, x, y, old);
                    over(old_drift, db, x, y, old);
                    if (r <= all.fade_from && !acc_near(got, layer, 0.02f, 10)) off_layer++;
                    if (r <= all.fade_from && !acc_near(got, old, 0.02f, 10)) off_old++;
                    if (r > all.fade_from && !acc_near(got, old, 0.02f, 10)) {
                        int cover = 0;
                        for (int i = 0; i < o.nclouds; i++) {
                            float one_acc[4] = {0};
                            over(scene + o.cloud_at[i], o.cloud[i], x, y, one_acc);
                            cover += one_acc[3] > 0;
                        }
                        rim_single += cover < 2;
                        rim_overlap++;
                        if (fabsf(got[3] - old[3]) > rim_da) rim_da = fabsf(got[3] - old[3]);
                    }
                }
            CHECK(off_layer == 0, "band %d night %d: the sprites at rest = the single cloud layer inside its rim fade (%d px off)", band,
                  night, off_layer);
            CHECK(off_old == 0, "band %d night %d: the first frame = the old still layer under the drift sprite inside its rim fade (%d px off)",
                  band, night, off_old);
            CHECK(rim_single == 0 && rim_da <= 0.26f,
                  "band %d night %d: from the rim fade to r 175 only overlaps of two sprites differ (%d single), alpha by %.3f (k - k^2 <= 0.25)",
                  band, night, rim_single, rim_da);
            if (!night)
                printf("temp: band %d, fade from r %.0f: %d px between it and r 175 differ from the old split (overlaps fading on their own, "
                       "alpha up to +%.3f)\n", band, all.fade_from, rim_overlap, rim_da);
            free(whole);
            free(old_still);
            free(old_drift);
        }
    printf("temp: cloud sprites <= %d px in the %d px scene buffer (old still layer + drift sprite <= %d px, one layer %d px)\n", sprites_max,
           scene_px, old_max, layer_max);
    printf("temp: PSRAM while shown %d B (ring strip %d B, hero back buffer %d B)\n",
           WX_FACE * WX_FACE * 2 + WX_RING_LUT * 2 + (int)sizeof ring + 2 * WX_HERO_BOX * WX_HERO_BOX * 3 +
               (scene_px + WX_THERMO_W * WX_THERMO_H + WX_MARKER * WX_MARKER) * 3,
           (int)sizeof ring, WX_HERO_BOX * WX_HERO_BOX * 3);

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
        wx_bake(&CASES[k].t, WX_BAKE_FULL, &b, &o);
        double ms = (double)(clock() - c0) * 1000 / CLOCKS_PER_SEC;
        if (ms > worst) worst = ms;
        CHECK((CASES[k].t.hero != WX_HERO_NONE) == (o.hero.x1 > o.hero.x0), "%s: hero box", CASES[k].name);
        printf("temp: bake %s %.1f ms on the host (background, hero, clouds)\n", CASES[k].name, ms);
    }
    CHECK(worst > 0, "bake timed");

    static uint16_t full[WX_FACE * WX_FACE], keep[WX_FACE * WX_FACE];
    static uint8_t hero2[WX_HERO_BOX * WX_HERO_BOX * 3];
    uint8_t *scene2 = malloc((size_t)scene_px * 3);
    for (size_t k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
        wx_temp_t t = CASES[k].t, n = t;
        n.lo -= 7;
        n.hi += 3;
        wx_bake_out_t o, o2;
        wx_bake(&n, WX_BAKE_FULL, &b, &o2);
        memcpy(full, bg, sizeof full);
        wx_bake(&t, WX_BAKE_FULL, &b, &o);
        memcpy(keep, bg, sizeof keep);
        memcpy(scene2, scene, (size_t)scene_px * 3);
        memcpy(hero2, hero, sizeof hero2);
        clock_t c0 = clock();
        wx_bake(&n, WX_BAKE_RING, &b, &o);
        double ms = (double)(clock() - c0) * 1000 / CLOCKS_PER_SEC;
        CHECK(!memcmp(bg, keep, sizeof keep) && memcmp(keep, full, sizeof full) != 0, "%s: a ring bake leaves the shown background", CASES[k].name);
        wx_ring_blit(&ring, bg);
        CHECK(!memcmp(bg, full, sizeof full), "%s: the ring blit over the old background = a full bake of the new range", CASES[k].name);
        CHECK(!memcmp(hero, hero2, sizeof hero2) && !memcmp(scene, scene2, (size_t)scene_px * 3) && !memcmp(&o.hero, &o2.hero, sizeof o.hero),
              "%s: a ring bake leaves the hero and the clouds", CASES[k].name);
        printf("temp: ring bake %s %.2f ms on the host\n", CASES[k].name, ms);

        n = t;
        n.moon_illum = t.moon_illum + 25;
        n.moon_phase = 100 - t.moon_phase;
        memcpy(keep, bg, sizeof keep);
        memcpy(scene2, scene, (size_t)scene_px * 3);
        c0 = clock();
        wx_bake(&n, WX_BAKE_HERO, &b, &o);
        ms = (double)(clock() - c0) * 1000 / CLOCKS_PER_SEC;
        CHECK(!memcmp(bg, keep, sizeof keep) && !memcmp(scene, scene2, (size_t)scene_px * 3), "%s: a hero bake leaves the background and the clouds",
              CASES[k].name);
        CHECK(t.hero < WX_HERO_MOON || memcmp(hero, hero2, sizeof hero2) != 0, "%s: a hero bake repaints the moon", CASES[k].name);
        printf("temp: hero bake %s %.2f ms on the host\n", CASES[k].name, ms);
    }
    free(scene2);
    free(scene);
}

static double circ(double a, double b)
{
    double d = fabs(a - b);
    return d > 0.5 ? 1 - d : d;
}

static void test_drift(void)
{
    wx_drift_t d = {0};
    int px, py;
    CHECK(wx_drift_step(&d) == -1, "drift: nothing before a start");
    int leans[2] = {0};
    for (int band = 1; band <= 4; band++) {
        wx_clouds_t all;
        wx_clouds_for(band, false, &all);
        int n = all.n, steps = 0, lag_even = 0, worst_lag = 0, bad_step = 0, two = 0, wrong = 0, out = 0, off_extreme = 0, off_mean = 0;
        int against = 0, flat_back = 0;
        long box_px = 0;
        bool back[WX_CLOUDS_MAX];
        for (int i = 0; i < n; i++) back[i] = all.cl[i].back;
        for (uint32_t seed = 0; seed < 32; seed++) {
            wx_drift_start(&d, n, back, seed * 2654435761u);
            CHECK(d.n == n && fabsf(d.lean) >= WX_DRIFT_LEAN_MIN && fabsf(d.lean) <= WX_DRIFT_LEAN_MAX, "band %d: %d paths, lean %.2f px", band,
                  n, d.lean);
            leans[d.lean > 0]++;
            for (int i = 0; i < n; i++) {
                CHECK(d.period[i] >= WX_DRIFT_MIN_S && d.period[i] <= WX_DRIFT_MAX_S, "band %d seed %u: period %.2f s in 7-13 s", band,
                      seed, d.period[i]);
                for (int j = 0; j < i; j++)
                    CHECK(fabsf(d.period[i] - d.period[j]) >= 0.3f * (WX_DRIFT_MAX_S - WX_DRIFT_MIN_S) / n &&
                              circ(d.phase_x[i], d.phase_x[j]) >= 0.3 / n - 1e-6,
                          "band %d seed %u: clouds %d and %d differ in period (%.2f, %.2f s) and phase (%.3f, %.3f)", band, seed, i, j,
                          d.period[i], d.period[j], d.phase_x[i], d.phase_x[j]);
            }
            int lo[WX_CLOUDS_MAX], hi[WX_CLOUDS_MAX], plo[WX_CLOUDS_MAX], phi[WX_CLOUDS_MAX];
            double sum[WX_CLOUDS_MAX] = {0};
            int cnt = 0;
            for (int k = 0; k < n; k++) lo[k] = hi[k] = plo[k] = phi[k] = 0;
            uint32_t r = seed + 1;
            while (d.t < 60) {
                r = r * 1664525u + 1013904223u;
                wx_drift_tick(&d, seed & 1 ? (1 + (r >> 30)) / 60.0 : 1.0 / WX_DRIFT_FPS);
                int bx[WX_CLOUDS_MAX], by[WX_CLOUDS_MAX];
                memcpy(bx, d.dx, sizeof bx);
                memcpy(by, d.dy, sizeof by);
                int i = wx_drift_step(&d), moved = 0;
                for (int k = 0; k < n; k++) {
                    if (d.dx[k] == bx[k] && d.dy[k] == by[k]) continue;
                    moved++;
                    wrong += k != i;
                    bad_step += abs(d.dx[k] - bx[k]) > 1 || abs(d.dy[k] - by[k]) > 1;
                }
                two += moved > 1;
                if (i >= 0) {
                    steps++;
                    wx_clouds_t one;
                    wx_cloud_one(&all, i, &one);
                    wx_box_t b = wx_clouds_box(&one);
                    box_px += (long)(b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1);
                }
                bool settled = d.t >= WX_DRIFT_RAMP_S + 1;
                cnt += settled;
                for (int k = 0; k < n; k++) {
                    out += abs(d.dx[k]) > WX_DRIFT_X || abs(d.dy[k]) > WX_DRIFT_Y;
                    wx_drift_path(&d, k, floor(d.t * WX_DRIFT_FPS) / WX_DRIFT_FPS, &px, &py);
                    int lag = abs(px - d.dx[k]) > abs(py - d.dy[k]) ? abs(px - d.dx[k]) : abs(py - d.dy[k]);
                    if (lag > worst_lag) worst_lag = lag;
                    if (!(seed & 1) && lag > lag_even) lag_even = lag;
                    if (!settled) continue;
                    sum[k] += d.dx[k];
                    if (d.dx[k] < lo[k] || cnt == 1) lo[k] = d.dx[k];
                    if (d.dx[k] > hi[k] || cnt == 1) hi[k] = d.dx[k];
                    if (px < plo[k] || cnt == 1) plo[k] = px;
                    if (px > phi[k] || cnt == 1) phi[k] = px;
                }
            }
            int front_range = 0, back_range = 99;
            for (int k = 0; k < n; k++) {
                double mean = sum[k] / cnt, want = d.depth[k] * d.lean;
                off_extreme += lo[k] != plo[k] || hi[k] != phi[k];
                off_mean += fabs(mean - want) > 0.75;
                against += mean * d.lean <= 0;
                if (back[k]) back_range = hi[k] - lo[k] < back_range ? hi[k] - lo[k] : back_range;
                else front_range = hi[k] - lo[k] > front_range ? hi[k] - lo[k] : front_range;
            }
            if (band >= 3) flat_back += back_range >= front_range;
        }
        CHECK(two == 0 && wrong == 0, "band %d: at most one cloud steps per frame, the one returned (%d frames with two, %d other)", band,
              two, wrong);
        CHECK(bad_step == 0 && out == 0, "band %d: 1 px steps within ±%d, ±%d px (%d bigger steps, %d past it)", band, WX_DRIFT_X,
              WX_DRIFT_Y, bad_step, out);
        CHECK(lag_even <= 2 && worst_lag <= 2, "band %d: at most 2 px behind the path, ticked at %d fps or at 15-60 fps (%d, %d)", band,
              WX_DRIFT_FPS, lag_even, worst_lag);
        CHECK(off_extreme == 0, "band %d: every cloud reaches both ends of its path (%d short)", band, off_extreme);
        CHECK(against == 0 && off_mean == 0, "band %d: every cloud leans with the shared wind, mean at depth x lean (%d against, %d off)",
              band, against, off_mean);
        CHECK(flat_back == 0, "band %d: back clouds swing less than front clouds (%d seeds not)", band, flat_back);
        printf("drift: band %d, %d cloud(s): %.1f steps/s, one box of %ld px each (%ld px/s; the old single drift was 1 step/s)\n", band, n,
               steps / (60.0 * 32), steps ? box_px / steps : 0, steps ? (long)(box_px / (60.0 * 32)) : 0);
    }
    CHECK(leans[0] > 10 && leans[1] > 10, "drift: the wind blows either way across shows (%d left, %d right)", leans[0], leans[1]);

    wx_drift_start(&d, 4, NULL, 7);
    wx_drift_t a = d;
    wx_drift_start(&d, 4, NULL, 7);
    bool differ = false;
    for (int i = 0; i < 4; i++) differ |= a.period[i] != d.period[i] || a.phase_x[i] != d.phase_x[i];
    CHECK(differ && d.starts == a.starts + 1, "drift: a second show with the same entropy takes new paths");
    wx_drift_t e = d;
    wx_drift_start(&e, 4, NULL, 8);
    differ = false;
    for (int i = 0; i < 4; i++) differ |= e.period[i] != d.period[i];
    CHECK(differ, "drift: other entropy, other paths");
    for (int i = 0; i < 4; i++) CHECK(d.depth[i] == 1, "drift: no depths, all front");

    wx_drift_start(&d, 4, NULL, 99);
    int moved = 0;
    for (int i = 0; i < 4; i++) {
        wx_drift_path(&d, i, 0, &px, &py);
        moved += px || py || d.dx[i] || d.dy[i];
    }
    CHECK(moved == 0 && d.t == 0 && wx_drift_step(&d) == -1, "drift: a start puts every cloud at rest on a clock at 0, the first frame steps none");
    wx_drift_tick(&d, NAN);
    wx_drift_tick(&d, INFINITY);
    wx_drift_tick(&d, -1);
    CHECK(d.t == 0 && wx_drift_step(&d) == -1, "drift: NaN, infinity or a negative dt leave the clock");
    wx_drift_tick(&d, 1);
    int first = wx_drift_step(&d);
    CHECK(first >= 0 && wx_drift_step(&d) == -1, "drift: one step per frame, none again on the same frame");

    wx_drift_start(&d, 4, NULL, 5);
    for (int f = 0; f < 2 * 3600 * WX_DRIFT_FPS; f++) {
        wx_drift_tick(&d, 1.0 / WX_DRIFT_FPS);
        wx_drift_step(&d);
    }
    int after = 0;
    for (int f = 0; f < 2 * WX_DRIFT_FPS; f++) {
        wx_drift_tick(&d, 1.0 / WX_DRIFT_FPS);
        after += wx_drift_step(&d) >= 0;
    }
    CHECK(after >= 2, "drift: after 2 h and a hide/show that keeps the scene (the face's s_t back to 0) the clouds keep moving (%d steps in 2 s)",
          after);

    wx_drift_start(&d, 0, NULL, 1);
    wx_drift_tick(&d, 5);
    CHECK(d.n == 0 && wx_drift_step(&d) == -1, "drift: no clouds, no steps");
    wx_drift_start(&d, 9, NULL, 1);
    CHECK(d.n == WX_CLOUDS_MAX, "drift: at most %d paths", WX_CLOUDS_MAX);
}

static void strip(const char *dir)
{
    static uint16_t bg[WX_FACE * WX_FACE], lut[WX_RING_LUT];
    static uint8_t hero[WX_HERO_BOX * WX_HERO_BOX * 3];
    static wx_ring_t ring;
    enum { Y0 = 40, H = 170, FRAMES = 6 };
    static const double AT[FRAMES] = {0, 1.5, 3, 4.5, 6, 7.5};
    uint8_t *scene = malloc((size_t)wx_scene_px() * 3);
    unsigned char *img = malloc((size_t)WX_FACE * H * FRAMES * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .scene = scene};
    for (int band = 1; band <= 4; band++) {
        wx_temp_t t = {.have = true, .lo = 6, .hi = 15, .cloud_band = band, .hero = band < 3 ? WX_HERO_SUN : WX_HERO_NONE};
        wx_bake_out_t o;
        wx_bake(&t, WX_BAKE_FULL, &b, &o);
        wx_drift_t d = {0};
        wx_drift_start(&d, o.nclouds, o.cloud_back, 212);
        for (int f = 0; f < FRAMES; f++) {
            while (d.t < AT[f]) {
                wx_drift_tick(&d, 1.0 / WX_DRIFT_FPS);
                wx_drift_step(&d);
            }
            for (int y = 0; y < H; y++)
                for (int x = 0; x < WX_FACE; x++) {
                    uint32_t c = rgb_of(bg[(Y0 + y) * WX_FACE + x]);
                    float acc[4] = {(float)((c >> 16) & 255), (float)((c >> 8) & 255), (float)(c & 255), 1};
                    if (o.hero.x1 > o.hero.x0) over(hero, o.hero, x, Y0 + y, acc);
                    for (int i = 0; i < o.nclouds; i++) {
                        wx_box_t q = o.cloud[i];
                        over(scene + o.cloud_at[i], q, x - d.dx[i], Y0 + y - d.dy[i], acc);
                    }
                    unsigned char *p = img + (((size_t)(f * H + y)) * WX_FACE + x) * 3;
                    for (int k = 0; k < 3; k++) p[k] = (unsigned char)lroundf(acc[k]);
                }
        }
        char path[512];
        snprintf(path, sizeof path, "%s/temp-drift-band%d.ppm", dir, band);
        FILE *fp = fopen(path, "wb");
        if (!fp) continue;
        fprintf(fp, "P6 %d %d 255\n", WX_FACE, H * FRAMES);
        fwrite(img, 1, (size_t)WX_FACE * H * FRAMES * 3, fp);
        fclose(fp);
        printf("temp: frame strip %s (t 0-7.5 s every 1.5 s, rows %d-%d)\n", path, Y0, Y0 + H);
    }
    free(scene);
    free(img);
}

int main(void)
{
    test_scale();
    test_uv();
    test_face_from();
    test_same_bake();
    test_bake_key();
    test_count();
    test_geometry();
    test_clouds();
    test_cloud_path();
    test_paint();
    test_shift();
    test_bake();
    test_drift();
    if (getenv("CINDER_TEMP_PPM")) strip(getenv("CINDER_TEMP_PPM"));
    if (failures) {
        printf("temp: %d failure(s)\n", failures);
        return 1;
    }
    printf("temp: all tests passed\n");
    return 0;
}
