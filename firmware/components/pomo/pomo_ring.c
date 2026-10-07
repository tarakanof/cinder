#include "pomo_ring.h"

#include <math.h>
#include <string.h>

#define TWO_PI 6.283185307179586

static void grid_point(const pomo_ring_t *ring, int k, float *x, float *y)
{
    if (k == POMO_RING_PTS) k = 0;
    double a = TWO_PI * k / POMO_RING_PTS;
    *x = (float)(ring->cx + ring->r * sin(a));
    *y = (float)(ring->cy - ring->r * cos(a));
}

int pomo_ring_points(const pomo_ring_t *ring, pomo_ring_work_t *wk, float frac)
{
    for (int k = 0; k <= POMO_RING_PTS; k++) grid_point(ring, k, &wk->tx[k], &wk->ty[k]);
    if (frac <= 0) return 0;
    if (frac >= 1) {
        memcpy(wk->ax, wk->tx, sizeof wk->tx);
        memcpy(wk->ay, wk->ty, sizeof wk->ty);
        return POMO_RING_PTS;
    }
    double pos = (double)frac * POMO_RING_PTS;
    int k = (int)floor(pos);
    float u = (float)(pos - k);
    for (int i = 0; i <= k; i++) { wk->ax[i] = wk->tx[i]; wk->ay[i] = wk->ty[i]; }
    int n = k;
    if (u > 1e-4f) {
        wk->ax[k + 1] = wk->tx[k] + u * (wk->tx[k + 1] - wk->tx[k]);
        wk->ay[k + 1] = wk->ty[k] + u * (wk->ty[k + 1] - wk->ty[k]);
        n = k + 1;
    } else if (k == 0) {
        wk->ax[1] = wk->ax[0];
        wk->ay[1] = wk->ay[0];
        n = 1;
    }
    return n;
}

static uint8_t mix(uint32_t arc_c, uint32_t track_c, uint32_t a, uint32_t t)
{
    return (uint8_t)((arc_c * a * 255 + track_c * t * (255 - a)) / (255 * 255));
}

uint16_t pomo_ring_pixel(uint32_t arc_rgb, uint32_t track_rgb, uint8_t a, uint8_t t)
{
    uint32_t r = mix((arc_rgb >> 16) & 0xFF, (track_rgb >> 16) & 0xFF, a, t);
    uint32_t g = mix((arc_rgb >> 8) & 0xFF, (track_rgb >> 8) & 0xFF, a, t);
    uint32_t b = mix(arc_rgb & 0xFF, track_rgb & 0xFF, a, t);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static int segs_in_box(const float *xs, const float *ys, int nseg, float m, int x0, int y0, int x1, int y1,
                       int *segs)
{
    int n = 0;
    for (int i = 0; i < nseg; i++) {
        float lx = fminf(xs[i], xs[i + 1]) - m, hx = fmaxf(xs[i], xs[i + 1]) + m;
        float ly = fminf(ys[i], ys[i + 1]) - m, hy = fmaxf(ys[i], ys[i + 1]) + m;
        if (hx >= x0 && lx <= x1 && hy >= y0 && ly <= y1) segs[n++] = i;
    }
    return n;
}

static bool box_touches_ring(const pomo_ring_t *ring, int x0, int y0, int x1, int y1)
{
    float nx = fmaxf((float)x0, fminf(ring->cx, (float)x1)) - ring->cx;
    float ny = fmaxf((float)y0, fminf(ring->cy, (float)y1)) - ring->cy;
    float fx = fmaxf(fabsf(x0 - ring->cx), fabsf(x1 - ring->cx));
    float fy = fmaxf(fabsf(y0 - ring->cy), fabsf(y1 - ring->cy));
    float dmin = sqrtf(nx * nx + ny * ny), dmax = sqrtf(fx * fx + fy * fy);
    float m = ring->hw + 2;
    return dmin <= ring->r + m && dmax >= ring->r - m;
}

void pomo_ring_render(const pomo_ring_t *ring, pomo_ring_work_t *wk, uint16_t *buf, int buf_w, int buf_h,
                      pomo_rect_t rect, float frac, uint32_t arc_rgb, uint32_t track_rgb)
{
    int rx0 = rect.x < 0 ? 0 : rect.x, ry0 = rect.y < 0 ? 0 : rect.y;
    int rx1 = rect.x + rect.w > buf_w ? buf_w : rect.x + rect.w;
    int ry1 = rect.y + rect.h > buf_h ? buf_h : rect.y + rect.h;
    int narc = pomo_ring_points(ring, wk, frac);
    float m = ring->hw + 1;
    for (int ty = ry0; ty < ry1; ty += POMO_RING_TILE) {
        for (int tx = rx0; tx < rx1; tx += POMO_RING_TILE) {
            int w = rx1 - tx < POMO_RING_TILE ? rx1 - tx : POMO_RING_TILE;
            int h = ry1 - ty < POMO_RING_TILE ? ry1 - ty : POMO_RING_TILE;
            if (!box_touches_ring(ring, tx, ty, tx + w, ty + h)) continue;

            int n = segs_in_box(wk->tx, wk->ty, POMO_RING_PTS, m, tx, ty, tx + w, ty + h, wk->segs);
            if (n) {
                bot_raster_stroke(&wk->scratch, NULL, wk->a_track, w, h, tx, ty, wk->tx, wk->ty, n, wk->segs,
                                  ring->hw, 0, 1.0f);
            } else {
                memset(wk->a_track, 0, (size_t)(w * h));
            }
            n = narc ? segs_in_box(wk->ax, wk->ay, narc, m, tx, ty, tx + w, ty + h, wk->segs) : 0;
            if (n) {
                bot_raster_stroke(&wk->scratch, NULL, wk->a_arc, w, h, tx, ty, wk->ax, wk->ay, n, wk->segs,
                                  ring->hw, 0, 1.0f);
            } else {
                memset(wk->a_arc, 0, (size_t)(w * h));
            }
            for (int y = 0; y < h; y++) {
                uint16_t *row = buf + (size_t)(ty + y) * buf_w + tx;
                const uint8_t *at = wk->a_track + y * w, *aa = wk->a_arc + y * w;
                for (int x = 0; x < w; x++) row[x] = pomo_ring_pixel(arc_rgb, track_rgb, aa[x], at[x]);
            }
        }
    }
}

static void grow(float *lo, float *hi, float v)
{
    if (v < *lo) *lo = v;
    if (v > *hi) *hi = v;
}

bool pomo_ring_dirty(const pomo_ring_t *ring, float f0, float f1, int buf_w, int buf_h, pomo_rect_t *out)
{
    f0 = f0 < 0 ? 0 : (f0 > 1 ? 1 : f0);
    f1 = f1 < 0 ? 0 : (f1 > 1 ? 1 : f1);
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
    if (ix1 > buf_w - 1) ix1 = buf_w - 1;
    if (iy1 > buf_h - 1) iy1 = buf_h - 1;
    if (ix1 < ix0 || iy1 < iy0) return false;
    *out = (pomo_rect_t){ix0, iy0, ix1 - ix0 + 1, iy1 - iy0 + 1};
    return true;
}
