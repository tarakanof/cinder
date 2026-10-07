#include "ring_glint.h"

#include <math.h>
#include <string.h>

#define DEG (float)(M_PI / 180.0)

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void ring_glint_span_box(const ring_glint_t *g, float end_deg, float span_deg, ring_glint_box_t *out)
{
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    int n = (int)ceilf(span_deg / 2.0f);
    if (n < 1) n = 1;
    for (int i = 0; i <= n; i++) {
        float a = (end_deg - span_deg * (float)i / (float)n) * DEG;
        float x = g->cx + g->r * cosf(a), y = g->cy + g->r * sinf(a);
        minx = fminf(minx, x); maxx = fmaxf(maxx, x);
        miny = fminf(miny, y); maxy = fmaxf(maxy, y);
    }
    float pad = g->hw + 2;
    int x0 = ((int)floorf(minx - pad)) & ~1, y0 = ((int)floorf(miny - pad)) & ~1;
    int x1 = (int)ceilf(maxx + pad), y1 = (int)ceilf(maxy + pad);
    out->x = x0;
    out->y = y0;
    out->w = (x1 - x0 + 2) & ~1;
    out->h = (y1 - y0 + 2) & ~1;
}

void ring_glint_box(const ring_glint_t *g, float head_deg, ring_glint_box_t *out)
{
    ring_glint_span_box(g, head_deg, g->tail_deg, out);
}

static float atan2_deg(float y, float x)
{
    float ax = fabsf(x), ay = fabsf(y);
    float mx = fmaxf(ax, ay), mn = fminf(ax, ay);
    if (mx == 0) return 0;
    float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    if (y < 0) r = -r;
    return r / DEG;
}

void ring_glint_raster(const ring_glint_t *g, float head_deg, const ring_glint_box_t *b, uint8_t *alpha)
{
    float lo = fmaxf(g->r - g->hw - 1, 0), hi = g->r + g->hw + 1;
    float lo2 = lo * lo, hi2 = hi * hi;
    float hx = g->cx + g->r * cosf(head_deg * DEG), hy = g->cy + g->r * sinf(head_deg * DEG);
    float cap = g->hw + 1;
    for (int y = 0; y < b->h; y++) {
        float dy = (float)(b->y + y) + 0.5f - g->cy;
        uint8_t *row = alpha + y * b->w;
        memset(row, 0, (size_t)b->w);
        if (dy * dy > hi2) continue;
        float outer = sqrtf(hi2 - dy * dy), inner = lo2 > dy * dy ? sqrtf(lo2 - dy * dy) : 0;
        for (int side = -1; side <= 1; side += 2) {
            float a0 = side < 0 ? -outer : inner, a1 = side < 0 ? -inner : outer;
            int x0 = (int)floorf(g->cx + a0 - 0.5f) - b->x, x1 = (int)ceilf(g->cx + a1 - 0.5f) - b->x;
            if (x0 < 0) x0 = 0;
            if (x1 > b->w - 1) x1 = b->w - 1;
            for (int x = x0; x <= x1; x++) {
                float dx = (float)(b->x + x) + 0.5f - g->cx;
                float d2 = dx * dx + dy * dy;
                if (d2 < lo2 || d2 > hi2) continue;
                float behind = head_deg - atan2_deg(dy, dx);
                behind -= 360.0f * floorf((behind + 180.0f) / 360.0f);
                float cov = 0;
                if (behind >= 0 && behind <= g->tail_deg) {
                    float radial = clamp01(g->hw + 0.5f - fabsf(sqrtf(d2) - g->r));
                    cov = radial * (1 - behind / g->tail_deg);
                }
                float ex = dx + g->cx - hx, ey = dy + g->cy - hy;
                if (fabsf(ex) < cap && fabsf(ey) < cap)
                    cov = fmaxf(cov, clamp01(g->hw + 0.5f - sqrtf(ex * ex + ey * ey)));
                row[x] = (uint8_t)(cov * 255 + 0.5f);
            }
        }
    }
}

uint32_t ring_glint_color(uint32_t rgb, float mix)
{
    uint32_t out = 0;
    for (int s = 0; s <= 16; s += 8) {
        float c = (float)((rgb >> s) & 0xFF);
        out |= (uint32_t)lroundf(c + (255 - c) * mix) << s;
    }
    return out;
}
