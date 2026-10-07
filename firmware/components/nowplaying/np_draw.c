#include <math.h>
#include <string.h>

#include "np.h"

#define TWO_PI 6.283185307179586

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static uint16_t rgb565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint16_t ring_pixel(uint32_t arc, uint32_t track, float a, float t)
{
    float k = t * (1 - a);
    uint32_t r = (uint32_t)(((arc >> 16) & 0xFF) * a + ((track >> 16) & 0xFF) * k + 0.5f);
    uint32_t g = (uint32_t)(((arc >> 8) & 0xFF) * a + ((track >> 8) & 0xFF) * k + 0.5f);
    uint32_t b = (uint32_t)((arc & 0xFF) * a + (track & 0xFF) * k + 0.5f);
    return rgb565(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

void np_ring_point(const np_ring_t *ring, float frac, float *x, float *y)
{
    double a = TWO_PI * frac;
    *x = (float)(ring->cx + ring->r * sin(a));
    *y = (float)(ring->cy - ring->r * cos(a));
}

void np_ring_render(const np_ring_t *ring, uint16_t *buf, int w, int h, np_rect_t rect, float frac, uint32_t arc_rgb,
                    uint32_t track_rgb)
{
    int x0 = rect.x < 0 ? 0 : rect.x, y0 = rect.y < 0 ? 0 : rect.y;
    int x1 = rect.x + rect.w > w ? w : rect.x + rect.w, y1 = rect.y + rect.h > h ? h : rect.y + rect.h;
    frac = clamp01(frac);
    float ro = ring->r + ring->hw + 1, ri = ring->r - ring->hw - 1;
    float ex, ey, sx, sy;
    np_ring_point(ring, frac, &ex, &ey);
    np_ring_point(ring, 0, &sx, &sy);
    float end = (float)(TWO_PI * frac), edge = ring->hw + 0.5f;
    for (int y = y0; y < y1; y++) {
        float dy = y + 0.5f - ring->cy;
        if (dy * dy >= ro * ro) continue;
        float xo = sqrtf(ro * ro - dy * dy);
        float xi = ri > 0 && dy * dy < ri * ri ? sqrtf(ri * ri - dy * dy) : -1;
        int lo = (int)floorf(ring->cx - xo), hi = (int)ceilf(ring->cx + xo);
        if (lo < x0) lo = x0;
        if (hi > x1) hi = x1;
        uint16_t *row = buf + (size_t)y * w;
        for (int x = lo; x < hi; x++) {
            float dx = x + 0.5f - ring->cx;
            if (xi > 0 && fabsf(dx) < xi) {
                int skip = (int)ceilf(ring->cx + xi - 0.5f);
                if (skip > x) x = skip - 1;
                continue;
            }
            float d = sqrtf(dx * dx + dy * dy);
            float t = clamp01(edge - fabsf(d - ring->r));
            float a = 0;
            if (frac >= 1) {
                a = t;
            } else if (frac > 0) {
                float phi = atan2f(dx, -dy);
                if (phi < 0) phi += (float)TWO_PI;
                if (phi <= end) a = t;
                float ce = clamp01(edge - hypotf(x + 0.5f - ex, y + 0.5f - ey));
                float cs = clamp01(edge - hypotf(x + 0.5f - sx, y + 0.5f - sy));
                if (ce > a) a = ce;
                if (cs > a) a = cs;
            }
            row[x] = ring_pixel(arc_rgb, track_rgb, a, t);
        }
    }
}

static void grow(float *lo, float *hi, float v)
{
    if (v < *lo) *lo = v;
    if (v > *hi) *hi = v;
}

bool np_ring_dirty(const np_ring_t *ring, float f0, float f1, int w, int h, np_rect_t *out)
{
    f0 = clamp01(f0);
    f1 = clamp01(f1);
    if (f0 == f1) return false;
    double lo = fmin(f0, f1) * TWO_PI, hi = fmax(f0, f1) * TWO_PI;
    float x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    const double ext[] = {lo, hi, 0, TWO_PI / 4, TWO_PI / 2, TWO_PI * 3 / 4, TWO_PI};
    for (int i = 0; i < (int)(sizeof ext / sizeof ext[0]); i++) {
        if (ext[i] < lo || ext[i] > hi) continue;
        grow(&x0, &x1, (float)(ring->cx + ring->r * sin(ext[i])));
        grow(&y0, &y1, (float)(ring->cy - ring->r * cos(ext[i])));
    }
    float m = ring->hw + 2;
    int ix0 = ((int)floorf(x0 - m)) & ~1, iy0 = ((int)floorf(y0 - m)) & ~1;
    int ix1 = ((int)ceilf(x1 + m)) | 1, iy1 = ((int)ceilf(y1 + m)) | 1;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > w - 1) ix1 = w - 1;
    if (iy1 > h - 1) iy1 = h - 1;
    if (ix1 < ix0 || iy1 < iy0) return false;
    *out = (np_rect_t){ix0, iy0, ix1 - ix0 + 1, iy1 - iy0 + 1};
    return true;
}

static uint16_t scale565(uint16_t p, float k)
{
    uint32_t r = (uint32_t)(((p >> 11) & 31) * k + 0.5f), g = (uint32_t)(((p >> 5) & 63) * k + 0.5f),
             b = (uint32_t)((p & 31) * k + 0.5f);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void np_face_mask(uint16_t *buf, int w, int h, float cx, float cy, float r)
{
    float ri = r - 0.5f, ro = r + 0.5f;
    for (int y = 0; y < h; y++) {
        uint16_t *row = buf + (size_t)y * w;
        float dy = y + 0.5f - cy;
        if (dy * dy >= ro * ro) {
            memset(row, 0, (size_t)w * 2);
            continue;
        }
        float xo = sqrtf(ro * ro - dy * dy);
        float xi = ri > 0 && dy * dy < ri * ri ? sqrtf(ri * ri - dy * dy) : 0;
        int lo = (int)floorf(cx - xo), hi = (int)ceilf(cx + xo);
        if (lo < 0) lo = 0;
        if (hi > w) hi = w;
        if (lo > 0) memset(row, 0, (size_t)lo * 2);
        if (hi < w) memset(row + hi, 0, (size_t)(w - hi) * 2);
        for (int x = lo; x < hi; x++) {
            float dx = x + 0.5f - cx;
            if (fabsf(dx) < xi - 1) {
                int skip = (int)floorf(cx + xi - 1 - 0.5f);
                if (skip > x) x = skip;
                continue;
            }
            float k = clamp01(ro - sqrtf(dx * dx + dy * dy));
            if (k < 1) row[x] = k <= 0 ? 0 : scale565(row[x], k);
        }
    }
}

void np_circle_alpha(uint8_t *a, int size)
{
    float c = size / 2.0f, r = c - 0.5f;
    for (int y = 0; y < size; y++) {
        float dy = y + 0.5f - c;
        for (int x = 0; x < size; x++) {
            float dx = x + 0.5f - c;
            float k = clamp01(r + 0.5f - sqrtf(dx * dx + dy * dy));
            a[(size_t)y * size + x] = (uint8_t)(k * 255 + 0.5f);
        }
    }
}
