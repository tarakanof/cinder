#include "usage.h"

#include <math.h>
#include <string.h>

#include "wx_clock.h"

#define DEG (3.14159265358979f / 180.0f)

typedef struct {
    float r, hw;
    float sx, sy, ex, ey;
    bool none, full, big;
} arc_t;

static void arc_init(arc_t *c, float r, float hw, float a0, float a1)
{
    float span = a1 - a0;
    *c = (arc_t){.r = r, .hw = hw, .none = span <= 0, .full = span >= 360.0f, .big = span > 180.0f};
    c->sx = sinf(a0 * DEG), c->sy = -cosf(a0 * DEG);
    c->ex = sinf(a1 * DEG), c->ey = -cosf(a1 * DEG);
}

static bool arc_inside(const arc_t *c, float dx, float dy)
{
    float cs = c->sx * dy - c->sy * dx, ce = c->ex * dy - c->ey * dx;
    return c->big ? !(ce > 0 && cs < 0) : cs >= 0 && ce <= 0;
}

static float clamp01(float v) { return v <= 0 ? 0 : v >= 1 ? 1 : v; }

static float arc_cov_d(const arc_t *c, float dx, float dy, float d)
{
    if (c->none) return 0;
    float lim = c->hw + 0.5f, radial = fabsf(d - c->r);
    if (c->full || arc_inside(c, dx, dy)) return clamp01(lim - radial);
    if (radial >= lim) return 0;
    float ax = dx - c->r * c->sx, ay = dy - c->r * c->sy, bx = dx - c->r * c->ex, by = dy - c->r * c->ey;
    float e2 = fminf(ax * ax + ay * ay, bx * bx + by * by);
    if (e2 >= lim * lim) return 0;
    return clamp01(lim - sqrtf(e2));
}

float usage_arc_cover(float dx, float dy, float r, float hw, float a0, float a1)
{
    arc_t c;
    arc_init(&c, r, hw, a0, a1);
    return arc_cov_d(&c, dx, dy, sqrtf(dx * dx + dy * dy));
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
            float dx = x + i + 0.5f - cx, dy = y + j + 0.5f - cy;
            out[j * w + i] = (uint8_t)(arc_cov_d(&a, dx, dy, sqrtf(dx * dx + dy * dy)) * 255.0f + 0.5f);
        }
}

void usage_dot_xy(int k, int n, float *x, float *y) { wx_arc_dot_xy(k, n, USAGE_DOT_ARC, USAGE_DOT_PITCH, USAGE_CX, USAGE_CY, x, y); }

void usage_dots_box(int *x, int *y, int *w, int *h)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int n = 2; n <= USAGE_MAX; n++)
        for (int k = 0; k < n; k++) {
            float cx, cy;
            usage_dot_xy(k, n, &cx, &cy);
            x0 = fminf(x0, cx), x1 = fmaxf(x1, cx), y0 = fminf(y0, cy), y1 = fmaxf(y1, cy);
        }
    float m = USAGE_DOT_R + 2;
    int ix0 = ((int)floorf(x0 - m)) & ~1, iy0 = (int)floorf(y0 - m);
    int ix1 = (int)ceilf(x1 + m), iy1 = (int)ceilf(y1 + m);
    *x = ix0;
    *y = iy0;
    *w = (ix1 - ix0 + 2) & ~1;
    *h = iy1 - iy0 + 1;
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

typedef void (*span_fn)(uint16_t *row, int x0, int x1, float dy, const void *ctx);

static void spans(uint16_t *buf, int w, int h, float r_in, float r_out, int y_from, span_fn fn, const void *ctx)
{
    int y0 = (int)floorf(USAGE_CY - r_out), y1 = (int)ceilf(USAGE_CY + r_out);
    if (y0 < y_from) y0 = y_from;
    if (y0 < 0) y0 = 0;
    if (y1 > h - 1) y1 = h - 1;
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - USAGE_CY, o2 = r_out * r_out - dy * dy;
        if (o2 <= 0) continue;
        float xo = sqrtf(o2), i2 = r_in * r_in - dy * dy;
        int a0 = (int)floorf(USAGE_CX - xo), b1 = (int)ceilf(USAGE_CX + xo);
        if (a0 < 0) a0 = 0;
        if (b1 > w - 1) b1 = w - 1;
        uint16_t *row = buf + (size_t)y * w;
        if (i2 > 0) {
            float xi = sqrtf(i2);
            int a1 = (int)ceilf(USAGE_CX - xi), b0 = (int)floorf(USAGE_CX + xi);
            fn(row, a0, a1 < b1 ? a1 : b1, dy, ctx);
            fn(row, b0 > a0 ? b0 : a0, b1, dy, ctx);
        } else {
            fn(row, a0, b1, dy, ctx);
        }
    }
}

typedef struct {
    arc_t track, arc;
    uint32_t track_rgb, arc_rgb;
    uint16_t track565, arc565;
} ring_ctx_t;

static void ring_span(uint16_t *row, int x0, int x1, float dy, const void *p)
{
    const ring_ctx_t *c = p;
    for (int x = x0; x <= x1; x++) {
        float dx = x + 0.5f - USAGE_CX, d = sqrtf(dx * dx + dy * dy);
        float t = clamp01(c->track.hw + 0.5f - fabsf(d - c->track.r));
        if (t <= 0) {
            row[x] = 0;
            continue;
        }
        float a = arc_cov_d(&c->arc, dx, dy, d);
        if (a >= 1) row[x] = c->arc565;
        else if (a <= 0) row[x] = t >= 1 ? c->track565 : rgb565(over(0, c->track_rgb, t));
        else row[x] = rgb565(over(over(0, c->track_rgb, t), c->arc_rgb, a));
    }
}

static float frac_deg(float f) { return f <= 0 ? 0 : f >= 1 ? 360.0f : f * 360.0f; }

static void ring(uint16_t *buf, int w, int h, float r, float hw, float frac, uint32_t track, uint32_t rgb)
{
    ring_ctx_t c = {.track_rgb = track, .arc_rgb = rgb, .track565 = rgb565(track), .arc565 = rgb565(rgb)};
    arc_init(&c.track, r, hw, 0, 360.0f);
    arc_init(&c.arc, r, hw, 0, frac_deg(frac));
    spans(buf, w, h, r - hw - 1, r + hw + 1, 0, ring_span, &c);
}

void usage_ring5_render(uint16_t *buf, int w, int h, const usage_face_t *f)
{
    ring(buf, w, h, USAGE_R5, USAGE_HW5, f->five_frac, USAGE_TRACK, f->five_rgb);
}

void usage_ring7_render(uint16_t *buf, int w, int h, const usage_face_t *f)
{
    ring(buf, w, h, USAGE_R7, USAGE_HW7, f->week_frac, USAGE_TRACK2, f->week_rgb);
}

static void clear_span(uint16_t *row, int x0, int x1, float dy, const void *ctx)
{
    (void)dy, (void)ctx;
    if (x1 >= x0) memset(row + x0, 0, (size_t)(x1 - x0 + 1) * sizeof *row);
}

void usage_dots_render(uint16_t *buf, int w, int h, int n, int shown)
{
    float m = USAGE_DOT_R + 2;
    spans(buf, w, h, USAGE_DOT_ARC - m, USAGE_DOT_ARC + m, (int)(USAGE_CY + USAGE_DOT_ARC / 2), clear_span, NULL);
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

void usage_rings_render(uint16_t *buf, int w, int h, const usage_face_t *f, int n, int shown)
{
    memset(buf, 0, (size_t)w * h * sizeof *buf);
    usage_ring5_render(buf, w, h, f);
    usage_ring7_render(buf, w, h, f);
    usage_dots_render(buf, w, h, n, shown);
}
