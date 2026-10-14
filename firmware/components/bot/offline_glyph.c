#include "offline_glyph.h"

#include <math.h>

#define PIVOT_X 31.0f
#define PIVOT_Y 42.0f
#define HALF_DEG 45.0f
#define HW 3.5f
#define DOT_R 5.0f
#define CUT 2.5f
#define SLASH_X0 10.0f
#define SLASH_Y0 4.0f
#define SLASH_X1 52.0f
#define SLASH_Y1 46.0f

static const float ARC_R[] = {14.0f, 25.5f, 37.0f};

static float cover(float d) { return d >= 0.5f ? 0 : d <= -0.5f ? 1 : 0.5f - d; }

static float arc_dist(float x, float y, float r)
{
    float dx = x - PIVOT_X, dy = y - PIVOT_Y;
    float half = HALF_DEG * (float)M_PI / 180.0f;
    if (fabsf(atan2f(dx, -dy)) <= half) return fabsf(hypotf(dx, dy) - r) - HW;
    float ex = r * sinf(half), ey = -r * cosf(half);
    return hypotf(fabsf(dx) - ex, dy - ey) - HW;
}

static float slash_dist(float x, float y)
{
    float vx = SLASH_X1 - SLASH_X0, vy = SLASH_Y1 - SLASH_Y0;
    float t = ((x - SLASH_X0) * vx + (y - SLASH_Y0) * vy) / (vx * vx + vy * vy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypotf(x - SLASH_X0 - t * vx, y - SLASH_Y0 - t * vy);
}

void offline_glyph_raster(uint8_t *a8)
{
    for (int py = 0; py < OFFLINE_GLYPH_H; py++) {
        for (int px = 0; px < OFFLINE_GLYPH_W; px++) {
            float x = (float)px + 0.5f, y = (float)py + 0.5f;
            float fan = cover(hypotf(x - PIVOT_X, y - PIVOT_Y) - DOT_R);
            for (int k = 0; k < 3; k++) fan = fmaxf(fan, cover(arc_dist(x, y, ARC_R[k])));
            float s = slash_dist(x, y);
            float a = fmaxf(fan * (1 - cover(s - HW - CUT)), cover(s - HW));
            a8[py * OFFLINE_GLYPH_W + px] = (uint8_t)lroundf(a * 255);
        }
    }
}
