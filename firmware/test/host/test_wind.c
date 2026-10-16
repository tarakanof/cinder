#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_faces.h"
#include "wx_paint.h"
#include "wx_temp.h"
#include "wx_wind.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NOW 1782043200LL

static wx_obs_t windy(int kmh, int dir)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = 12, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .moon_up = -1, .observed_at = NOW - 300, .has_obs_age = true, .obs_age_s = 300,
                  .has_rain = true, .has_utc_offset = true, .utc_offset = 3600, .has_now_rain = true,
                  .has_wind = true, .wind_kmh = kmh, .has_wind_dir = dir >= 0, .wind_dir = dir >= 0 ? dir : 0};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "clouds");
    snprintf(o.code, sizeof o.code, "3");
    return o;
}

static bool near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void test_bands(void)
{
    static const struct {
        int kmh;
        uint32_t rgb;
        const char *name;
    } C[] = {
        {0, 0x9AA4B0, "Calm"},          {1, 0x9AA4B0, "Light air"},       {4, 0x9AA4B0, "Light air"},
        {5, 0x9AA4B0, "Light air"},     {6, 0x4FD6CC, "Light breeze"},    {11, 0x4FD6CC, "Light breeze"},
        {12, 0x8ACF7A, "Gentle breeze"}, {18, 0x8ACF7A, "Gentle breeze"}, {19, 0x8ACF7A, "Gentle breeze"},
        {20, 0xC9D25A, "Moderate breeze"}, {28, 0xC9D25A, "Moderate breeze"}, {29, 0xE8D44A, "Fresh breeze"},
        {38, 0xE8D44A, "Fresh breeze"}, {39, 0xF08A3A, "Strong breeze"}, {45, 0xF08A3A, "Strong breeze"},
        {49, 0xF08A3A, "Strong breeze"}, {50, 0xFF5050, "Near gale"},   {61, 0xFF5050, "Near gale"},
        {62, 0xD03060, "Gale"},         {74, 0xD03060, "Gale"},           {75, 0xD03060, "Strong gale"},
        {88, 0xD03060, "Strong gale"},  {89, 0xD03060, "Storm"},          {102, 0xD03060, "Storm"},
        {103, 0xD03060, "Violent storm"}, {117, 0xD03060, "Violent storm"}, {118, 0xD03060, "Hurricane"},
        {999, 0xD03060, "Hurricane"},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++)
        CHECK(wx_wind_rgb(C[i].kmh) == C[i].rgb && !strcmp(wx_wind_band(C[i].kmh), C[i].name), "%d km/h: %06X %s, want %06X %s",
              C[i].kmh, (unsigned)wx_wind_rgb(C[i].kmh), wx_wind_band(C[i].kmh), (unsigned)C[i].rgb, C[i].name);
    CHECK(wx_wind_dark(0x8ACF7A) == 0x4C7243 && wx_wind_dark(0x9AA4B0) == 0x555A61 && wx_wind_dark(0xF08A3A) == 0x844C20 &&
              wx_wind_dark(0xD03060) == 0x721A35,
          "the dark stripes as the canvas's shade(): %06X", (unsigned)wx_wind_dark(0xF08A3A));
}

static void test_compass(void)
{
    static const struct {
        int dir;
        const char *p;
    } C[] = {{0, "N"},    {22, "N"},  {23, "NE"}, {45, "NE"}, {67, "NE"},  {68, "E"},   {90, "E"},   {135, "SE"},
             {180, "S"},  {202, "S"}, {203, "SW"}, {225, "SW"}, {270, "W"}, {315, "NW"}, {337, "NW"}, {338, "N"},
             {359, "N"},  {360, "N"}, {-45, "NW"}, {-1, "N"},  {810, "E"}};
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++)
        CHECK(!strcmp(wx_wind_compass(C[i].dir), C[i].p), "%d degrees: %s, want %s", C[i].dir, wx_wind_compass(C[i].dir), C[i].p);
}

static void test_levels(void)
{
    CHECK(wx_wind_level(0) == 0 && wx_wind_level(4) == 0 && wx_wind_level(11) == 0, "limp up to a light breeze");
    CHECK(wx_wind_level(12) == 1 && wx_wind_level(18) == 1 && wx_wind_level(28) == 1, "gentle: gentle and moderate breeze");
    CHECK(wx_wind_level(29) == 2 && wx_wind_level(45) == 2 && wx_wind_level(999) == 2, "straight out from a fresh breeze");
    static const struct {
        int kmh, shown, want;
    } K[] = {{14, 0, 0}, {15, 0, 1}, {9, 1, 1}, {8, 1, 0}, {31, 1, 1}, {32, 1, 2}, {26, 2, 2}, {25, 2, 1},
             {40, 0, 2}, {3, 2, 0}, {14, -1, 1}, {14, 3, 1}, {12, 1, 1}, {0, 0, 0}};
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++)
        CHECK(wx_wind_level_keep(K[i].kmh, K[i].shown) == K[i].want, "%d km/h with level %d shown: %d, want %d", K[i].kmh,
              K[i].shown, wx_wind_level_keep(K[i].kmh, K[i].shown), K[i].want);
}

static void test_from(void)
{
    static const struct {
        int kmh, dir, level;
        const char *sub;
    } C[] = {
        {18, 225, 1, "#8ACF7A Gentle breeze# \xE2\x80\xA2 SW"},
        {4, 45, 0, "#9AA4B0 Light air# \xE2\x80\xA2 NE"},
        {45, 270, 2, "#F08A3A Strong breeze# \xE2\x80\xA2 W"},
        {0, 90, 0, "#9AA4B0 Calm#"},
        {21, -1, 1, "#C9D25A Moderate breeze#"},
        {130, 315, 2, "#D03060 Hurricane# \xE2\x80\xA2 NW"},
        {25, 359, 1, "#C9D25A Moderate breeze# \xE2\x80\xA2 N"},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        wx_obs_t o = windy(C[i].kmh, C[i].dir);
        wx_wind_t w;
        wx_wind_from(&o, NULL, &w);
        CHECK(w.on && w.have && w.kmh == C[i].kmh && w.level == C[i].level && !strcmp(w.sub, C[i].sub), "%d km/h %d: level %d '%s'",
              C[i].kmh, C[i].dir, w.level, w.sub);
    }
    wx_obs_t o = windy(18, 225);
    wx_wind_t w;
    wx_wind_from(&o, NULL, &w);
    CHECK(near(w.frac, 0.36f, 1e-6f) && w.has_dir && w.dir == 225, "AW2: the marker at 18 of 50 (%.3f)", w.frac);
    o = windy(50, 0);
    wx_wind_from(&o, NULL, &w);
    CHECK(w.frac == 1, "50: the gauge's end");
    o = windy(130, 0);
    wx_wind_from(&o, NULL, &w);
    CHECK(w.frac == 1 && w.kmh == 130, "130: the value as it is, the marker at 50+");
    o = windy(5000, 0);
    wx_wind_from(&o, NULL, &w);
    CHECK(w.kmh == WX_WIND_MAX_KMH, "clamped to %d", w.kmh);
    o = windy(-3, 0);
    wx_wind_from(&o, NULL, &w);
    CHECK(w.kmh == 0 && w.frac == 0 && !strcmp(w.sub, "#9AA4B0 Calm#"), "a negative speed is calm");
    o = windy(10, 720 + 225);
    wx_wind_from(&o, NULL, &w);
    CHECK(w.dir == 225, "direction wraps: %d", w.dir);
    o = windy(10, 0);
    o.wind_dir = -90;
    wx_wind_from(&o, NULL, &w);
    CHECK(w.dir == 270, "a negative direction wraps: %d", w.dir);

    static const char *const LONG[] = {"Moderate breeze", "Violent storm"};
    for (int i = 0; i < 2; i++) {
        char want[64];
        snprintf(want, sizeof want, "#D03060 %s# \xE2\x80\xA2 NW", LONG[i]);
        CHECK(strlen(want) < sizeof w.sub, "'%s' fits the sub-line buffer (%zu)", want, strlen(want));
    }
    o = windy(110, 315);
    wx_wind_from(&o, NULL, &w);
    CHECK(!strcmp(w.sub, "#D03060 Violent storm# \xE2\x80\xA2 NW"), "the longest band with a direction: '%s'", w.sub);

    wx_wind_t shown;
    o = windy(18, 225);
    wx_wind_from(&o, NULL, &shown);
    o = windy(10, 225);
    wx_wind_from(&o, &shown, &w);
    CHECK(w.level == 1, "10 km/h with the gentle sock shown: kept");
    shown.have = false;
    wx_wind_from(&o, &shown, &w);
    CHECK(w.level == 0, "a shown face without data keeps nothing");
    shown.have = true;
    shown.on = false;
    wx_wind_from(&o, &shown, &w);
    CHECK(w.level == 0, "nor another face");

    o = windy(18, 225);
    o.has_wind = false;
    wx_wind_from(&o, NULL, &w);
    CHECK(w.on && !w.have && w.level == 0 && !w.sub[0] && w.frac == 0, "no wind_kmh: no data");
    o = windy(18, 225);
    o.stale = true;
    wx_wind_from(&o, NULL, &w);
    CHECK(w.on && !w.have, "stale: no data");
    o = windy(18, 225);
    o.age_s = WX_MAX_AGE_S + 1;
    wx_wind_from(&o, NULL, &w);
    CHECK(!w.have, "old: no data");
    o = windy(18, 225);
    o.enabled = false;
    wx_wind_from(&o, NULL, &w);
    CHECK(!w.have, "disabled: no data");
    o = windy(18, 225);
    o.valid = false;
    wx_wind_from(&o, NULL, &w);
    CHECK(!w.have, "invalid: no data");
    wx_wind_from(NULL, NULL, &w);
    CHECK(w.on && !w.have, "NULL: no data");
    CHECK(!wx_wind_strong(NULL, false) && !wx_wind_strong(NULL, true), "NULL is never strong");
}

static void test_auto(void)
{
    wx_temp_t t, shown;
    wx_obs_t o = windy(39, 270);
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_WIND && t.wind.on && t.wind.have && !t.rain.on && !t.cast.on, "39 km/h: the Wind face is the auto face");
    o = windy(38, 270);
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP && !t.wind.on, "38 km/h: Temperature");
    o = windy(45, 270);
    wx_temp_face(&o, NULL, WXF_AUTO, &shown);
    o = windy(34, 270);
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_WIND, "34 km/h with the Wind face shown: it stays");
    o = windy(33, 270);
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "33 km/h: Temperature again");
    wx_temp_face(&o, NULL, WXF_TEMP, &shown);
    o = windy(34, 270);
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "no hold with another face on screen");

    o = windy(80, 270);
    snprintf(o.condition, sizeof o.condition, "rain");
    o.now_rain = 2;
    o.rain_n = 1;
    o.rain_spell[0] = (wx_spell_t){NOW - 900, NOW + 3600, 2, 0};
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_RAIN && t.rain.on && !t.wind.on, "wet in a gale: Rain now wins");
    o = windy(80, 270);
    o.now_rain = 0;
    o.rain_n = 1;
    o.rain_spell[0] = (wx_spell_t){NOW + 1800, NOW + 3600, 2, 0};
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_CAST && !t.wind.on, "rain in 30 min in a gale: the forecast first");
    o = windy(80, 270);
    o.stale = true;
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "stale: never the auto Wind face");
    o = windy(80, 270);
    o.has_wind = false;
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "no wind data: never the auto Wind face");

    o = windy(3, 0);
    wx_temp_face(&o, NULL, WXF_WIND, &t);
    CHECK(t.face == WXF_WIND && t.wind.on && t.wind.have && t.wind.level == 0 && !t.rain.on && !t.cast.on, "picked when calm");
    o = windy(60, 0);
    wx_temp_face(&o, NULL, WXF_TEMP, &t);
    CHECK(t.face == WXF_TEMP && !t.wind.on && !t.wind.have && !t.wind.sub[0], "Temperature picked in a gale carries no wind");
    wx_temp_face(&o, NULL, WXF_CAST, &t);
    CHECK(t.face == WXF_CAST && t.cast.on && !t.wind.on, "the forecast picked in a gale carries no wind");
    wx_temp_face(&o, NULL, WXF_RAIN, &t);
    CHECK(t.face == WXF_RAIN && t.rain.dry && !t.wind.on, "Rain now picked in a gale: dry, no wind");
    o = windy(10, 0);
    snprintf(o.condition, sizeof o.condition, "rain");
    o.now_rain = 2;
    o.rain_n = 1;
    o.rain_spell[0] = (wx_spell_t){NOW - 900, NOW + 3600, 2, 0};
    wx_temp_face(&o, NULL, WXF_WIND, &t);
    CHECK(t.face == WXF_WIND && t.wind.on && !t.rain.on, "Wind picked while wet: the wind, no rain");
    wx_temp_face(NULL, NULL, WXF_WIND, &t);
    CHECK(t.face == WXF_WIND && t.wind.on && !t.wind.have, "picked without data");

    o = windy(18, 0);
    wx_temp_face(&o, NULL, WXF_WIND, &shown);
    o = windy(10, 0);
    wx_temp_face(&o, &shown, WXF_WIND, &t);
    CHECK(t.wind.level == 1, "a picked Wind face keeps its level through wx_temp_face");
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP && !t.wind.on, "auto at 10 km/h: Temperature");
}

static void test_bake_kind(void)
{
    wx_temp_t a, b;
    wx_obs_t o = windy(18, 225);
    wx_temp_face(&o, NULL, WXF_WIND, &a);
    o = windy(24, 90);
    wx_temp_face(&o, &a, WXF_WIND, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == 0, "speed and direction in the level: no bake (value, colour, marker, text)");
    o = windy(24, 90);
    o.has_wind_dir = false;
    wx_temp_face(&o, &a, WXF_WIND, &b);
    CHECK(!b.wind.has_dir && wx_temp_bake_kind(&b, &a) == 0 && wx_temp_bake_kind(&a, &b) == 0,
          "the direction coming or going: no bake, only the sub-line");
    o = windy(9, 90);
    wx_temp_face(&o, &a, WXF_WIND, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == 0, "9 km/h with the gentle sock shown: kept, no bake");
    o = windy(8, 90);
    wx_temp_face(&o, &a, WXF_WIND, &b);
    CHECK(b.wind.level == 0 && wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "8 km/h: the limp sock, a full scene");
    o = windy(40, 90);
    wx_temp_face(&o, &a, WXF_WIND, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "a stronger level: a full scene");
    o = windy(18, 225);
    o.stale = true;
    wx_temp_face(&o, &a, WXF_WIND, &b);
    CHECK(!b.wind.have && wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "data going: a full scene (grey ring, labels)");
    o = windy(18, 225);
    wx_temp_face(&o, NULL, WXF_WIND, &a);
    o.has_wind = false;
    wx_temp_face(&o, &a, WXF_WIND, &b);
    a.wind.level = 0;
    CHECK(!b.wind.have && b.wind.level == 0 && wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "data going at the same level: still full");
    wx_temp_face(&o, NULL, WXF_TEMP, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL && wx_temp_bake_kind(&a, &b) == WX_BAKE_FULL, "face change: full");
    b = a;
    b.face = WXF_CAST;
    CHECK(wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "the face id alone is a full scene");
    wx_wind_t x = a.wind, y = a.wind;
    y.on = false;
    CHECK(!wx_wind_same_scene(&x, &y) && !wx_wind_same_scene(&y, &x), "on differs");
    x.on = y.on = false;
    x.level = 2;
    CHECK(wx_wind_same_scene(&x, &y), "both off: the rest does not matter");
}

static void test_geometry(void)
{
    static const struct {
        int level, pose, quad;
        float p[4][2];
    } R[] = {
        {0, 0, 4, {{268.15f, 171.34f}, {269.64f, 195.89f}, {261.05f, 197.05f}, {257.15f, 172.82f}}},
        {0, 1, 0, {{261.17f, 68.8f}, {266.73f, 95.93f}, {233.79f, 105.27f}, {219.43f, 87.2f}}},
        {0, 2, 4, {{253.64f, 176.42f}, {251.65f, 201.11f}, {243.39f, 200.52f}, {243.07f, 175.65f}}},
        {1, 0, 4, {{293.59f, 136.75f}, {308.36f, 157.54f}, {302.11f, 163.55f}, {284.18f, 148.22f}}},
        {1, 1, 0, {{211.98f, 80.65f}, {232.83f, 94.16f}, {214.67f, 130.11f}, {191.42f, 121.35f}}},
        {1, 2, 4, {{291.09f, 151.38f}, {303.99f, 173.8f}, {297.09f, 178.41f}, {280.46f, 160.63f}}},
        {2, 0, 4, {{286.26f, 83.6f}, {310.32f, 85.05f}, {311.34f, 104.41f}, {287.55f, 108.37f}}},
        {2, 1, 0, {{191.2f, 78.2f}, {215.9f, 80.86f}, {215.9f, 121.14f}, {191.2f, 123.8f}}},
        {2, 2, 4, {{293.44f, 94.67f}, {318.71f, 98.55f}, {317.75f, 116.76f}, {292.21f, 117.98f}}},
    };
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++) {
        wx_pt_t p[4];
        wx_wind_quad(R[i].level, R[i].pose, R[i].quad, p);
        for (int k = 0; k < 4; k++)
            CHECK(near(p[k].x, R[i].p[k][0], 0.02f) && near(p[k].y, R[i].p[k][1], 0.02f),
                  "level %d pose %d quad %d point %d (%.2f, %.2f), canvas (%.2f, %.2f)", R[i].level, R[i].pose, R[i].quad, k, p[k].x,
                  p[k].y, R[i].p[k][0], R[i].p[k][1]);
    }
    CHECK(wx_wind_geo(-1) == wx_wind_geo(0) && wx_wind_geo(7) == wx_wind_geo(2), "levels outside clamp");
    CHECK(wx_wind_geo(0)->nrows == 1 && wx_wind_geo(1)->nrows == 3 && wx_wind_geo(2)->nrows == 4, "streak rows: 1, 3, 4");
    CHECK(wx_wind_geo(0)->nleaves == 0 && wx_wind_geo(1)->nleaves == 0 && wx_wind_geo(2)->nleaves == 2, "leaves only at the strong level");
    CHECK(wx_wind_streak_x(1, 0) == 228 && wx_wind_streak_y(1, 0) == 64, "AW2's first streak from x 231.7 at y 67: sprite at %d,%d",
          wx_wind_streak_x(1, 0), wx_wind_streak_y(1, 0));
}

static void test_cycle(void)
{
    static const int MS[WX_WIND_LEVELS] = {667, 333, 133};
    for (int lv = 0; lv < WX_WIND_LEVELS; lv++) {
        const wx_wind_geo_t *g = wx_wind_geo(lv);
        CHECK((int)lround(g->pose_frames * 1000.0 / WX_WIND_FPS) == MS[lv], "level %d: a pose every %d ms", lv, MS[lv]);
        int seq[4] = {-1, -1, -1, -1}, runs = 0, prev = -1;
        for (int k = 0; k < 4 * g->pose_frames; k++) {
            wx_wind_frame_t f;
            wx_wind_frame(lv, k, &f);
            if (f.pose != prev) {
                if (runs < 4) seq[runs] = f.pose;
                runs++;
                prev = f.pose;
            }
            CHECK(f.pose == seq[k / g->pose_frames], "level %d frame %d: pose %d", lv, k, f.pose);
        }
        CHECK(runs == 4 && seq[0] == 0 && seq[1] == 1 && seq[2] == 2 && seq[3] == 1, "level %d: A-B-C-B (%d %d %d %d)", lv, seq[0],
              seq[1], seq[2], seq[3]);
        wx_wind_frame_t f;
        wx_wind_frame(lv, 4 * g->pose_frames, &f);
        CHECK(f.pose == 0, "level %d: the cycle repeats", lv);
        wx_wind_frame(lv, -1, &f);
        int on = 0;
        for (int r = 0; r < WX_WIND_ROWS; r++) on += f.streak_opa[r] > 0;
        for (int i = 0; i < WX_WIND_LEAVES; i++) on += f.leaf_opa[i] > 0;
        CHECK(f.pose == 1 && on == 0, "still: pose B, no streaks or leaves");
    }
}

static void test_streaks(void)
{
    static const struct {
        int level, row, k, dx, opa;
    } C[] = {
        {1, 0, 0, -24, 0},  {1, 0, 6, -4, 96},  {1, 0, 11, 13, 175}, {1, 0, 12, 16, 191}, {1, 0, 18, 36, 96},
        {1, 0, 23, 53, 16}, {1, 0, 24, -24, 0}, {1, 1, 0, -1, 112},  {2, 0, 3, -4, 96},   {2, 3, 0, 36, 96},
        {2, 0, 6, 16, 191}, {0, 0, 11, -6, 87},  {0, 0, 22, 14, 183}, {0, 0, 23, 16, 191}, {0, 0, 45, -24, 0}, {1, 0, 2424, -24, 0},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        wx_wind_frame_t f;
        wx_wind_frame(C[i].level, C[i].k, &f);
        CHECK(f.streak_dx[C[i].row] == C[i].dx && f.streak_opa[C[i].row] == C[i].opa, "level %d row %d frame %d: dx %d opa %d, want %d %d",
              C[i].level, C[i].row, C[i].k, f.streak_dx[C[i].row], f.streak_opa[C[i].row], C[i].dx, C[i].opa);
    }
    wx_wind_frame_t f;
    wx_wind_frame(1, 5, &f);
    CHECK(f.streak_opa[3] == 0 && f.streak_dx[3] == 0, "rows past the level's count stay off");
}

static void test_leaves(void)
{
    const wx_wind_geo_t *g = wx_wind_geo(2);
    wx_wind_frame_t f;
    wx_wind_frame(2, 0, &f);
    CHECK(f.leaf_opa[0] == 0 && f.leaf_x[0] == (int)lroundf(g->mount.x + 2) - WX_LEAF_BOX / 2 &&
              f.leaf_y[0] == (int)lroundf(g->mount.y - 10) - WX_LEAF_BOX / 2 && f.leaf_rot[0] == 0,
          "leaf 0 starts hidden at the hoop: %d,%d", f.leaf_x[0], f.leaf_y[0]);
    CHECK(f.leaf_opa[1] == 255 && f.leaf_rot[1] == 9 && f.leaf_x[1] == (int)lround(g->mount.x + 2 + 70 * 10.0 / 12) - 10,
          "leaf 1 starts 0.7 s in: rot %d x %d", f.leaf_rot[1], f.leaf_x[1]);
    wx_wind_frame(2, 9, &f);
    CHECK(f.leaf_opa[0] == 255 && f.leaf_rot[0] == 11 && f.leaf_x[0] == (int)lroundf(g->mount.x + 72) - 10 &&
              f.leaf_y[0] == (int)lroundf(g->mount.y - 20) - 10,
          "leaf 0 half way: 70 px on, 10 up, 160 degrees (%d)", f.leaf_rot[0]);
    wx_wind_frame(2, 1, &f);
    CHECK(f.leaf_opa[0] > 0 && f.leaf_opa[0] < 255, "fading in (%d)", f.leaf_opa[0]);
    wx_wind_frame(2, 17, &f);
    CHECK(f.leaf_opa[0] > 0 && f.leaf_opa[0] < 255 && f.leaf_rot[0] == ((int)lround((160 + 170 * 8.0 / 9) / 15)) % WX_LEAF_ROTS,
          "fading out at the end, turned %d", f.leaf_rot[0]);
    wx_wind_frame(2, 18, &f);
    CHECK(f.leaf_opa[0] == 0, "leaf 0 loops every 1.2 s");
    wx_wind_frame(1, 9, &f);
    CHECK(f.leaf_opa[0] == 0 && f.leaf_opa[1] == 0, "no leaves below the strong level");
}

static int box_px(wx_box_t b) { return (b.x1 - b.x0) * (b.y1 - b.y0); }

static bool inside(wx_box_t a, wx_box_t b) { return a.x0 >= b.x0 && a.y0 >= b.y0 && a.x1 <= b.x1 && a.y1 <= b.y1; }

static void test_layout(void)
{
    int scene = wx_scene_px() * 3;
    for (int lv = 0; lv < WX_WIND_LEVELS; lv++) {
        wx_wind_layout_t l;
        wx_wind_layout(lv, &l);
        CHECK(l.total <= scene, "level %d: layers %d B fit the scene buffer %d B", lv, l.total, scene);
        CHECK(inside(l.sock, l.area), "level %d: the sock inside the area", lv);
        CHECK(!(l.area.x0 & 1) && !(l.area.y0 & 1) && !(l.area.x1 & 1) && !(l.area.y1 & 1) && !(l.sock.x0 & 1) && !(l.sock.y0 & 1),
              "level %d: even boxes", lv);
        for (int p = 0; p < WX_WIND_POSES; p++)
            for (int q = 0; q < WX_WIND_QUADS; q++) {
                wx_pt_t pt[4];
                wx_wind_quad(lv, p, q, pt);
                for (int i = 0; i < 4; i++)
                    CHECK(pt[i].x >= l.sock.x0 + 1 && pt[i].x <= l.sock.x1 - 1 && pt[i].y >= l.sock.y0 + 1 && pt[i].y <= l.sock.y1 - 1,
                          "level %d pose %d quad %d inside the sock box with a margin", lv, p, q);
            }
        float far = 0;
        int cx[2] = {l.area.x0, l.area.x1}, cy[2] = {l.area.y0, l.area.y1};
        for (int i = 0; i < 4; i++) far = fmaxf(far, hypotf((float)cx[i & 1] - WX_FACE_C, (float)cy[i >> 1] - WX_FACE_C));
        CHECK(far < 210, "level %d: the area's corners stay inside r 210 (%.1f), off the ring", lv, far);
        CHECK(l.area.y1 <= 212, "level %d: the area ends above the value (y %d)", lv, l.area.y1);
        int ok = 1;
        for (int k = 0; k < 1200; k++) {
            wx_wind_frame_t f;
            wx_wind_frame(lv, k, &f);
            for (int r = 0; r < l.nrows; r++) {
                int x = wx_wind_streak_x(lv, r) + f.streak_dx[r], y = wx_wind_streak_y(lv, r);
                if (f.streak_opa[r] && !inside((wx_box_t){x, y, x + l.streak_w[r], y + WX_STREAK_H}, l.area)) ok = 0;
            }
            for (int i = 0; i < l.nleaves; i++)
                if (f.leaf_opa[i] && !inside((wx_box_t){f.leaf_x[i], f.leaf_y[i], f.leaf_x[i] + WX_LEAF_BOX, f.leaf_y[i] + WX_LEAF_BOX}, l.area))
                    ok = 0;
        }
        CHECK(ok, "level %d: every streak and leaf stays in the area for 80 s", lv);
        printf("wind: level %d, sock box %dx%d, area %d,%d-%d,%d (%d px), layers %d of %d B\n", lv, l.sock.x1 - l.sock.x0,
               l.sock.y1 - l.sock.y0, l.area.x0, l.area.y0, l.area.x1, l.area.y1, box_px(l.area), l.total, scene);
    }
}

static void test_areas(void)
{
    for (int lv = 0; lv < WX_WIND_LEVELS; lv++) {
        wx_wind_layout_t l;
        wx_wind_layout(lv, &l);
        wx_wind_frame_t p, f;
        wx_wind_frame(lv, 0, &p);
        int dirty = 0, n = 0;
        for (int k = 1; k <= 900; k++, n++) {
            wx_wind_frame(lv, k, &f);
            bool d = f.pose != p.pose;
            for (int r = 0; r < l.nrows; r++) d |= f.streak_dx[r] != p.streak_dx[r] || f.streak_opa[r] != p.streak_opa[r];
            for (int i = 0; i < l.nleaves; i++)
                d |= f.leaf_x[i] != p.leaf_x[i] || f.leaf_y[i] != p.leaf_y[i] || f.leaf_rot[i] != p.leaf_rot[i] || f.leaf_opa[i] != p.leaf_opa[i];
            dirty += d;
            p = f;
        }
        printf("wind: level %d, 1 area on %d of %d frames at 15 fps, %d px each, avg %d px per frame\n", lv, dirty, n, box_px(l.area),
               box_px(l.area) * dirty / n);
        CHECK(dirty > n / 2, "level %d: the box animates", lv);
    }
}

static uint32_t rgb_of(uint16_t c) { return ((uint32_t)(c >> 11) << 19) | ((uint32_t)((c >> 5) & 63) << 10) | ((uint32_t)(c & 31) << 3); }

static void test_paint(void)
{
    static wx_ring_t ring;
    static uint16_t bg[WX_FACE * WX_FACE], lut[WX_RING_LUT];
    size_t scene = (size_t)wx_scene_px() * 3;
    uint8_t *m = calloc(1, scene), *hero = calloc(1, WX_HERO_BOX * WX_HERO_BOX * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .scene = m};
    for (int lv = 0; lv < WX_WIND_LEVELS; lv++) {
        static const int KMH[WX_WIND_LEVELS] = {4, 18, 45};
        wx_obs_t o = windy(KMH[lv], 225);
        wx_temp_t t;
        wx_temp_face(&o, NULL, WXF_WIND, &t);
        CHECK(t.wind.level == lv, "level %d from %d km/h", lv, KMH[lv]);
        wx_bake_out_t out = {.hero = {1, 1, 2, 2}, .nclouds = 1};
        memset(m, 0, scene);
        clock_t t0 = clock();
        wx_bake(&t, WX_BAKE_FULL, &b, &out);
        printf("wind: level %d host bake %.2f ms\n", lv, (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
        CHECK(out.hero.x1 == 0 && out.nclouds == 0, "no hero or cloud sprites");
        const wx_wind_geo_t *g = wx_wind_geo(lv);
        float px = g->mount.x - WX_WIND_ARM;
        CHECK(bg[(int)(g->mount.y + 20) * WX_FACE + (int)px] == wx_rgb565(WX_WIND_POLE_RGB), "level %d: the pole", lv);
        int across = 0;
        for (int x = (int)px - 10; x <= (int)px + 10; x++) across += bg[(int)(g->mount.y + 20) * WX_FACE + x] != 0;
        CHECK(across >= 7 && across <= 8, "level %d: 7 px wide (%d px inked)", lv, across);
        CHECK(bg[(int)g->mount.y * WX_FACE + (int)(px + 12)] == wx_rgb565(WX_WIND_POLE_RGB), "level %d: the arm to the hoop", lv);
        CHECK(bg[202 * WX_FACE + (int)(px - 20)] == wx_rgb565(WX_WIND_POLE_RGB) && bg[202 * WX_FACE + (int)(px - 26)] == 0,
              "level %d: the base, 48 px", lv);
        CHECK(bg[(int)(g->mount.y - WX_WIND_POLE_TOP - 4) * WX_FACE + (int)px] == wx_rgb565(WX_WIND_POLE_RGB) &&
                  bg[(int)(g->mount.y - WX_WIND_POLE_TOP - 7) * WX_FACE + (int)px] == 0,
              "level %d: the knob on top", lv);
        float dx, dy;
        wx_face_dot_xy(WXF_WIND, WXF_N, &dx, &dy);
        CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_ON), "the Wind face's dot is white");
        wx_face_dot_xy(WXF_TEMP, WXF_N, &dx, &dy);
        CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_OFF), "the others grey");

        wx_wind_layout_t l;
        wx_wind_layout(lv, &l);
        int w = l.sock.x1 - l.sock.x0;
        for (int p = 0; p < WX_WIND_POSES; p++)
            for (int q = 0; q < WX_WIND_QUADS; q++) {
                wx_pt_t pt[4];
                wx_wind_quad(lv, p, q, pt);
                float cx = 0, cy = 0;
                for (int i = 0; i < 4; i++) {
                    cx += pt[i].x / 4;
                    cy += pt[i].y / 4;
                }
                int i = ((int)cy - l.sock.y0) * w + ((int)cx - l.sock.x0);
                CHECK(m[l.fill[p] + i] == ((q & 1) ? 0 : 255) && m[l.dark[p] + i] == ((q & 1) ? 255 : 0), "level %d pose %d stripe %d: fill %d dark %d", lv,
                      p, q, m[l.fill[p] + i], m[l.dark[p] + i]);
            }
        CHECK(m[l.fill[1]] == 0 && m[l.fill[1] + w - 1] == 0, "level %d: box corners clear", lv);
        {
            wx_pt_t pt[4];
            wx_wind_quad(lv, 0, 1, pt);
            int sx = (int)((pt[0].x + pt[3].x) / 2), sy = (int)((pt[0].y + pt[3].y) / 2), seam = 0, solid = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int i = (sy + dy - l.sock.y0) * w + (sx + dx - l.sock.x0), d = m[l.dark[0] + i];
                    if (d == 0 || d == 255) continue;
                    seam++;
                    solid += m[l.fill[0] + i] == 255;
                }
            CHECK(seam > 0 && solid == seam, "level %d pose A: under the seam of stripes 1 and 2 the fill stays solid (%d of %d px)", lv, solid, seam);
        }
        for (int q = 0; q < 2; q++) {
            wx_pt_t pt[4];
            wx_wind_quad(lv, 1, q, pt);
            float ex = (pt[0].x + pt[1].x) / 2, ey = (pt[0].y + pt[1].y) / 2, nx = pt[3].x - pt[0].x, ny = pt[3].y - pt[0].y,
                  nl = hypotf(nx, ny);
            ex += nx / nl * 0.5f;
            ey += ny / nl * 0.5f;
            int i = ((int)ey - l.sock.y0) * w + ((int)ex - l.sock.x0), f = m[l.fill[1] + i], d = m[l.dark[1] + i];
            if (q)
                CHECK(d > 0 && f == 0, "level %d: on the dark stripe's outer edge only the dark plane covers (fill %d, dark %d)", lv, f, d);
            else
                CHECK(f > 0 && d == 0, "level %d: the light stripe's outer edge (fill %d, dark %d)", lv, f, d);
        }
        for (int k = 0; k < WX_LEAF_ROTS && l.nleaves; k++)
            CHECK(m[l.leaf[k] + (WX_LEAF_BOX / 2) * WX_LEAF_BOX + WX_LEAF_BOX / 2] == 255, "level %d: leaf rotation %d baked", lv, k);
        int diff = 0, partial = 0;
        for (int i = 0; i < w * (l.sock.y1 - l.sock.y0); i++) {
            diff += m[l.fill[0] + i] != m[l.fill[2] + i];
            partial += m[l.fill[1] + i] > 0 && m[l.fill[1] + i] < 255;
        }
        CHECK(diff > 100 && partial > 50, "level %d: poses A and C differ (%d px), soft edges (%d px)", lv, diff, partial);
        for (int r = 0; r < l.nrows; r++) {
            const uint8_t *s = m + l.streak[r];
            int sw = l.streak_w[r], mid = WX_STREAK_PAD + (int)(g->row[r].len / 2);
            CHECK(s[sw + mid] == 255 && s[2 * sw + mid] == 255 && s[3 * sw + mid] == 255 && s[4 * sw + mid] == 255 && s[mid] == 0 &&
                      s[5 * sw + mid] == 0,
                  "level %d row %d: a 4 px line", lv, r);
            CHECK(s[2 * sw] == 0 && s[2 * sw + sw - 1] == 0 && s[2 * sw + WX_STREAK_PAD + 1] > 0,
                  "level %d row %d: round caps inside the sprite", lv, r);
        }
    }
    wx_obs_t o = windy(18, 225);
    wx_temp_t t;
    wx_temp_face(&o, NULL, WXF_WIND, &t);
    wx_bake_out_t out;
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    static const struct {
        float frac;
        uint32_t rgb;
    } RING[] = {{0.04f, 0x9AA4B0}, {0.2f, 0x4FD6CC}, {0.36f, 0x8ACF7A}, {0.5f, 0xC9D25A}, {0.7f, 0xE8D44A}, {0.9f, 0xF08A3A}, {0.995f, 0xFF5050}};
    for (size_t i = 0; i < sizeof RING / sizeof RING[0]; i++) {
        float x, y;
        wx_gauge_xy(RING[i].frac, &x, &y);
        uint16_t c = bg[(int)y * WX_FACE + (int)x];
        CHECK(c == wx_rgb565(RING[i].rgb), "the gauge at %.0f km/h: %06X, want %06X", RING[i].frac * 50, (unsigned)rgb_of(c),
              (unsigned)RING[i].rgb);
    }
    o.has_wind = false;
    wx_temp_face(&o, NULL, WXF_WIND, &t);
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    float x, y;
    wx_gauge_xy(0.5f, &x, &y);
    CHECK(bg[(int)y * WX_FACE + (int)x] == wx_rgb565(0x262626), "no data: the grey track");

    static uint8_t leaf[WX_LEAF_BOX * WX_LEAF_BOX];
    wx_paint_leaf(leaf, 0);
    CHECK(leaf[10 * WX_LEAF_BOX + 10] == 255 && leaf[10 * WX_LEAF_BOX + 4] > 0 && leaf[10 * WX_LEAF_BOX + 15] > 0 &&
              leaf[3 * WX_LEAF_BOX + 10] == 0 && leaf[10 * WX_LEAF_BOX + 1] == 0,
          "leaf at 0 degrees: 16 px long, lying flat");
    wx_paint_leaf(leaf, 6);
    CHECK(leaf[10 * WX_LEAF_BOX + 10] == 255 && leaf[4 * WX_LEAF_BOX + 10] > 0 && leaf[10 * WX_LEAF_BOX + 4] == 0,
          "leaf at 90 degrees: upright");
    wx_paint_leaf(leaf, 3);
    CHECK(leaf[15 * WX_LEAF_BOX + 15] > 128 && leaf[5 * WX_LEAF_BOX + 5] > 128 && leaf[5 * WX_LEAF_BOX + 15] == 0 &&
              leaf[15 * WX_LEAF_BOX + 5] == 0,
          "leaf at 45 degrees turns clockwise, as CSS rotate()");
    int ink = 0;
    for (int i = 0; i < WX_LEAF_BOX * WX_LEAF_BOX; i++) ink += leaf[i];
    CHECK(ink > 60 * 255 && ink < 90 * 255, "leaf area about 16 x 7.5 x 2/3 px (%d)", ink / 255);
    free(m);
    free(hero);
}

int main(void)
{
    test_bands();
    test_compass();
    test_levels();
    test_from();
    test_auto();
    test_bake_kind();
    test_geometry();
    test_cycle();
    test_streaks();
    test_leaves();
    test_layout();
    test_areas();
    test_paint();
    if (failures) {
        printf("wind: %d failure(s)\n", failures);
        return 1;
    }
    printf("wind: all passed\n");
    return 0;
}
