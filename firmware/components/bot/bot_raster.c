#include "bot_raster.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

static float seg_dist2(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float l2 = vx * vx + vy * vy;
    float t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float dx = wx - t * vx, dy = wy - t * vy;
    return dx * dx + dy * dy;
}

static uint16_t rgb565_scaled(uint32_t rgb, float k)
{
    uint32_t r = (uint32_t)(((rgb >> 16) & 0xFF) * k);
    uint32_t g = (uint32_t)(((rgb >> 8) & 0xFF) * k);
    uint32_t b = (uint32_t)((rgb & 0xFF) * k);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static bool capsule_span(float ax, float ay, float bx, float by, float cy, float r, float *lo, float *hi)
{
    float l = INFINITY, h = -INFINITY;
    float dy = cy - ay;
    if (dy * dy <= r * r) { float s = sqrtf(r * r - dy * dy); l = fminf(l, ax - s); h = fmaxf(h, ax + s); }
    dy = cy - by;
    if (dy * dy <= r * r) { float s = sqrtf(r * r - dy * dy); l = fminf(l, bx - s); h = fmaxf(h, bx + s); }
    float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
    if (l2 > 1e-6f) {
        float len = sqrtf(l2), ey = cy - ay;
        float s0 = -INFINITY, s1 = INFINITY;
        if (fabsf(vx) > 1e-6f) {
            float a = -ey * vy / vx, b = (l2 - ey * vy) / vx;
            s0 = fmaxf(s0, fminf(a, b)); s1 = fminf(s1, fmaxf(a, b));
        } else if (!(-ey * vy <= 0 && 0 <= l2 - ey * vy)) {
            s1 = -INFINITY;
        }
        if (fabsf(vy) > 1e-6f) {
            float a = (ey * vx - r * len) / vy, b = (ey * vx + r * len) / vy;
            s0 = fmaxf(s0, fminf(a, b)); s1 = fminf(s1, fmaxf(a, b));
        } else if (!(fabsf(ey * vx) <= r * len)) {
            s1 = -INFINITY;
        }
        if (s0 <= s1) { l = fminf(l, ax + s0); h = fmaxf(h, ax + s1); }
    }
    *lo = l;
    *hi = h;
    return l <= h;
}

void bot_raster_stroke(bot_raster_scratch_t *scratch, uint16_t *buf, uint8_t *alpha, int w, int h, int ox,
                       int oy, const float *xs, const float *ys, int nseg, const int *segs, float hw, uint32_t rgb,
                       float gain)
{
    int *row_segs = scratch->row_segs;
    uint8_t *state = scratch->state;
    const float r_out = hw + 0.5f, r_in = hw - 0.5f, out2 = r_out * r_out;
    const uint16_t solid = rgb565_scaled(rgb, gain);
    for (int y = 0; y < h; y++) {
        float cy = oy + y + 0.5f;
        int n = 0;
        for (int k = 0; k < nseg; k++) {
            int i = segs[k];
            float y0 = fminf(ys[i], ys[i + 1]) - r_out, y1 = fmaxf(ys[i], ys[i + 1]) + r_out;
            if (cy >= y0 && cy <= y1) row_segs[n++] = i;
        }
        uint16_t *row = buf ? buf + y * w : NULL;
        uint8_t *arow = alpha ? alpha + y * w : NULL;
        memset(state, 0, (size_t)w);
        for (int k = 0; k < n; k++) {
            int i = row_segs[k];
            float lo, hi;
            if (!capsule_span(xs[i], ys[i], xs[i + 1], ys[i + 1], cy, r_out, &lo, &hi)) continue;
            int x0 = (int)ceilf(lo - ox - 0.5f), x1 = (int)floorf(hi - ox - 0.5f);
            if (x0 < 0) x0 = 0;
            if (x1 > w - 1) x1 = w - 1;
            for (int x = x0; x <= x1; x++) if (!state[x]) state[x] = 1;
            if (r_in > 0 && capsule_span(xs[i], ys[i], xs[i + 1], ys[i + 1], cy, r_in, &lo, &hi)) {
                x0 = (int)ceilf(lo - ox - 0.5f); x1 = (int)floorf(hi - ox - 0.5f);
                if (x0 < 0) x0 = 0;
                if (x1 > w - 1) x1 = w - 1;
                for (int x = x0; x <= x1; x++) state[x] = 2;
            }
        }
        for (int x = 0; x < w; x++) {
            float cov;
            if (state[x] == 0) {
                cov = 0;
            } else if (state[x] == 2) {
                cov = 1;
            } else {
                float cx = ox + x + 0.5f, d2 = out2;
                for (int k = 0; k < n; k++) {
                    int i = row_segs[k];
                    float s = seg_dist2(cx, cy, xs[i], ys[i], xs[i + 1], ys[i + 1]);
                    if (s < d2) d2 = s;
                }
                cov = d2 >= out2 ? 0 : hw - sqrtf(d2) + 0.5f;
                if (cov > 1) cov = 1;
            }
            if (alpha) {
                if (row) row[x] = solid;
                arow[x] = cov <= 0 ? 0 : (uint8_t)(cov * 255);
            } else {
                row[x] = cov <= 0 ? 0 : (cov >= 1 ? solid : rgb565_scaled(rgb, cov * gain));
            }
        }
    }
}

void bot_raster_stroke_ref(uint16_t *buf, uint8_t *alpha, int w, int h, int ox, int oy, const float *xs,
                           const float *ys, int nseg, const int *segs, float hw, uint32_t rgb, float gain)
{
    const float out2 = (hw + 0.5f) * (hw + 0.5f);
    const uint16_t solid = rgb565_scaled(rgb, gain);
    for (int y = 0; y < h; y++) {
        float cy = oy + y + 0.5f;
        for (int x = 0; x < w; x++) {
            float cx = ox + x + 0.5f, d2 = out2;
            for (int k = 0; k < nseg; k++) {
                int i = segs[k];
                float s = seg_dist2(cx, cy, xs[i], ys[i], xs[i + 1], ys[i + 1]);
                if (s < d2) d2 = s;
            }
            float cov = d2 >= out2 ? 0 : hw - sqrtf(d2) + 0.5f;
            if (cov > 1) cov = 1;
            if (alpha) {
                buf[y * w + x] = solid;
                alpha[y * w + x] = cov <= 0 ? 0 : (uint8_t)(cov * 255);
            } else {
                buf[y * w + x] = cov <= 0 ? 0 : (cov >= 1 ? solid : rgb565_scaled(rgb, cov * gain));
            }
        }
    }
}
