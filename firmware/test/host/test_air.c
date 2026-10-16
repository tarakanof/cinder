#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_air.h"
#include "wx_faces.h"
#include "wx_paint.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NOW 1782043200LL

static wx_obs_t air(int aqi)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = 12, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .moon_up = -1, .observed_at = NOW - 300, .has_obs_age = true, .obs_age_s = 300,
                  .has_utc_offset = true, .utc_offset = 3600, .has_now_rain = true, .has_rain = true, .has_aqi = true, .aqi = aqi};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "clouds");
    snprintf(o.code, sizeof o.code, "3");
    return o;
}

static int channel_diff(uint32_t a, uint32_t b)
{
    int m = 0;
    for (int s = 0; s <= 16; s += 8) {
        int d = abs((int)((a >> s) & 0xFF) - (int)((b >> s) & 0xFF));
        if (d > m) m = d;
    }
    return m;
}

static uint32_t rgb_of(uint16_t c)
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (r << 19) | (g << 10) | (b << 3);
}

static void test_colours(void)
{
    static const struct {
        int aqi;
        uint32_t rgb;
    } C[] = {{0, 0x4FD6CC},  {10, 0x4FD6CC}, {20, 0x50D1BB}, {25, 0x50CEB2}, {30, 0x50CCAA}, {42, 0xB0DC6B}, {50, 0xF0E641},
             {60, 0xF89B48}, {68, 0xFE5F4E}, {70, 0xFF5050}, {80, 0xE84058}, {90, 0xD03060}, {100, 0xD03060}, {150, 0xD03060}};
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++)
        CHECK(channel_diff(wx_air_rgb(C[i].aqi), C[i].rgb) <= 1, "%d: %06X, AA2's generator %06X", C[i].aqi, (unsigned)wx_air_rgb(C[i].aqi),
              (unsigned)C[i].rgb);
    CHECK(wx_air_rgb(-5) == 0x4FD6CC && wx_air_rgb(999) == 0xD03060, "outside the scale: its ends");
    static const struct {
        int aqi;
        uint32_t rgb;
    } X[] = {{13, 0x4FD5C7}, {25, 0x50CFB3}, {37, 0x88D585}, {55, 0xF4C145}, {60, 0xF89B49},
             {68, 0xFE5F4F}, {75, 0xF34854}, {85, 0xDC385C}};
    for (size_t i = 0; i < sizeof X / sizeof X[0]; i++)
        CHECK(wx_air_rgb(X[i].aqi) == X[i].rgb, "%d: %06X, want %06X (rounded half up)", X[i].aqi, (unsigned)wx_air_rgb(X[i].aqi),
              (unsigned)X[i].rgb);
}

static void test_bands(void)
{
    static const struct {
        int aqi, band;
        const char *word;
        uint32_t rgb;
    } C[] = {
        {0, 0, "Good", 0x4FD6CC},      {20, 0, "Good", 0x4FD6CC},          {21, 1, "Fair", 0x50CCAA},
        {40, 1, "Fair", 0x50CCAA},     {41, 2, "Moderate", 0xF0E641},     {42, 2, "Moderate", 0xF0E641},
        {60, 2, "Moderate", 0xF0E641}, {61, 3, "Poor", 0xFF5050},          {68, 3, "Poor", 0xFF5050},
        {80, 3, "Poor", 0xFF5050},     {81, 4, "Very poor", 0xD03060},    {100, 4, "Very poor", 0xD03060},
        {101, 5, "Extremely poor", 0xB060E0}, {999, 5, "Extremely poor", 0xB060E0},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        int b = wx_air_band(C[i].aqi);
        CHECK(b == C[i].band && !strcmp(wx_air_word(b), C[i].word) && wx_air_band_rgb(b) == C[i].rgb, "%d: band %d %s %06X", C[i].aqi, b,
              wx_air_word(b), (unsigned)wx_air_band_rgb(b));
    }
    CHECK(!strcmp(wx_air_word(-1), "Good") && !strcmp(wx_air_word(9), "Extremely poor") && wx_air_band_rgb(9) == 0xB060E0,
          "a band outside the list is clamped");
}

static void test_wilted(void)
{
    CHECK(!wx_air_wilted(60, -1) && wx_air_wilted(61, -1), "nothing shown: poor (above 60) wilts");
    CHECK(wx_air_wilted(56, 1) && !wx_air_wilted(55, 1), "wilted on screen: it stays above 55");
    CHECK(!wx_air_wilted(65, 0) && wx_air_wilted(66, 0), "healthy on screen: it stays to 65");
}

static void test_from(void)
{
    wx_obs_t o = air(42);
    wx_air_t a;
    wx_air_from(&o, NULL, &a);
    CHECK(a.on && a.have && a.aqi == 42 && a.band == 2 && !a.wilted && fabsf(a.frac - 0.42f) < 1e-6f, "AA2: 42, Moderate, healthy");
    o = air(68);
    wx_air_from(&o, NULL, &a);
    CHECK(a.have && a.band == 3 && a.wilted && fabsf(a.frac - 0.68f) < 1e-6f, "AA4: 68, Poor, wilted");
    o = air(250);
    wx_air_from(&o, NULL, &a);
    CHECK(a.aqi == 250 && a.band == 5 && a.wilted && a.frac == 1, "250: the marker at the gauge's end");
    o = air(-3);
    wx_air_from(&o, NULL, &a);
    CHECK(a.aqi == 0 && a.frac == 0, "clamped at 0");
    o = air(5000);
    wx_air_from(&o, NULL, &a);
    CHECK(a.aqi == WX_AIR_MAX, "clamped at 999");
    wx_air_t shown = {.on = true, .have = true, .wilted = true};
    o = air(58);
    wx_air_from(&o, &shown, &a);
    CHECK(a.wilted, "58 with the wilted flower on screen: kept");
    wx_air_from(&o, NULL, &a);
    CHECK(!a.wilted, "58 fresh: healthy");
    shown.have = false;
    wx_air_from(&o, &shown, &a);
    CHECK(!a.wilted, "a shown face without data keeps nothing");
    shown = (wx_air_t){.on = false, .have = true, .wilted = true};
    wx_air_from(&o, &shown, &a);
    CHECK(!a.wilted, "another face's air keeps nothing");
    shown = (wx_air_t){.on = true, .have = true, .wilted = false};
    o = air(63);
    wx_air_from(&o, &shown, &a);
    CHECK(!a.wilted, "63 with the healthy flower on screen: kept");

    o = air(68);
    o.has_aqi = false;
    wx_air_from(&o, NULL, &a);
    CHECK(a.on && !a.have && !a.wilted && a.frac == 0, "no aqi: grey");
    o = air(68);
    o.stale = true;
    wx_air_from(&o, NULL, &a);
    CHECK(!a.have, "stale: grey");
    o = air(68);
    o.age_s = WX_MAX_AGE_S + 1;
    wx_air_from(&o, NULL, &a);
    CHECK(!a.have, "old: grey");
    o = air(68);
    o.valid = false;
    wx_air_from(&o, NULL, &a);
    CHECK(!a.have, "invalid: grey");
    wx_air_from(NULL, NULL, &a);
    CHECK(a.on && !a.have, "no data: grey");
}

static void test_poor(void)
{
    wx_obs_t o = air(60);
    CHECK(!wx_air_poor(&o, false), "60 Moderate: not the auto face");
    o.aqi = 61;
    CHECK(wx_air_poor(&o, false), "61 Poor: the auto face");
    o.aqi = 56;
    CHECK(wx_air_poor(&o, true) && !wx_air_poor(&o, false), "56 while shown: kept");
    o.aqi = 55;
    CHECK(!wx_air_poor(&o, true), "55: released");
    o.aqi = 5000;
    CHECK(wx_air_poor(&o, false), "huge: poor");
    o = air(90);
    o.stale = true;
    CHECK(!wx_air_poor(&o, false), "stale: never");
    o = air(90);
    o.has_aqi = false;
    CHECK(!wx_air_poor(&o, false) && !wx_air_poor(NULL, true), "no aqi, no data: never");
}

static void test_auto(void)
{
    wx_temp_t t, shown;
    wx_obs_t o = air(68);
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_AIR && t.air.on && t.air.wilted && !t.rain.on && !t.cast.on && !t.wind.on, "poor air, dry and calm: Air");
    o = air(42);
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP && !t.air.on, "moderate: Temperature");
    o = air(58);
    shown = t;
    shown.face = WXF_AIR;
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_AIR, "58 while Air shows: it stays (hysteresis)");
    shown.face = WXF_TEMP;
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "58 while Temperature shows: Temperature");

    o = air(90);
    o.now_rain = 2;
    o.rain_n = 1;
    o.rain_spell[0] = (wx_spell_t){NOW - 600, NOW + 3600, 2, 0};
    o.rain_until = NOW + 3600;
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_RAIN && !t.air.on, "wet: Rain now first");
    o = air(90);
    o.rain_n = 1;
    o.rain_spell[0] = (wx_spell_t){NOW + 1800, NOW + 5400, 2, 0};
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_CAST && !t.air.on, "rain within the hour: Rain forecast before Air");
    o = air(90);
    o.has_wind = true;
    o.wind_kmh = 45;
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_WIND && !t.air.on, "a strong wind: Wind before Air");
    o.wind_kmh = 20;
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_AIR, "a light wind: Air");
}

static void test_pick(void)
{
    wx_temp_t t, shown;
    wx_obs_t o = air(42);
    wx_temp_face(&o, NULL, WXF_AIR, &t);
    CHECK(t.face == WXF_AIR && t.air.on && t.air.have && !t.air.wilted && !t.rain.on && !t.sun.on, "picked: the Air");
    o = air(90);
    wx_temp_face(&o, NULL, WXF_TEMP, &t);
    CHECK(t.face == WXF_TEMP && !t.air.on, "Temperature picked over an auto Air carries no air");
    wx_temp_face(&o, NULL, WXF_SUN, &t);
    CHECK(t.face == WXF_SUN && t.sun.on && !t.air.on, "Sun picked over an auto Air");
    o = air(68);
    wx_temp_face(&o, NULL, WXF_AIR, &shown);
    o = air(58);
    wx_temp_face(&o, &shown, WXF_AIR, &t);
    CHECK(t.air.wilted && wx_temp_bake_kind(&t, &shown) == 0, "58 picked with the wilted flower on screen: kept, no bake");
    o = air(58);
    o.now_rain = 2;
    o.rain_n = 1;
    o.rain_spell[0] = (wx_spell_t){NOW - 600, NOW + 3600, 2, 0};
    wx_temp_face(&o, &shown, WXF_AIR, &t);
    CHECK(t.face == WXF_AIR && t.air.wilted && !t.rain.on, "picked while it rains (auto Rain now): the flower on screen kept");
    o = air(50);
    wx_temp_face(&o, &shown, WXF_AIR, &t);
    CHECK(!t.air.wilted && wx_temp_bake_kind(&t, &shown) == WX_BAKE_FULL, "50: the healthy flower bakes");
    o = air(68);
    wx_temp_face(&o, &shown, WXF_AIR, &t);
    o.aqi = 75;
    wx_temp_t u;
    wx_temp_face(&o, &t, WXF_AIR, &u);
    CHECK(wx_temp_bake_kind(&u, &t) == 0, "the value moving inside a flower: no bake");
    o.stale = true;
    wx_temp_face(&o, &t, WXF_AIR, &u);
    CHECK(wx_temp_bake_kind(&u, &t) == WX_BAKE_FULL && !u.air.have, "data going stale: the grey gauge bakes");
    wx_temp_face(NULL, NULL, WXF_AIR, &t);
    CHECK(t.face == WXF_AIR && t.air.on && !t.air.have, "no data: the empty Air");
    wx_air_t a = {.on = true, .have = true}, b = a;
    CHECK(wx_air_same_scene(&a, &b), "same");
    b.on = false;
    CHECK(!wx_air_same_scene(&a, &b), "on");
    b = a;
    b.have = false;
    CHECK(!wx_air_same_scene(&a, &b), "have");
    b = a;
    b.wilted = true;
    CHECK(!wx_air_same_scene(&a, &b), "wilted");
    b = a;
    b.aqi = 77;
    b.band = 3;
    b.frac = 0.77f;
    CHECK(wx_air_same_scene(&a, &b), "the value is not the picture");
}

static bool box_in(wx_box_t b, wx_box_t a) { return b.x0 >= a.x0 && b.y0 >= a.y0 && b.x1 <= a.x1 && b.y1 <= a.y1; }

static void test_petal(void)
{
    wx_petal_t p;
    wx_air_petal(-1, &p);
    CHECK(p.opa == 0, "k < 0: hidden");
    wx_air_petal(75, &p);
    CHECK(p.opa == 0 && p.x == 254 && p.y == 134 && p.rot == 0, "the loop's start: 58 px up, 14 right, -50 degrees, transparent");
    wx_air_petal(75 + 6, &p);
    CHECK(p.opa > 0 && p.opa < 255 && p.y > 134, "fading in as it starts to fall: %d", p.opa);
    wx_air_petal(75 + 9, &p);
    CHECK(p.opa == 255, "in by 8 %% of the loop");
    wx_air_petal(75 + 66, &p);
    CHECK(p.opa == 255 && p.x == 240 && p.y == 192 && p.rot == 5, "landed by 62 %%: at rest, upright");
    wx_air_petal(75 + 94, &p);
    CHECK(p.opa == 255 && p.y == 192, "lies there to 90 %%");
    wx_air_petal(75 + 100, &p);
    CHECK(p.opa > 0 && p.opa < 255, "then fades: %d", p.opa);
    static const int F[][5] = {{0, 248, 159, 2, 255},   {10, 244, 174, 3, 255}, {20, 242, 186, 4, 255},
                               {80, 254, 135, 0, 168},  {90, 252, 140, 1, 255}, {100, 250, 152, 2, 255},
                               {130, 241, 189, 5, 255}, {176, 240, 192, 5, 77}, {178, 240, 192, 5, 19}};
    for (size_t i = 0; i < sizeof F / sizeof F[0]; i++) {
        wx_air_petal(F[i][0], &p);
        CHECK(p.x == F[i][1] && p.y == F[i][2] && p.rot == F[i][3] && p.opa == F[i][4],
              "frame %d: AA4's ease-in-out curve: %d,%d rot %d opa %d, want %d,%d %d %d", F[i][0], p.x, p.y, p.rot, p.opa, F[i][1], F[i][2],
              F[i][3], F[i][4]);
    }
    wx_air_petal(0, &p);
    wx_petal_t q;
    wx_air_petal(105, &q);
    CHECK(!memcmp(&p, &q, sizeof p), "a 7 s loop at 15 fps");
    wx_air_petal(0, &p);
    CHECK(p.y > 134 && p.y < 192 && p.opa == 255, "the show starts 2 s into the loop: mid-fall, visible (%d)", p.y);
    int ly = -1, lr = -1;
    bool mono = true;
    for (int k = 75; k < 75 + 66; k++) {
        wx_air_petal(k, &p);
        mono &= p.y >= ly && p.rot >= lr;
        ly = p.y;
        lr = p.rot;
    }
    CHECK(mono, "falls and turns, never back");
    wx_box_t box = wx_air_petal_box(), all = {WX_FACE, WX_FACE, 0, 0};
    CHECK(box.x0 == 240 && box.y0 == 134 && box.x1 == 274 && box.y1 == 212 && !(box.x0 & 1) && !(box.x1 & 1), "the box %d,%d..%d,%d",
          box.x0, box.y0, box.x1, box.y1);
    int changed = 0, still = 0;
    wx_petal_t prev;
    wx_air_petal(-1, &prev);
    for (int k = 0; k < 2 * 105; k++) {
        wx_air_petal(k, &p);
        wx_box_t s = {p.x, p.y, p.x + WX_PETAL_BOX, p.y + WX_PETAL_BOX};
        CHECK(box_in(s, box), "frame %d inside the box", k);
        if (s.x0 < all.x0) all.x0 = s.x0;
        if (s.y0 < all.y0) all.y0 = s.y0;
        if (s.x1 > all.x1) all.x1 = s.x1;
        if (s.y1 > all.y1) all.y1 = s.y1;
        CHECK(p.rot >= 0 && p.rot < WX_PETAL_ROTS && p.opa >= 0 && p.opa <= 255, "frame %d in range", k);
        if (k >= 105) {
            if (memcmp(&p, &prev, sizeof p)) changed++;
            else still++;
        }
        prev = p;
    }
    CHECK(all.x0 == box.x0 && all.y0 == box.y0 && all.x1 == box.x1 && all.y1 == box.y1, "the box is the travel, no more");
    CHECK(still >= 25, "still while it lies there: %d frames", still);
    printf("air: 1 area of %dx%d (%d px) on %d of 105 frames per 7 s at 15 fps, the healthy flower none\n", box.x1 - box.x0,
           box.y1 - box.y0, (box.x1 - box.x0) * (box.y1 - box.y0), changed);
}


typedef struct {
    double a, b, c, d, e, f;
} mat_t;

static mat_t mat_mul(mat_t p, mat_t q)
{
    return (mat_t){p.a * q.a + p.b * q.c, p.a * q.b + p.b * q.d, p.c * q.a + p.d * q.c, p.c * q.b + p.d * q.d,
                   p.a * q.e + p.b * q.f + p.e, p.c * q.e + p.d * q.f + p.f};
}

static mat_t rot(double deg, double px, double py)
{
    double r = deg * M_PI / 180, c = cos(r), sn = sin(r);
    return (mat_t){c, -sn, sn, c, px - c * px + sn * py, py - sn * px - c * py};
}

static mat_t move(double x, double y, double deg)
{
    double r = deg * M_PI / 180, c = cos(r), sn = sin(r);
    return (mat_t){c, -sn, sn, c, x, y};
}

static const mat_t ID = {1, 0, 0, 1, 0, 0};

static void inv(mat_t m, double x, double y, double *lx, double *ly)
{
    double det = m.a * m.d - m.b * m.c, dx = x - m.e, dy = y - m.f;
    *lx = (m.d * dx - m.b * dy) / det;
    *ly = (-m.c * dx + m.a * dy) / det;
}

static double ref[WX_FACE * WX_FACE][3];
static double cov1(double d) { return d < -0.5 ? 1 : d > 0.5 ? 0 : 0.5 - d; }

static void ref_put(int i, uint32_t rgb, double a)
{
    for (int k = 0; k < 3; k++) ref[i][k] += ((double)((rgb >> (16 - 8 * k)) & 0xFF) - ref[i][k]) * a;
}

static double seg(double x, double y, double ax, double ay, double bx, double by)
{
    double vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy, t = l2 > 0 ? ((x - ax) * vx + (y - ay) * vy) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypot(x - ax - t * vx, y - ay - t * vy);
}

#define RP 720

static double outline_sd(const double (*p)[2], int n, bool closed, double x, double y)
{
    double d = 1e9;
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (i || closed) {
            double e = seg(x, y, p[j][0], p[j][1], p[i][0], p[i][1]);
            if (e < d) d = e;
        }
        if (closed && (p[i][1] > y) != (p[j][1] > y) && x < (p[j][0] - p[i][0]) * (y - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0]) in = !in;
    }
    return in ? -d : d;
}

static void ref_shape(mat_t m, const double (*p)[2], int n, bool closed, uint32_t fill, bool has_fill, uint32_t line, double hw,
                      uint32_t (*shade)(double, double))
{
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (int i = 0; i < n; i++) {
        double fx = m.a * p[i][0] + m.b * p[i][1] + m.e, fy = m.c * p[i][0] + m.d * p[i][1] + m.f;
        x0 = fmin(x0, fx), y0 = fmin(y0, fy), x1 = fmax(x1, fx), y1 = fmax(y1, fy);
    }
    for (int y = (int)floor(y0 - hw - 2); y <= (int)ceil(y1 + hw + 2); y++)
        for (int x = (int)floor(x0 - hw - 2); x <= (int)ceil(x1 + hw + 2); x++) {
            double lx, ly;
            inv(m, x + 0.5, y + 0.5, &lx, &ly);
            double d = outline_sd(p, n, closed, lx, ly);
            int i = y * WX_FACE + x;
            if (has_fill) ref_put(i, shade ? shade(lx, ly) : fill, cov1(d));
            if (hw > 0) ref_put(i, line, cov1(fabs(d) - hw));
        }
}

static double pts[2 * RP][2];

static void ref_ellipse(mat_t m, double cx, double cy, double rx, double ry, uint32_t fill, bool has_fill, uint32_t line, double hw,
                        uint32_t (*shade)(double, double))
{
    for (int i = 0; i < RP; i++) {
        double a = 2 * M_PI * i / RP;
        pts[i][0] = cx + rx * cos(a);
        pts[i][1] = cy + ry * sin(a);
    }
    ref_shape(m, pts, RP, true, fill, has_fill, line, hw, shade);
}

static void ref_curve(const double b[4][2], int n, int at)
{
    for (int i = 0; i < n; i++) {
        double t = (double)i / (n - 1), u = 1 - t, w[4] = {u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t};
        pts[at + i][0] = w[0] * b[0][0] + w[1] * b[1][0] + w[2] * b[2][0] + w[3] * b[3][0];
        pts[at + i][1] = w[0] * b[0][1] + w[1] * b[1][1] + w[2] * b[2][1] + w[3] * b[3][1];
    }
}

static void ref_stem(mat_t m, const double b[4][2], uint32_t rgb)
{
    ref_curve(b, RP, 0);
    ref_shape(m, pts, RP, false, 0, false, rgb, 2.5, NULL);
}

static void ref_line(mat_t m, double ax, double ay, double bx, double by, double w, uint32_t rgb)
{
    const double p[2][2] = {{ax, ay}, {bx, by}};
    ref_shape(m, p, 2, false, 0, false, rgb, w / 2, NULL);
}

static void ref_leaf(mat_t g, double x, double y, double deg, double len, double wid, uint32_t fill, uint32_t line)
{
    mat_t m = mat_mul(g, move(x, y, deg));
    const double up[4][2] = {{0, 0}, {len * 0.25, -wid / 2}, {len * 0.75, -wid / 2}, {len, 0}};
    const double down[4][2] = {{len, 0}, {len * 0.75, wid / 2}, {len * 0.25, wid / 2}, {0, 0}};
    ref_curve(up, RP, 0);
    ref_curve(down, RP, RP);
    ref_shape(m, pts, 2 * RP, true, fill, true, line, 1.5, NULL);
    ref_line(m, 4, 0, len - 6, 0, 2, line);
}

static uint32_t ref_pink(double x, double y)
{
    double g = hypot(x - 233, y - 112) / 40;
    if (g <= 0.3) return 0xF59AB0;
    double f = (g - 0.3) / 0.7;
    f = f > 1 ? 1 : f;
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        double a = (0xF59AB0 >> sh) & 0xFF, b = (0xE87A96 >> sh) & 0xFF;
        out |= (uint32_t)lround(a + (b - a) * f) << sh;
    }
    return out;
}

static void ref_ends(void);

static void ref_healthy(void)
{
    memset(ref, 0, sizeof ref);
    ref_ends();
    const double st[4][2] = {{233, 204}, {230, 176}, {237, 150}, {233, 126}};
    ref_stem(ID, st, 0x4E8F45);
    ref_leaf(ID, 233, 188, -22, 40, 20, 0x7BC86C, 0x5FAE55);
    ref_leaf(ID, 233, 170, -158, 34, 18, 0x7BC86C, 0x5FAE55);
    for (int k = 0; k < 6; k++) ref_ellipse(rot(60.0 * k, 233, 112), 233, 93, 12.5, 20, 0, true, 0xF4B6C6, 1.25, ref_pink);
    ref_ellipse(ID, 233, 112, 11, 11, 0xF5C84A, true, 0xE3AE36, 1, NULL);
}

static void ref_wilted(void)
{
    memset(ref, 0, sizeof ref);
    ref_ends();
    ref_line(ID, 178, 206, 292, 206, 2, 0x2E3236);
    static const double FALL[3][3] = {{188, 202, 72}, {266, 203, 104}, {284, 201, 60}};
    for (int i = 0; i < 3; i++)
        ref_ellipse(rot(FALL[i][2], FALL[i][0], FALL[i][1]), FALL[i][0], FALL[i][1], 4.5, 8, 0xC98A9A, true, 0xD9A9B4, 0.75, NULL);
    mat_t g = rot(18, 226, 204), h = mat_mul(g, rot(32, 226, 122));
    const double st[4][2] = {{226, 204}, {222, 176}, {230, 150}, {226, 132}};
    ref_stem(g, st, 0x7C8A50);
    ref_leaf(g, 226, 188, 20, 36, 17, 0x9CA35A, 0x7F8646);
    ref_leaf(g, 226, 172, 165, 30, 15, 0x9CA35A, 0x7F8646);
    static const int KEPT[4] = {0, 2, 3, 5};
    for (int k = 0; k < 4; k++)
        ref_ellipse(mat_mul(h, rot(60.0 * KEPT[k], 226, 122)), 226, 104, 11.5, 18, 0xC98A9A, true, 0xD9A9B4, 1.25, NULL);
    ref_ellipse(h, 226, 122, 10, 10, 0xC9A84A, true, 0xA98A3A, 1, NULL);
    static const double DUSTS[6][3] = {{166, 128, 3}, {182, 166, 3.5}, {196, 108, 2.5}, {302, 176, 3}, {276, 90, 2.5}, {156, 188, 2.5}};
    for (int i = 0; i < 6; i++)
        for (int y = (int)DUSTS[i][1] - 6; y <= (int)DUSTS[i][1] + 6; y++)
            for (int x = (int)DUSTS[i][0] - 6; x <= (int)DUSTS[i][0] + 6; x++)
                ref_put(y * WX_FACE + x, 0x8A8A8A, 0.6 * cov1(hypot(x + 0.5 - DUSTS[i][0], y + 0.5 - DUSTS[i][1]) - DUSTS[i][2]));
    const double tri[3][2] = {{318, 108}, {340, 146}, {296, 146}};
    ref_shape(ID, tri, 3, true, 0, true, 0xFF5050, 2, NULL);
    ref_line(ID, 318, 121, 318, 133, 4, 0xFF5050);
    ref_ellipse(ID, 318, 139, 2.5, 2.5, 0xFF5050, true, 0, 0, NULL);
}

typedef struct {
    int ink, off16, off40;
    double mean;
} diff_t;

static diff_t diff_in(const uint16_t *bg, int x0, int y0, int x1, int y1)
{
    diff_t r = {0};
    double sum = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t got = rgb_of(bg[y * WX_FACE + x]), want = 0;
            for (int k = 0; k < 3; k++) want |= (uint32_t)lround(ref[y * WX_FACE + x][k]) << (16 - 8 * k);
            if (!got && !want) continue;
            r.ink++;
            int d = channel_diff(got, want);
            sum += d;
            r.off16 += d > 16;
            r.off40 += d > 40;
        }
    r.mean = r.ink ? sum / r.ink : 0;
    return r;
}

static void compare(const uint16_t *bg, const char *name, int x0, int y0, int x1, int y1, int min_ink, int max16, int max40, double max_mean)
{
    diff_t r = diff_in(bg, x0, y0, x1, y1);
    CHECK(r.ink >= min_ink && r.off16 <= max16 && r.off40 <= max40 && r.mean <= max_mean,
          "%s matches AA's geometry: %d ink px, %d off by more than 16 (at most %d), %d by more than 40 (at most %d), mean %.2f (at most %.1f)",
          name, r.ink, r.off16, max16, r.off40, max40, r.mean, max_mean);
    printf("air: %s against the reference: %d px, %d off > 16, %d off > 40, mean %.2f\n", name, r.ink, r.off16, r.off40, r.mean);
}

static void ref_ends(void)
{
    mat_t fl = {1.2, 0, 0, 1.2, 116 - 1.2 * 116, 383 - 1.2 * 383};
    for (int k = 0; k < 5; k++) {
        mat_t m = mat_mul(fl, rot(72.0 * k, 116, 383));
        for (int i = 0; i < RP; i++) {
            double a = 2 * M_PI * i / RP, x = 116 + 3.5 * cos(a), y = 376 + 6 * sin(a);
            pts[i][0] = m.a * x + m.b * y + m.e;
            pts[i][1] = m.c * x + m.d * y + m.f;
        }
        ref_shape(ID, pts, RP, true, 0, false, 0x4FD6CC, 1.25 * 1.2, NULL);
    }
    ref_ellipse(ID, 116, 383, 3.6, 3.6, 0xF0E641, true, 0, 0, NULL);
    mat_t hz = {1.2, 0, 0, 1.2, 349 - 1.2 * 352, 383 - 1.2 * 386};
    const double tri[3][2] = {{352, 368}, {370, 399}, {334, 399}}, bar[2][2] = {{352, 378}, {352, 388}};
    double t[3][2], b[2][2];
    for (int i = 0; i < 3; i++) t[i][0] = hz.a * tri[i][0] + hz.e, t[i][1] = hz.d * tri[i][1] + hz.f;
    for (int i = 0; i < 2; i++) b[i][0] = hz.a * bar[i][0] + hz.e, b[i][1] = hz.d * bar[i][1] + hz.f;
    ref_shape(ID, t, 3, true, 0, false, 0xD03060, 1.75 * 1.2, NULL);
    ref_shape(ID, b, 2, false, 0, false, 0xD03060, 1.75 * 1.2, NULL);
    ref_ellipse(ID, 349, 383 + 1.2 * 8, 2.4, 2.4, 0xD03060, true, 0, 0, NULL);
}

static void test_deg_cs(void)
{
    double worst = 0;
    for (float deg = -720; deg <= 720; deg += 0.25f) {
        float c, sn;
        wx_deg_cs(deg, &c, &sn);
        double e = fmax(fabs(c - cos(deg * M_PI / 180)), fabs(sn - sin(deg * M_PI / 180)));
        if (e > worst) worst = e;
    }
    CHECK(worst < 2e-6, "cos and sin in degrees, -720..720: worst error %.2g", worst);
}

static void test_paint(void)
{
    static wx_ring_t ring;
    static uint16_t bg[WX_FACE * WX_FACE], lut[WX_RING_LUT];
    size_t scene = (size_t)wx_scene_px() * 3;
    uint8_t *m = calloc(1, scene), *hero = calloc(1, WX_HERO_BOX * WX_HERO_BOX * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .scene = m};
    wx_obs_t o = air(42);
    wx_temp_t t;
    wx_temp_face(&o, NULL, WXF_AIR, &t);
    wx_bake_out_t out = {.hero = {1, 1, 2, 2}, .nclouds = 1};
    memset(bg, 0x55, sizeof bg);
    clock_t t0 = clock();
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    printf("air: host bake healthy %.2f ms\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    CHECK(out.hero.x1 == 0 && out.nclouds == 0, "no hero or cloud sprites");
    CHECK(lut[0] == wx_rgb565(0x4FD6CC) && lut[WX_RING_LUT - 1] == wx_rgb565(0xD03060) && lut[(WX_RING_LUT - 1) / 2] == wx_rgb565(0xF0E641),
          "the gauge from Good to Very poor, Moderate at the top");
    size_t zero = 0;
    for (size_t i = 0; i < 6 * WX_PETAL_BOX * WX_PETAL_BOX * 3; i++) zero += m[i] == 0;
    CHECK(zero == 6 * WX_PETAL_BOX * WX_PETAL_BOX * 3, "the healthy flower bakes no petals");
#define PX(x, y) rgb_of(bg[(int)(y) * WX_FACE + (int)(x)])
    CHECK(channel_diff(PX(116, 383), 0xF0E641) <= 8, "the flower end icon's centre: %06X", (unsigned)PX(116, 383));
    CHECK(channel_diff(PX(116, 383 - 1.2f * 13), 0x4FD6CC) <= 24, "its petal outline: %06X", (unsigned)PX(116, 383 - 1.2f * 13));
    CHECK(channel_diff(PX(349, 383 - 1.2f * 3), 0xD03060) <= 8, "the hazard end icon's bar: %06X", (unsigned)PX(349, 383 - 1.2f * 3));
    CHECK(channel_diff(PX(349, 383 + 1.2f * 8), 0xD03060) <= 8, "and its dot");
    CHECK(PX(337, 395) == 0 && PX(116, 383 - 1.2f * 7) == 0, "the end icons are outlines");
    CHECK(channel_diff(PX(233, 112), 0xF5C84A) <= 8, "AA2's yellow centre: %06X", (unsigned)PX(233, 112));
    CHECK(channel_diff(PX(233, 80), 0xEC839D) <= 10, "a pink petal above it, shaded: %06X", (unsigned)PX(233, 80));
    CHECK(channel_diff(PX(233, 196), 0x4E8F45) <= 16, "the green stem: %06X", (unsigned)PX(233, 196));
    float lc = cosf(-22 * (float)M_PI / 180), ls = sinf(-22 * (float)M_PI / 180);
    CHECK(channel_diff(PX(233 + 20 * lc - 5 * ls, 188 + 20 * ls + 5 * lc), 0x7BC86C) <= 12, "a leaf: %06X",
          (unsigned)PX(233 + 20 * lc - 5 * ls, 188 + 20 * ls + 5 * lc));
    CHECK(PX(318, 139) == 0 && PX(166, 128) == 0 && PX(200, 206) == 0, "no hazard sign, dust or ground on a healthy day");
    ref_healthy();
    compare(bg, "AA2's flower", 150, 60, 346, 216, 5000, 170, 75, 7);
    compare(bg, "AA2's stem", 228, 196, 239, 208, 50, 2, 0, 7);
    compare(bg, "the flower end icon", 90, 355, 142, 410, 500, 30, 2, 8);
    compare(bg, "the hazard end icon", 320, 350, 380, 410, 700, 4, 0, 2);
    CHECK(PX(233, 300) == 0 && PX(233, 40) == 0, "black elsewhere");
    float dx, dy;
    wx_face_dot_xy(WXF_AIR, WXF_N, &dx, &dy);
    CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_ON), "the Air dot is white");
    wx_face_dot_xy(WXF_SUN, WXF_N, &dx, &dy);
    CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_OFF), "the others grey");

    o = air(68);
    wx_temp_face(&o, NULL, WXF_AIR, &t);
    t0 = clock();
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    printf("air: host bake wilted %.2f ms\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    CHECK(channel_diff(PX(318, 139), 0xFF5050) <= 8 && channel_diff(PX(318, 127), 0xFF5050) <= 8, "AA4's hazard sign");
    CHECK(PX(310, 140) == 0, "black inside it");
    CHECK(channel_diff(PX(200, 206), 0x2E3236) <= 8, "the ground");
    CHECK(channel_diff(PX(188, 202), 0xC98A9A) <= 8, "a fallen petal");
    CHECK(channel_diff(PX(166, 128), 0x535353) <= 8, "dust: %06X", (unsigned)PX(166, 128));
    float hs = sinf(18 * (float)M_PI / 180), hc = cosf(18 * (float)M_PI / 180);
    CHECK(channel_diff(PX(226 + 82 * hs, 204 - 82 * hc), 0xC9A84A) <= 8, "the faded centre, drooping 18 degrees: %06X",
          (unsigned)PX(226 + 82 * hs, 204 - 82 * hc));
    CHECK(channel_diff(PX(233, 112), 0xF5C84A) > 40, "not AA2's flower");
    ref_wilted();
    compare(bg, "AA4's flower", 150, 60, 346, 216, 5000, 125, 30, 5.5);
    compare(bg, "AA4's hazard sign", 290, 100, 346, 152, 600, 4, 0, 7);
    for (int r = 0; r < WX_PETAL_ROTS; r++) {
        const uint8_t *s = m + r * WX_PETAL_BOX * WX_PETAL_BOX * 3, *a = s + WX_PETAL_BOX * WX_PETAL_BOX * 2;
        CHECK(a[10 * WX_PETAL_BOX + 10] == 255 && channel_diff(rgb_of(((const uint16_t *)s)[10 * WX_PETAL_BOX + 10]), 0xC98A9A) <= 8,
              "petal %d: filled at its centre", r);
        CHECK(!a[0] && !a[WX_PETAL_BOX - 1] && !a[WX_PETAL_BOX * WX_PETAL_BOX - 1], "petal %d: clear corners", r);
    }
    const uint8_t *a5 = m + 5 * WX_PETAL_BOX * WX_PETAL_BOX * 3 + WX_PETAL_BOX * WX_PETAL_BOX * 2;
    const uint8_t *a0 = m + WX_PETAL_BOX * WX_PETAL_BOX * 2;
    CHECK(a5[10 * WX_PETAL_BOX + 16] > 200 && a5[16 * WX_PETAL_BOX + 10] == 0, "at rest it lies along the ground (86 degrees)");
    CHECK(a0[10 * WX_PETAL_BOX + 17] == 0 && a0[15 * WX_PETAL_BOX + 6] > 100 && a5[15 * WX_PETAL_BOX + 6] < 100,
          "at the top it is turned 50 degrees back");
    CHECK((size_t)(6 * WX_PETAL_BOX * WX_PETAL_BOX * 3) <= scene, "the petals fit the scene buffer");

    o.has_aqi = false;
    wx_temp_face(&o, NULL, WXF_AIR, &t);
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    CHECK(lut[0] == wx_rgb565(0x262626) && lut[WX_RING_LUT - 1] == wx_rgb565(0x262626), "no data: the grey track");
    CHECK(channel_diff(PX(233, 112), 0xF5C84A) <= 8 && channel_diff(PX(116, 383), 0xF0E641) <= 8, "with the healthy flower and the ends");
#undef PX
    free(m);
    free(hero);
}

int main(void)
{
    test_deg_cs();
    test_colours();
    test_bands();
    test_wilted();
    test_from();
    test_poor();
    test_auto();
    test_pick();
    test_petal();
    test_paint();
    if (failures) {
        printf("air: %d failure(s)\n", failures);
        return 1;
    }
    printf("air: all passed\n");
    return 0;
}
