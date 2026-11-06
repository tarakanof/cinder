#include <math.h>
#include <string.h>

#include "print.h"

#define BODY_W 60.0f
#define BODY_H 56.0f
#define HEAD_Y (-24.0f)
#define PART_TOP (HEAD_Y + 9.0f)
#define PART_MAX 48.0f
#define LAYER 3.0f
#define HEAD_SWEEP 20.0f
#define HEAD_PARK 30.0f

#define COL_BODY 0x262626
#define COL_ROD 0x2E2E2E
#define COL_BED 0x5A5A5A
#define COL_BED_HEAT 0x555555
#define COL_GANTRY 0x8A8A8A
#define COL_HEAD 0xD8D8D8
#define COL_WIN_TOP 0x0C0C0C
#define COL_WIN_BOT 0x070707
#define COL_WIN_OFF 0x080808
#define COL_LIGHT 0xFFF3E0
#define COL_HOT 0xFFF1D8

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void unpack(uint16_t p, float *r, float *g, float *b)
{
    *r = (float)((p >> 11) & 0x1F) * (255.0f / 31);
    *g = (float)((p >> 5) & 0x3F) * (255.0f / 63);
    *b = (float)(p & 0x1F) * (255.0f / 31);
}

static uint16_t pack(float r, float g, float b)
{
    int R = (int)(r + 0.5f), G = (int)(g + 0.5f), B = (int)(b + 0.5f);
    R = R < 0 ? 0 : R > 255 ? 255 : R;
    G = G < 0 ? 0 : G > 255 ? 255 : G;
    B = B < 0 ? 0 : B > 255 ? 255 : B;
    return (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3));
}

static void blend(float *px, uint32_t c, float a)
{
    if (a <= 0) return;
    px[0] += ((float)((c >> 16) & 0xFF) - px[0]) * a;
    px[1] += ((float)((c >> 8) & 0xFF) - px[1]) * a;
    px[2] += ((float)(c & 0xFF) - px[2]) * a;
}

static float rrect(float u, float v, float x, float y, float w, float h, float r, float sc)
{
    float hx = w / 2, hy = h / 2;
    float qx = fabsf(u - (x + hx)) - (hx - r), qy = fabsf(v - (y + hy)) - (hy - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float d = sqrtf(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
    return clamp01(0.5f - d * sc);
}

static float rect(float u, float v, float x, float y, float w, float h, float sc)
{
    float a = clamp01((u - x) * sc + 0.5f) * clamp01((x + w - u) * sc + 0.5f);
    return a * clamp01((v - y) * sc + 0.5f) * clamp01((y + h - v) * sc + 0.5f);
}

static float edge(float u, float v, float ax, float ay, float bx, float by)
{
    float ex = bx - ax, ey = by - ay, l = sqrtf(ex * ex + ey * ey);
    return ((u - ax) * ey - (v - ay) * ex) / l;
}

static float tri(float u, float v, float hx, float sc)
{
    float d0 = edge(u, v, hx - 4, HEAD_Y + 2, hx + 4, HEAD_Y + 2);
    float d1 = edge(u, v, hx + 4, HEAD_Y + 2, hx, HEAD_Y + 8);
    float d2 = edge(u, v, hx, HEAD_Y + 8, hx - 4, HEAD_Y + 2);
    float d = d0 > d1 ? d0 : d1;
    d = d > d2 ? d : d2;
    return clamp01(0.5f - d * sc);
}

static float disc(float u, float v, float x, float y, float r, float sc)
{
    float d = sqrtf((u - x) * (u - x) + (v - y) * (v - y)) - r;
    return clamp01(0.5f - d * sc);
}

static uint32_t light_of(print_look_t l)
{
    switch (l) {
    case PRINT_LOOK_ATTENTION: return PRINT_COL_WAIT;
    case PRINT_LOOK_ERROR: return PRINT_COL_ERR;
    case PRINT_LOOK_DONE: return PRINT_COL_DONE;
    case PRINT_LOOK_NONE:
    case PRINT_LOOK_OFFLINE: return 0;
    case PRINT_LOOK_STOPPED: return 0x333333;
    default: return COL_LIGHT;
    }
}

static bool part_shown(print_look_t l)
{
    return l != PRINT_LOOK_HEATING && l != PRINT_LOOK_OFFLINE && l != PRINT_LOOK_NONE && l != PRINT_LOOK_IDLE;
}

typedef struct {
    const print_pose_t *p;
    uint32_t win_top, bed, nozzle, layer_a, layer_b, light;
    bool sign;
    float part, bed_y;
} paint_ctx_t;

static void paint_init(const print_pose_t *p, paint_ctx_t *c)
{
    print_look_t l = p->look;
    bool off = l == PRINT_LOOK_OFFLINE || l == PRINT_LOOK_NONE, heat = l == PRINT_LOOK_HEATING;
    c->p = p;
    c->win_top = off ? COL_WIN_OFF : print_mix(COL_WIN_TOP, light_of(l), 0.18f * p->pulse);
    c->bed = heat ? print_mix(COL_BED_HEAT, PRINT_COL_HEAT, p->heat) : COL_BED;
    c->nozzle = heat ? print_mix(COL_HEAD, PRINT_COL_HEAT, p->heat) : COL_HEAD;
    c->layer_a = PRINT_COL_FIL;
    c->layer_b = print_scale(PRINT_COL_FIL, 0.85f);
    c->light = light_of(l);
    c->sign = l == PRINT_LOOK_ATTENTION || l == PRINT_LOOK_ERROR;
    c->part = part_shown(l) ? PART_MAX * clamp01(p->progress) : 0;
    c->bed_y = PART_TOP + c->part;
}

static void paint(const paint_ctx_t *c, uint32_t win_col, float u, float v, float sc, float *px)
{
    const print_pose_t *p = c->p;
    float body = rrect(u, v, -BODY_W, -BODY_H, 2 * BODY_W, 2 * BODY_H, 14, sc);
    blend(px, COL_BODY, body);
    blend(px, PRINT_COL_ACCENT, body * clamp01((-BODY_H + 14 - v) * sc + 0.5f));
    blend(px, win_col, rrect(u, v, -48, -38, 96, 84, 8, sc));
    blend(px, COL_ROD, rect(u, v, -44, -38, 3, 84, sc));
    blend(px, COL_ROD, rect(u, v, 41, -38, 3, 84, sc));
    float h = c->part, bed = c->bed_y;
    if (h > 0) {
        float a = rect(u, v, -22, bed - h, 44, h, sc);
        if (a > 0) blend(px, (int)floorf((bed - v) / LAYER) % 2 ? c->layer_a : c->layer_b, a);
    }
    blend(px, c->bed, rect(u, v, -42, bed, 84, 5, sc));
    blend(px, COL_GANTRY, rect(u, v, -46, HEAD_Y - 6, 92, 3, sc));
    blend(px, COL_HEAD, rrect(u, v, p->head_x - 9, HEAD_Y - 10, 18, 12, 3, sc));
    blend(px, c->nozzle, tri(u, v, p->head_x, sc));
    if (c->sign) {
        float a = rrect(u, v, -3.2f, -9, 6.4f, 20, 3.2f, sc);
        float d = disc(u, v, 0, 17, 3.4f, sc);
        blend(px, c->light, (a > d ? a : d) * p->pulse);
    }
}

static print_rect_t box(const print_hero_t *h, float u0, float v0, float u1, float v1)
{
    int x0 = (int)floorf(h->cx + u0 * h->sc) - 1, y0 = (int)floorf(h->cy + v0 * h->sc) - 1;
    int x1 = (int)ceilf(h->cx + u1 * h->sc) + 1, y1 = (int)ceilf(h->cy + v1 * h->sc) + 1;
    x0 &= ~1;
    y0 &= ~1;
    x1 += x1 & 1;
    y1 += y1 & 1;
    return (print_rect_t){x0, y0, x1 - x0, y1 - y0};
}

print_rect_t print_hero_box(const print_hero_t *h) { return box(h, -BODY_W, -BODY_H, BODY_W, BODY_H); }

print_rect_t print_hero_head_at(const print_hero_t *h, float head_x)
{
    return box(h, head_x - 10, HEAD_Y - 11, head_x + 10, HEAD_Y + 9);
}

print_rect_t print_hero_heat_box(const print_hero_t *h) { return box(h, -43, HEAD_Y + 1, 43, PART_TOP + 6); }

print_rect_t print_rect_union(print_rect_t a, print_rect_t b)
{
    if (a.w <= 0 || a.h <= 0) return b;
    if (b.w <= 0 || b.h <= 0) return a;
    int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w, y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (print_rect_t){x0, y0, x1 - x0, y1 - y0};
}

float print_head_x(print_phase_t phase, double t)
{
    if (phase == PRINT_PRINTING) return (float)(sin(t * 2.6) * HEAD_SWEEP);
    return phase == PRINT_HEATING ? 0 : HEAD_PARK;
}

float print_pulse(print_look_t l, double t)
{
    if (l != PRINT_LOOK_ATTENTION && l != PRINT_LOOK_ERROR) return 1;
    return (float)(0.35 + 0.65 * (0.5 + 0.5 * sin(t * 5)));
}

void print_hero_render(const print_hero_t *h, const print_pose_t *p, uint16_t *buf, int w, int h_px, print_rect_t clip)
{
    int x0 = clip.x < 0 ? 0 : clip.x, y0 = clip.y < 0 ? 0 : clip.y;
    int x1 = clip.x + clip.w > w ? w : clip.x + clip.w, y1 = clip.y + clip.h > h_px ? h_px : clip.y + clip.h;
    paint_ctx_t c;
    paint_init(p, &c);
    for (int y = y0; y < y1; y++) {
        float v = (y + 0.5f - h->cy) / h->sc;
        uint32_t win = print_mix(c.win_top, COL_WIN_BOT, clamp01((v + 40) / 86));
        uint16_t *row = buf + (size_t)y * w;
        for (int x = x0; x < x1; x++) {
            float u = (x + 0.5f - h->cx) / h->sc;
            float px[3] = {0, 0, 0};
            if (fabsf(u) <= BODY_W + 1 && fabsf(v) <= BODY_H + 1) paint(&c, win, u, v, h->sc, px);
            row[x] = pack(px[0], px[1], px[2]);
        }
    }
}

static float luma(uint16_t p)
{
    float r, g, b;
    unpack(p, &r, &g, &b);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

#define MODEL_LUMA 6.0f

void print_hot_line(const uint16_t *ghost_row, uint16_t *dst_row, int w, float k)
{
    int lo = -1, hi = -1;
    for (int x = 0; x < w; x++)
        if (luma(ghost_row[x]) > MODEL_LUMA) {
            if (lo < 0) lo = x;
            hi = x;
        }
    if (lo < 0) return;
    int end = lo + (int)lroundf(clamp01(k) * (float)(hi - lo + 1));
    for (int x = lo; x < end; x++) {
        if (luma(ghost_row[x]) <= MODEL_LUMA) continue;
        float px[3];
        unpack(dst_row[x], &px[0], &px[1], &px[2]);
        blend(px, COL_HOT, 0.85f);
        dst_row[x] = pack(px[0], px[1], px[2]);
    }
}

void print_glint_row(const uint16_t *src, uint16_t *dst, int w, float a)
{
    float k = clamp01(a) * 0.55f;
    for (int x = 0; x < w; x++) {
        if (luma(src[x]) <= MODEL_LUMA) {
            dst[x] = src[x];
            continue;
        }
        float r, g, b;
        unpack(src[x], &r, &g, &b);
        dst[x] = pack(r + (255 - r) * k, g + (255 - g) * k, b + (255 - b) * k);
    }
}

void print_dot_xy(int k, int n, float cx, float cy, float *x, float *y)
{
    float a = 1.57079633f - ((float)k - (float)(n - 1) / 2) * (PRINT_DOT_PITCH / PRINT_DOT_R);
    *x = cx + cosf(a) * PRINT_DOT_R;
    *y = cy + sinf(a) * PRINT_DOT_R;
}

print_rect_t print_dot_render(uint16_t *buf, int w, int h, float cx, float cy, float r, uint32_t rgb)
{
    int x0 = ((int)floorf(cx - r) - 1) & ~1, y0 = ((int)floorf(cy - r) - 1) & ~1;
    int x1 = (int)ceilf(cx + r) + 1, y1 = (int)ceilf(cy + r) + 1;
    x1 += x1 & 1;
    y1 += y1 & 1;
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < h; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < w; x++) {
            float a = disc(x + 0.5f, y + 0.5f, cx, cy, r, 1);
            if (a <= 0) continue;
            float px[3];
            unpack(buf[(size_t)y * w + x], &px[0], &px[1], &px[2]);
            blend(px, rgb, a);
            buf[(size_t)y * w + x] = pack(px[0], px[1], px[2]);
        }
    return (print_rect_t){x0, y0, x1 - x0, y1 - y0};
}
