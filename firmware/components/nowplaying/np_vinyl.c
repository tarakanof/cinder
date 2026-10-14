#include <math.h>

#include "np_cover.h"

#define TWO_PI 6.283185307179586f
#define FACE_C (NP_COVER_PX / 2.0f)
#define NOTE_R 27
#define SS 4
#define FLAG_N 17
#define TEXT_PAD 16
#define DRIFT 8

typedef struct {
    float r, g, b;
} col_t;

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static col_t hex(uint32_t c) { return (col_t){(float)((c >> 16) & 0xFF), (float)((c >> 8) & 0xFF), (float)(c & 0xFF)}; }

static col_t over(col_t a, col_t b, float t)
{
    return (col_t){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

static uint16_t pack(col_t c)
{
    int r = (int)(c.r + 0.5f), g = (int)(c.g + 0.5f), b = (int)(c.b + 0.5f);
    r = r > 255 ? 255 : r < 0 ? 0 : r;
    g = g > 255 ? 255 : g < 0 ? 0 : g;
    b = b > 255 ? 255 : b < 0 ? 0 : b;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = clamp01((wx * vx + wy * vy) / (vx * vx + vy * vy));
    return hypotf(wx - vx * t, wy - vy * t);
}

static void quad(float *xs, float *ys, int at, int n, float x0, float y0, float cx, float cy, float x1, float y1)
{
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) / n, u = 1 - t;
        xs[at + i] = u * u * x0 + 2 * u * t * cx + t * t * x1;
        ys[at + i] = u * u * y0 + 2 * u * t * cy + t * t * y1;
    }
}

static void flag_poly(float *xs, float *ys)
{
    xs[0] = 237;
    ys[0] = 180;
    quad(xs, ys, 1, 8, 237, 180, 251, 184, 249, 198);
    quad(xs, ys, 9, 8, 249, 198, 247, 190, 237, 190);
}

static bool in_poly(const float *xs, const float *ys, int n, float x, float y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if ((ys[i] > y) != (ys[j] > y) && x < (xs[j] - xs[i]) * (y - ys[i]) / (ys[j] - ys[i]) + xs[i]) in = !in;
    return in;
}

static bool in_note(const float *fx, const float *fy, float x, float y)
{
    const float c = 0.93969262f, s = -0.34202014f;
    float ex = x - 229, ey = y - 212;
    float lx = ex * c + ey * s, ly = -ex * s + ey * c;
    if (lx * lx / 81 + ly * ly / 49 <= 1) return true;
    if (x >= 235 && x <= 239 && y >= 180 && y <= 212) {
        float qx = fminf(x - 235, 239 - x), qy = fminf(y - 180, 212 - y);
        if (qx >= 2 || qy >= 2 || hypotf(2 - qx, 2 - qy) <= 2) return true;
    }
    return x >= 237 && x <= 251 && y >= 180 && y <= 198 && in_poly(fx, fy, FLAG_N, x, y);
}

static bool note_at(const float *fx, const float *fy, float x, float y, float cs, float sn)
{
    float dx = x - NP_V15_CX, dy = y - NP_V15_CY;
    float ux = NP_V15_CX + dx * cs + dy * sn, uy = NP_V15_CY - dx * sn + dy * cs;
    if (ux < 218 || ux > 252 || uy < 177 || uy > 221) return false;
    return in_note(fx, fy, ux, uy);
}

static float note_cover(const float *fx, const float *fy, float x, float y, float cs, float sn)
{
    int corners = note_at(fx, fy, x, y, cs, sn) + note_at(fx, fy, x + 1, y, cs, sn) + note_at(fx, fy, x, y + 1, cs, sn) +
                  note_at(fx, fy, x + 1, y + 1, cs, sn);
    if (corners == 0 || corners == 4) return corners / 4.0f;
    int hit = 0;
    for (int j = 0; j < SS; j++)
        for (int i = 0; i < SS; i++) hit += note_at(fx, fy, x + (i + 0.5f) / SS, y + (j + 0.5f) / SS, cs, sn);
    return (float)hit / (SS * SS);
}

static float sheen(float dx, float dy, float d)
{
    float t = d / NP_V15_R;
    if (t < 0.4f) return 0;
    float g = t < 0.8f ? 0.07f * (t - 0.4f) / 0.4f : 0.07f - 0.05f * clamp01((t - 0.8f) / 0.2f);
    float phi = atan2f(dy, dx);
    if (phi < 0) phi += TWO_PI;
    float best = 0;
    static const float W[2][2] = {{0.5236f, 1.1345f}, {3.6652f, 4.2761f}};
    for (int i = 0; i < 2; i++) {
        float in = fminf(phi - W[i][0], W[i][1] - phi) * d;
        best = fmaxf(best, clamp01(in + 0.5f));
    }
    return g * best;
}

static col_t arm(col_t c, float x, float y)
{
    if (x < 318 || x > 412 || y < 128 || y > 264) return c;
    float dp = hypotf(x - 398, y - 250);
    c = over(c, hex(0x2A2A2A), clamp01(10.5f - dp));
    c = over(c, hex(0x4A4A4A), clamp01(1.5f - fabsf(dp - 10)));
    float da = fminf(seg_dist(x, y, 398, 250, 352, 170), seg_dist(x, y, 352, 170, 334, 148));
    c = over(c, hex(0x6A6A6A), clamp01(2.5f - da));
    const float k = 0.76604444f, s = -0.64278761f;
    float ex = x - 333, ey = y - 146;
    float lx = fabsf(ex * k + ey * s), ly = fabsf(-ex * s + ey * k);
    float qx = lx - 4, qy = ly - 7;
    float sd = hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0) - 3;
    return over(c, hex(0x8A8A8A), clamp01(0.5f - sd));
}

void np_v15_render(uint16_t *buf, int w, int h, np_rect_t rect, int frame, uint32_t accent)
{
    int x0 = rect.x < 0 ? 0 : rect.x, y0 = rect.y < 0 ? 0 : rect.y;
    int x1 = rect.x + rect.w > w ? w : rect.x + rect.w, y1 = rect.y + rect.h > h ? h : rect.y + rect.h;
    float fx[FLAG_N], fy[FLAG_N];
    flag_poly(fx, fy);
    float a = (float)(frame % NP_V15_FRAMES) * TWO_PI / NP_V15_FRAMES, cs = cosf(a), sn = sinf(a);
    const col_t black = {0, 0, 0}, fill = hex(0x0B0B0B), groove = hex(0x181818), rim = hex(0x222222);
    const col_t label = hex(0xD8D2C4), ring = hex(accent), ink = hex(0x4A4540), hole = hex(0x333333);
    for (int y = y0; y < y1; y++) {
        uint16_t *row = buf + (size_t)y * w;
        float py = y + 0.5f, fdy = py - FACE_C, dy = py - NP_V15_CY;
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f, fdx = px - FACE_C;
            if (fdx * fdx + fdy * fdy >= (float)NP_EDGE_IN * NP_EDGE_IN) continue;
            float dx = px - NP_V15_CX, d = sqrtf(dx * dx + dy * dy);
            col_t c = black;
            if (d < NP_V15_R + 2) {
                c = over(c, fill, clamp01(NP_V15_R + 0.5f - d));
                float gr = 60 + 5 * roundf((d - 60) / 5);
                if (gr < 60) gr = 60;
                if (gr > 130) gr = 130;
                c = over(c, groove, clamp01(1 - fabsf(d - gr)));
                float sh = sheen(dx, dy, d);
                if (sh > 0) c = over(c, (col_t){255, 255, 255}, sh);
                c = over(c, rim, clamp01(1.5f - fabsf(d - NP_V15_R)));
            }
            if (d < NP_V15_LABEL_R + 1) {
                c = over(c, label, clamp01(NP_V15_LABEL_R + 0.5f - d));
                c = over(c, ring, 0.8f * clamp01(1.5f - fabsf(d - 46)));
                if (d < NOTE_R) {
                    float n = note_cover(fx, fy, (float)x, (float)y, cs, sn);
                    if (n > 0) c = over(c, ink, n);
                }
                c = over(c, black, clamp01(4.5f - d));
                c = over(c, hole, clamp01(1.25f - fabsf(d - 4)));
            }
            c = arm(c, px, py);
            row[x] = pack(c);
        }
    }
}

np_rect_t np_v15_label_box(void)
{
    int x = (int)floorf(NP_V15_CX - NP_V15_LABEL_R - 1) & ~1, y = (int)floorf(NP_V15_CY - NP_V15_LABEL_R - 1) & ~1;
    int x2 = (int)ceilf(NP_V15_CX + NP_V15_LABEL_R + 1), y2 = (int)ceilf(NP_V15_CY + NP_V15_LABEL_R + 1);
    return (np_rect_t){x, y, (x2 - x + 1) & ~1, (y2 - y + 1) & ~1};
}

np_rect_t np_v15_text_box(void)
{
    float half = (float)NP_V15_TEXT_W / 2 / NP_V15_TITLE_R;
    float in = NP_V15_TITLE_R - 30, out = NP_V15_ARTIST_R + 8;
    int x = (int)floorf(FACE_C - out * sinf(half)) - TEXT_PAD - DRIFT;
    int y = (int)floorf(FACE_C + in * cosf(half)) - TEXT_PAD - DRIFT;
    int y2 = (int)ceilf(FACE_C + out) + DRIFT + 2;
    x &= ~1;
    y &= ~1;
    return (np_rect_t){x, y, ((int)(2 * FACE_C) - 2 * x + 1) & ~1, (y2 - y + 1) & ~1};
}
