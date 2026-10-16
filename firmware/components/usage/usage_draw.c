#include "usage.h"

#include <math.h>

#define DEG (3.14159265358979f / 180.0f)

typedef struct {
    float r, hw, a0, span;
    bool full;
    float x0, y0, x1, y1;
} arc_t;

static void arc_init(arc_t *c, float r, float hw, float a0, float a1)
{
    *c = (arc_t){.r = r, .hw = hw, .a0 = a0, .span = a1 - a0, .full = a1 - a0 >= 360.0f};
    c->x0 = r * sinf(a0 * DEG), c->y0 = -r * cosf(a0 * DEG);
    c->x1 = r * sinf(a1 * DEG), c->y1 = -r * cosf(a1 * DEG);
}

static float arc_cov(const arc_t *c, float dx, float dy)
{
    if (c->span <= 0) return 0;
    float dist = fabsf(hypotf(dx, dy) - c->r);
    if (dist > c->hw + 0.5f && !c->full) {
        float e = fminf(hypotf(dx - c->x0, dy - c->y0), hypotf(dx - c->x1, dy - c->y1));
        if (e > c->hw + 0.5f) return 0;
    }
    if (!c->full) {
        float a = atan2f(dx, -dy) / DEG;
        float rel = fmodf(a - c->a0 + 720.0f, 360.0f);
        if (rel > c->span) dist = fminf(hypotf(dx - c->x0, dy - c->y0), hypotf(dx - c->x1, dy - c->y1));
    }
    float v = c->hw + 0.5f - dist;
    return v <= 0 ? 0 : v >= 1 ? 1 : v;
}

float usage_arc_cover(float dx, float dy, float r, float hw, float a0, float a1)
{
    arc_t c;
    arc_init(&c, r, hw, a0, a1);
    return arc_cov(&c, dx, dy);
}

void usage_arc_box(float cx, float cy, float r, float hw, float a0, float a1, int *x, int *y, int *w, int *h)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (float a = a0;; a += 1.0f) {
        if (a > a1) a = a1;
        float px = cx + r * sinf(a * DEG), py = cy - r * cosf(a * DEG);
        x0 = fminf(x0, px), x1 = fmaxf(x1, px), y0 = fminf(y0, py), y1 = fmaxf(y1, py);
        if (a >= a1) break;
    }
    float m = hw + 2;
    int ix0 = ((int)floorf(x0 - m)) & ~1, iy0 = (int)floorf(y0 - m);
    int ix1 = (int)ceilf(x1 + m), iy1 = (int)ceilf(y1 + m);
    *x = ix0;
    *y = iy0;
    *w = (ix1 - ix0 + 2) & ~1;
    *h = iy1 - iy0 + 1;
}

void usage_arc_mask(float cx, float cy, float r, float hw, float a0, float a1, int x, int y, int w, int h, uint8_t *out)
{
    arc_t a;
    arc_init(&a, r, hw, a0, a1);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            float c = arc_cov(&a, x + i + 0.5f - cx, y + j + 0.5f - cy);
            out[j * w + i] = (uint8_t)(c * 255.0f + 0.5f);
        }
}

void usage_dot_xy(int k, int n, float *x, float *y)
{
    float a = ((float)k - (float)(n - 1) / 2) * USAGE_DOT_PITCH / USAGE_DOT_ARC;
    *x = USAGE_CX + USAGE_DOT_ARC * sinf(a);
    *y = USAGE_CY + USAGE_DOT_ARC * cosf(a);
}

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xFF) >> 3));
}

static uint32_t over(uint32_t dst, uint32_t src, float a)
{
    uint32_t out = 0;
    for (int s = 0; s <= 16; s += 8) {
        float d = (float)((dst >> s) & 0xFF), c = (float)((src >> s) & 0xFF);
        out |= (uint32_t)(d + (c - d) * a + 0.5f) << s;
    }
    return out;
}

static uint32_t ring_px(const arc_t *track_arc, const arc_t *arc, float dx, float dy, uint32_t track, uint32_t rgb)
{
    float t = arc_cov(track_arc, dx, dy);
    if (t <= 0) return 0;
    uint32_t px = over(0, track, t);
    float a = arc_cov(arc, dx, dy);
    return a > 0 ? over(px, rgb, a) : px;
}

static float frac_deg(float f) { return f <= 0 ? 0 : f >= 1 ? 360.0f : f * 360.0f; }

void usage_rings_render(uint16_t *buf, int w, int h, const usage_face_t *f, int n, int shown)
{
    arc_t t5, a5, t7, a7;
    arc_init(&t5, USAGE_R5, USAGE_HW5, 0, 360.0f);
    arc_init(&a5, USAGE_R5, USAGE_HW5, 0, frac_deg(f->five_frac));
    arc_init(&t7, USAGE_R7, USAGE_HW7, 0, 360.0f);
    arc_init(&a7, USAGE_R7, USAGE_HW7, 0, frac_deg(f->week_frac));
    float b5o = USAGE_R5 + USAGE_HW5 + 1, b5i = USAGE_R5 - USAGE_HW5 - 1;
    float b7o = USAGE_R7 + USAGE_HW7 + 1, b7i = USAGE_R7 - USAGE_HW7 - 1;
    for (int y = 0; y < h; y++) {
        float dy = y + 0.5f - USAGE_CY;
        for (int x = 0; x < w; x++) {
            float dx = x + 0.5f - USAGE_CX, d2 = dx * dx + dy * dy;
            uint32_t px = 0;
            if (d2 <= b5o * b5o && d2 >= b5i * b5i)
                px = ring_px(&t5, &a5, dx, dy, USAGE_TRACK, f->five_rgb);
            else if (d2 <= b7o * b7o && d2 >= b7i * b7i)
                px = ring_px(&t7, &a7, dx, dy, USAGE_TRACK2, f->week_rgb);
            buf[y * w + x] = px ? rgb565(px) : 0;
        }
    }
    if (n < 2) return;
    for (int k = 0; k < n; k++) {
        float cx, cy;
        usage_dot_xy(k, n, &cx, &cy);
        uint32_t c = k == shown ? USAGE_DOT_ON : USAGE_DOT_OFF;
        for (int y = (int)(cy - USAGE_DOT_R - 1); y <= (int)(cy + USAGE_DOT_R + 1); y++)
            for (int x = (int)(cx - USAGE_DOT_R - 1); x <= (int)(cx + USAGE_DOT_R + 1); x++) {
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                float a = USAGE_DOT_R + 0.5f - hypotf(x + 0.5f - cx, y + 0.5f - cy);
                if (a <= 0) continue;
                buf[y * w + x] = rgb565(over(0, c, a > 1 ? 1 : a));
            }
    }
}
