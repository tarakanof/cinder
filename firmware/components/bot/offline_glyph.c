#include "offline_glyph.h"

#include <math.h>

#define PIVOT_X 31.0f
#define PIVOT_Y 43.0f
#define HALF_DEG 45.0f
#define HW 3.5f
#define DOT_R 4.0f
#define X_CX 50.5f
#define X_CY 44.0f
#define X_ARM 5.0f
#define X_HW 2.25f

static const float ARC_R[] = {16.0f, 27.0f, 38.0f};

static float cover(float d) { return d >= 0.5f ? 0 : d <= -0.5f ? 1 : 0.5f - d; }

static float arc_dist(float x, float y, float r)
{
    float dx = x - PIVOT_X, dy = y - PIVOT_Y;
    float half = HALF_DEG * (float)M_PI / 180.0f;
    if (fabsf(atan2f(dx, -dy)) <= half) return fabsf(hypotf(dx, dy) - r) - HW;
    float ex = r * sinf(half), ey = -r * cosf(half);
    return hypotf(fabsf(dx) - ex, dy - ey) - HW;
}

static float seg_dist(float x, float y, float x0, float y0, float x1, float y1)
{
    float vx = x1 - x0, vy = y1 - y0;
    float t = ((x - x0) * vx + (y - y0) * vy) / (vx * vx + vy * vy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypotf(x - x0 - t * vx, y - y0 - t * vy);
}

static float cross_dist(float x, float y)
{
    float a = seg_dist(x, y, X_CX - X_ARM, X_CY - X_ARM, X_CX + X_ARM, X_CY + X_ARM);
    float b = seg_dist(x, y, X_CX - X_ARM, X_CY + X_ARM, X_CX + X_ARM, X_CY - X_ARM);
    return fminf(a, b) - X_HW;
}

void offline_glyph_raster(uint8_t *a8)
{
    for (int py = 0; py < OFFLINE_GLYPH_H; py++) {
        for (int px = 0; px < OFFLINE_GLYPH_W; px++) {
            float x = (float)px + 0.5f, y = (float)py + 0.5f;
            float a = cover(hypotf(x - PIVOT_X, y - PIVOT_Y) - DOT_R);
            for (int k = 0; k < 3; k++) a = fmaxf(a, cover(arc_dist(x, y, ARC_R[k])));
            a = fmaxf(a, cover(cross_dist(x, y)));
            a8[py * OFFLINE_GLYPH_W + px] = (uint8_t)lroundf(a * 255);
        }
    }
}
