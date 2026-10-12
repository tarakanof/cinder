#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_faces.h"
#include "wx_paint.h"
#include "wx_rain.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline float ref_clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static inline float ref_cov(float d) { return ref_clamp01(0.5f - d); }

static inline uint16_t ref_mix(uint16_t d, uint16_t s, float a)
{
    int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
    int r = dr + (int)lroundf((float)(sr - dr) * a), g = dg + (int)lroundf((float)(sg - dg) * a),
        b = db + (int)lroundf((float)(sb - db) * a);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void ref_put(wx_img_t *im, int i, uint16_t c, float a)
{
    if (!(a > 0.002f)) return;
    if (a > 1) a = 1;
    if (!im->a) {
        im->px[i] = a >= 0.998f ? c : ref_mix(im->px[i], c, a);
        return;
    }
    float da = im->a[i] / 255.0f;
    float oa = a + da * (1 - a);
    im->px[i] = da <= 0 ? c : ref_mix(im->px[i], c, a / oa);
    im->a[i] = (uint8_t)lroundf(oa * 255.0f);
}

static void ref_clip(const wx_img_t *im, float fx0, float fy0, float fx1, float fy1, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = (int)floorf(fx0) - im->x0;
    *y0 = (int)floorf(fy0) - im->y0;
    *x1 = (int)ceilf(fx1) - im->x0;
    *y1 = (int)ceilf(fy1) - im->y0;
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > im->w) *x1 = im->w;
    if (*y1 > im->h) *y1 = im->h;
}

static float ref_seg_d(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float l2 = vx * vx + vy * vy;
    float t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
    t = ref_clamp01(t);
    return hypotf(wx - t * vx, wy - t * vy);
}

static float ref_ellipse_d(float x, float y, float cx, float cy, float rx, float ry)
{
    float dx = x - cx, dy = y - cy;
    float f = dx * dx / (rx * rx) + dy * dy / (ry * ry) - 1;
    float gx = 2 * dx / (rx * rx), gy = 2 * dy / (ry * ry);
    float gl = sqrtf(gx * gx + gy * gy);
    return gl > 1e-6f ? f / gl : -(rx < ry ? rx : ry);
}

#define CANOPY_L 105.0f
#define CANOPY_R 361.0f
#define CANOPY_RIM 198.0f
#define CANOPY_RY 103.0f
#define SCALLOPS 5
#define UMB_HW 2.25f

static float ref_canopy_d(float x, float y)
{
    float cx = (CANOPY_L + CANOPY_R) / 2, rx = (CANOPY_R - CANOPY_L) / 2;
    float d = fmaxf(ref_ellipse_d(x, y, cx, CANOPY_RIM, rx, CANOPY_RY), y - CANOPY_RIM);
    float w = (CANOPY_R - CANOPY_L) / SCALLOPS;
    for (int k = 0; k < SCALLOPS; k++) d = fmaxf(d, -ref_ellipse_d(x, y, CANOPY_L + w * (k + 0.5f), CANOPY_RIM, w / 2, 9));
    return d;
}

static float ref_handle_d(float x, float y)
{
    float d = ref_seg_d(x, y, 233, 185, 233, 210.75f);
    d = fminf(d, ref_seg_d(x, y, 233, 336.25f, 233, 366));
    float arc = y >= 366 ? fabsf(hypotf(x - 244, y - 366) - 11) : fminf(hypotf(x - 233, y - 366), hypotf(x - 255, y - 366));
    return fminf(d, arc);
}

static void ref_umbrella(wx_img_t *im)
{
    const uint16_t orange = wx_rgb565(0xFF8A3D);
    int x0, y0, x1, y1;
    ref_clip(im, CANOPY_L - 4, 76, CANOPY_R + 4, 382, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            int i = y * im->w + x;
            ref_put(im, i, orange, ref_cov(ref_handle_d(fx, fy) - UMB_HW));
            float d = ref_canopy_d(fx, fy);
            ref_put(im, i, 0, ref_cov(d));
            ref_put(im, i, orange, ref_cov(fabsf(d) - UMB_HW));
            ref_put(im, i, orange, ref_cov(ref_seg_d(fx, fy, 233, 93, 233, 81) - UMB_HW));
        }
}

static float ref_drip_hw(float t) { return 8.0f * t * (1 - t); }

static void ref_drip(uint8_t *a8, int pose)
{
    static const float SCALE[WX_DRIP_POSES] = {0.45f, 0.75f, 1.0f};
    const float sc = SCALE[pose < 0 ? 0 : pose >= WX_DRIP_POSES ? WX_DRIP_POSES - 1 : pose];
    const float ox = WX_DRIP_W / 2.0f, oy = 203.0f - WX_DRIP_Y;
    memset(a8, 0, WX_DRIP_W * WX_DRIP_H);
    for (int y = 0; y < WX_DRIP_H; y++)
        for (int x = 0; x < WX_DRIP_W; x++) {
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float px = ((float)x + (sx + 0.5f) / 4 - ox) / sc, py = ((float)y + (sy + 0.5f) / 4 - oy) / sc + 203.0f;
                    if (py < 199 || py > 208) continue;
                    float lo = 0, hi = 1;
                    for (int it = 0; it < 24; it++) {
                        float t = (lo + hi) / 2, u = 1 - t;
                        float yt = 199 * u * u + 2 * u * t * 205 + 208 * t * t;
                        if (yt < py) lo = t;
                        else hi = t;
                    }
                    if (fabsf(px) <= ref_drip_hw((lo + hi) / 2)) hit++;
                }
            a8[y * WX_DRIP_W + x] = (uint8_t)(hit * 255 / 16);
        }
}

static uint32_t rng = 0x9E3779B9u;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void fill(wx_img_t *im, bool noise)
{
    size_t n = (size_t)im->w * im->h;
    for (size_t i = 0; i < n; i++) {
        im->px[i] = noise ? (uint16_t)next() : 0;
        if (im->a) im->a[i] = noise ? (uint8_t)next() : 0;
    }
}

static int first_diff(const wx_img_t *a, const wx_img_t *b)
{
    size_t n = (size_t)a->w * a->h;
    for (size_t i = 0; i < n; i++)
        if (a->px[i] != b->px[i] || (a->a && a->a[i] != b->a[i])) return (int)i;
    return -1;
}

static void test_face(bool alpha)
{
    static uint16_t got[WX_FACE * WX_FACE], want[WX_FACE * WX_FACE];
    static uint8_t got_a[WX_FACE * WX_FACE], want_a[WX_FACE * WX_FACE];
    const char *what = alpha ? "face RGBA" : "face bg";
    for (int noise = 0; noise < 2; noise++) {
        wx_img_t g = {.px = got, .a = alpha ? got_a : NULL, .w = WX_FACE, .h = WX_FACE};
        wx_img_t w = {.px = want, .a = alpha ? want_a : NULL, .w = WX_FACE, .h = WX_FACE};
        uint32_t seed = rng;
        fill(&g, noise);
        rng = seed;
        fill(&w, noise);
        wx_paint_umbrella(&g);
        ref_umbrella(&w);
        int i = first_diff(&g, &w);
        CHECK(i < 0, "%s (%s): first diff at (%d, %d) %04X want %04X", what, noise ? "noise" : "cleared", i % WX_FACE, i / WX_FACE,
              i < 0 ? 0 : got[i], i < 0 ? 0 : want[i]);
        if (alpha && !noise) {
            CHECK(got_a[190 * WX_FACE + 233] == 255 && got_a[185 * WX_FACE + 233] > 0 && got_a[200 * WX_FACE + 233] > 0,
                  "face RGBA: the handle/canopy overlap near (233, 185) is painted");
            CHECK(got_a[377 * WX_FACE + 244] > 0 && got_a[366 * WX_FACE + 255] > 0, "face RGBA: the lower hook is painted");
        }
    }
}

static void test_cover_boxes(void)
{
    wx_rain_layout_t l;
    wx_rain_layout(&l);
    for (int s = 0; s < WX_SHEETS; s++) {
        wx_box_t c = l.cover_box[s];
        int w = c.x1 - c.x0, h = c.y1 - c.y0;
        uint8_t *gb = malloc((size_t)w * h * 3), *wb = malloc((size_t)w * h * 3);
        for (int noise = 0; noise < 2; noise++) {
            wx_img_t g = {.px = (uint16_t *)gb, .a = gb + (size_t)w * h * 2, .w = w, .h = h, .x0 = c.x0, .y0 = c.y0};
            wx_img_t r = {.px = (uint16_t *)wb, .a = wb + (size_t)w * h * 2, .w = w, .h = h, .x0 = c.x0, .y0 = c.y0};
            uint32_t seed = rng;
            fill(&g, noise);
            rng = seed;
            fill(&r, noise);
            wx_paint_umbrella(&g);
            ref_umbrella(&r);
            int i = first_diff(&g, &r);
            CHECK(i < 0, "cover %d (%s): first diff at (%d, %d)", s, noise ? "noise" : "cleared", i < 0 ? 0 : c.x0 + i % w,
                  i < 0 ? 0 : c.y0 + i / w);
        }
        free(gb);
        free(wb);
    }
}

static void test_drips(void)
{
    for (int p = 0; p < WX_DRIP_POSES; p++) {
        uint8_t g[WX_DRIP_W * WX_DRIP_H], w[WX_DRIP_W * WX_DRIP_H];
        wx_paint_drip(g, p);
        ref_drip(w, p);
        CHECK(!memcmp(g, w, sizeof g), "drip pose %d", p);
    }
}

static void ref_bake_rain(const wx_temp_t *t, uint16_t *bg, wx_ring_t *ring, uint16_t *lut, uint8_t *m)
{
    const wx_rain_t *r = &t->rain;
    wx_paint_ring_rain(ring, r->intensity, !r->has_pop, lut);
    wx_img_t im = {.px = bg, .w = WX_FACE, .h = WX_FACE};
    wx_paint_clear(&im);
    ref_umbrella(&im);
    wx_paint_face_dots(&im, t->face, WXF_N);
    wx_ring_blit(ring, bg);
    wx_rain_layout_t l;
    wx_rain_layout(&l);
    for (int s = 0; s < WX_SHEETS; s++) {
        wx_paint_sheet(m + l.tile[s], s, r);
        wx_paint_ramp(m + l.top[s], s, false, l.ramp_h[s]);
        wx_paint_ramp(m + l.bottom[s], s, true, l.ramp_h[s]);
        wx_box_t c = l.cover_box[s];
        int w = c.x1 - c.x0, h = c.y1 - c.y0;
        wx_img_t cv = {.px = (uint16_t *)(m + l.cover[s]), .a = m + l.cover[s] + (size_t)w * h * 2, .w = w, .h = h,
                       .x0 = c.x0, .y0 = c.y0};
        wx_paint_clear(&cv);
        ref_umbrella(&cv);
    }
    for (int p = 0; p < WX_DRIP_POSES; p++) ref_drip(m + l.drip[p], p);
    for (int n = 0; n < WX_SPLASHES; n++) wx_paint_splash(m + l.splash[n], n);
    wx_paint_bolt(m + l.bolt);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static void test_bake(void)
{
    static uint16_t bg[WX_FACE * WX_FACE], rbg[WX_FACE * WX_FACE], lut[WX_RING_LUT], rlut[WX_RING_LUT];
    static uint8_t hero[WX_HERO_BOX * WX_HERO_BOX * 3];
    static wx_ring_t ring, rring;
    size_t sn = (size_t)wx_clouds_max_px(false) * 3;
    uint8_t *m = malloc(sn), *rm = malloc(sn), *drift = malloc((size_t)wx_clouds_max_px(true) * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .still = m, .drift = drift};
    wx_rain_layout_t l;
    wx_rain_layout(&l);
    static const struct {
        const char *name;
        wx_precip_t kind;
        int in;
        bool pop, dry;
    } CASES[] = {{"rain-light", WX_PRECIP_RAIN, 1, true, false}, {"rain-moderate", WX_PRECIP_RAIN, 2, true, false},
                 {"rain-heavy", WX_PRECIP_RAIN, 3, true, false}, {"snow", WX_PRECIP_SNOW, 2, true, false},
                 {"storm", WX_PRECIP_STORM, 3, false, false},    {"dry", WX_PRECIP_RAIN, 1, true, true}};
    wx_temp_t t = {0};
    t.face = WXF_RAIN;
    for (size_t c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
        t.rain = (wx_rain_t){.on = true, .kind = CASES[c].kind, .intensity = CASES[c].in, .has_pop = CASES[c].pop, .pop = 90,
                             .dry = CASES[c].dry};
        memset(m, 0x5A, sn);
        memset(rm, 0x5A, sn);
        wx_bake_out_t o;
        wx_bake(&t, WX_BAKE_FULL, &b, &o);
        ref_bake_rain(&t, rbg, &rring, rlut, rm);
        CHECK(!memcmp(bg, rbg, sizeof bg), "%s: bake_rain bg as the brute-force painters", CASES[c].name);
        CHECK(!memcmp(m, rm, (size_t)l.total), "%s: bake_rain still buffer as the brute-force painters", CASES[c].name);
    }
    const char *bench = getenv("CINDER_BENCH");
    bool on = bench && *bench && strcmp(bench, "0");
    int reps = on ? 50 : 3;
    double got_ms = 1e9, ref_ms = 1e9;
    wx_bake_out_t o;
    wx_bake(&t, WX_BAKE_FULL, &b, &o);
    ref_bake_rain(&t, rbg, &rring, rlut, rm);
    for (int k = 0; k < reps; k++) {
        double t0 = now_ms();
        wx_bake(&t, WX_BAKE_FULL, &b, &o);
        double t1 = now_ms();
        ref_bake_rain(&t, rbg, &rring, rlut, rm);
        double t2 = now_ms();
        if (t1 - t0 < got_ms) got_ms = t1 - t0;
        if (t2 - t1 < ref_ms) ref_ms = t2 - t1;
    }
    printf("umbrella: rain bake %.2f ms, brute-force reference %.2f ms (x%.1f, best of %d) on the host\n", got_ms, ref_ms,
           ref_ms / got_ms, reps);
    if (on) CHECK(got_ms * 2 < ref_ms, "CINDER_BENCH: the rain bake is well under half the brute-force reference");
    free(m);
    free(rm);
    free(drift);
}

int main(void)
{
    test_face(false);
    test_face(true);
    test_cover_boxes();
    test_drips();
    test_bake();
    if (failures) {
        printf("umbrella: %d failure(s)\n", failures);
        return 1;
    }
    printf("umbrella: all tests passed\n");
    return 0;
}
