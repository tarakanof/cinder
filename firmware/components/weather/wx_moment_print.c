#include <math.h>
#include <string.h>

#include "wx_moment.h"
#include "wx_paint.h"

#define SS 4
#define MAX_PTS 32

typedef struct {
    uint16_t *px;
    uint8_t *a;
    int w, h, x0, y0;
    void (*yield)(void);
} img_t;

typedef struct {
    float x, y;
} pt_t;

static const pt_t HEX_C = {75.0f, 44.0f};
#define HEX_R 37.0f
static const pt_t SPARKLE[4] = {{22, 18}, {130, 34}, {34, 78}, {124, 78}};
#define SPARKLE_HALF 12
#define SPARKLE_R 10.0f
#define PRINTER_X 86
#define PRINTER_Y 12
#define PRINTER_W 62
#define PRINTER_H 66
#define BADGE_X 126
#define BADGE_Y 2
#define BADGE_S 28

static int add_layer(wxm_layout_t *l, int x, int y, int w, int h)
{
    l->layer[l->nl] = (wxm_layer_t){x, y, w, h, l->total};
    l->total += w * h * 3;
    return l->nl++;
}

static void add_el(wxm_layout_t *l, int layer, int x, int y) { l->el[l->ne++] = (wxm_place_t){layer, x, y}; }

void wxm_print_layout(const wxm_moment_t *m, wxm_layout_t *l)
{
    int part = 0;
    if (m->kind == WXM_PRINT_DONE) {
        part = add_layer(l, 41, 5, 68, 78);
        int spark = add_layer(l, 0, 0, 2 * SPARKLE_HALF, 2 * SPARKLE_HALF);
        add_el(l, part, 41, 5);
        for (int i = 0; i < 4; i++) add_el(l, spark, (int)SPARKLE[i].x - SPARKLE_HALF, (int)SPARKLE[i].y - SPARKLE_HALF);
    } else {
        int printer = add_layer(l, PRINTER_X, PRINTER_Y, PRINTER_W, PRINTER_H);
        int badge = add_layer(l, BADGE_X, BADGE_Y, BADGE_S, BADGE_S);
        add_el(l, printer, PRINTER_X, PRINTER_Y);
        add_el(l, badge, BADGE_X, BADGE_Y);
    }
}

static inline uint16_t mix565(uint16_t d, uint16_t s, float a)
{
    int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
    int r = dr + (int)lroundf((float)(sr - dr) * a), g = dg + (int)lroundf((float)(sg - dg) * a),
        b = db + (int)lroundf((float)(sb - db) * a);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void put(img_t *im, int i, uint16_t c, float a)
{
    if (!(a > 0.002f)) return;
    if (a > 1) a = 1;
    float da = im->a[i] / 255.0f;
    float oa = a + da * (1 - a);
    im->px[i] = da <= 0 ? c : mix565(im->px[i], c, a / oa);
    im->a[i] = (uint8_t)lroundf(oa * 255.0f);
}

static bool inside(const pt_t *p, int n, float x, float y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
    return in;
}

static void poly(img_t *im, const pt_t *p, int n, uint32_t rgb)
{
    const uint16_t c = wx_rgb565(rgb);
    float minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
    for (int i = 1; i < n; i++) {
        minx = fminf(minx, p[i].x), maxx = fmaxf(maxx, p[i].x);
        miny = fminf(miny, p[i].y), maxy = fmaxf(maxy, p[i].y);
    }
    int x0 = (int)floorf(minx) - im->x0, x1 = (int)ceilf(maxx) - im->x0;
    int y0 = (int)floorf(miny) - im->y0, y1 = (int)ceilf(maxy) - im->y0;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > im->w) x1 = im->w;
    if (y1 > im->h) y1 = im->h;
    for (int y = y0; y < y1; y++) {
        if (im->yield) im->yield();
        for (int x = x0; x < x1; x++) {
            int hit = 0;
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++)
                    hit += inside(p, n, (float)(im->x0 + x) + (sx + 0.5f) / SS, (float)(im->y0 + y) + (sy + 0.5f) / SS);
            put(im, y * im->w + x, c, (float)hit / (SS * SS));
        }
    }
}

static void rect(img_t *im, float x0, float y0, float x1, float y1, uint32_t rgb)
{
    const pt_t p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    poly(im, p, 4, rgb);
}

static void rrect(img_t *im, float x0, float y0, float x1, float y1, float r, uint32_t rgb)
{
    pt_t p[MAX_PTS];
    int n = 0;
    const float cx[4] = {x1 - r, x1 - r, x0 + r, x0 + r}, cy[4] = {y0 + r, y1 - r, y1 - r, y0 + r};
    for (int q = 0; q < 4; q++)
        for (int i = 0; i <= 4; i++) {
            float a = ((float)q * 90.0f - 90.0f + (float)i * 22.5f) * 0.0174532925f;
            p[n++] = (pt_t){cx[q] + r * cosf(a), cy[q] + r * sinf(a)};
        }
    poly(im, p, n, rgb);
}

static void disc(img_t *im, float cx, float cy, float r, uint32_t rgb)
{
    pt_t p[24];
    for (int i = 0; i < 24; i++) {
        float a = (float)i * 6.2831853f / 24.0f;
        p[i] = (pt_t){cx + r * cosf(a), cy + r * sinf(a)};
    }
    poly(im, p, 24, rgb);
}

static uint32_t scale_rgb(uint32_t c, float k)
{
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) out |= (uint32_t)lroundf((float)((c >> s) & 0xFF) * k) << s;
    return out;
}

static uint32_t lift_rgb(uint32_t c, float k)
{
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        float v = (float)((c >> s) & 0xFF);
        out |= (uint32_t)lroundf(v + (255.0f - v) * k) << s;
    }
    return out;
}

static void paint_part(img_t *im)
{
    const float w = HEX_R * 0.8660254f, h = HEX_R * 0.5f;
    const pt_t hexv[6] = {{HEX_C.x, HEX_C.y - HEX_R}, {HEX_C.x + w, HEX_C.y - h}, {HEX_C.x + w, HEX_C.y + h},
                          {HEX_C.x, HEX_C.y + HEX_R}, {HEX_C.x - w, HEX_C.y + h}, {HEX_C.x - w, HEX_C.y - h}};
    poly(im, hexv, 6, WXM_PRINT_FIL);
    const pt_t right[4] = {HEX_C, hexv[1], hexv[2], hexv[3]};
    poly(im, right, 4, scale_rgb(WXM_PRINT_FIL, 0.7f));
    const pt_t top[4] = {hexv[0], hexv[1], HEX_C, hexv[5]};
    poly(im, top, 4, lift_rgb(WXM_PRINT_FIL, 0.3f));
}

static void paint_sparkle(img_t *im)
{
    const float c = SPARKLE_HALF, hw = 1.5f;
    rect(im, c - SPARKLE_R, c - hw, c + SPARKLE_R, c + hw, 0xFFFFFF);
    rect(im, c - hw, c - SPARKLE_R, c + hw, c + SPARKLE_R, 0xFFFFFF);
}

static void paint_printer(img_t *im)
{
    const float x = PRINTER_X, y = PRINTER_Y;
    rrect(im, x + 2, y + 4, x + 60, y + 64, 5, 0xB4B4B8);
    rrect(im, x + 6, y + 8, x + 56, y + 60, 2.5f, 0x141414);
    rect(im, x + 8, y + 18, x + 54, y + 22, 0x8A8A90);
    rect(im, x + 26, y + 14, x + 36, y + 25, 0xE8E8E8);
    const pt_t nozzle[3] = {{x + 28, y + 25}, {x + 34, y + 25}, {x + 31, y + 30}};
    poly(im, nozzle, 3, WXM_PRINT_RGB);
    rect(im, x + 12, y + 50, x + 50, y + 55, WXM_PRINT_RGB);
    rect(im, x + 22, y + 42, x + 40, y + 50, WXM_PRINT_FIL);
}

static void paint_badge(img_t *im)
{
    const float cx = BADGE_X + 14.0f, cy = BADGE_Y + 14.0f;
    disc(im, cx, cy, 12.6f, 0x000000);
    disc(im, cx, cy, 10.8f, WXM_PRINT_RGB);
    rrect(im, cx - 1.7f, cy - 7.0f, cx + 1.7f, cy + 2.0f, 1.5f, 0xFFFFFF);
    disc(im, cx, cy + 6.0f, 1.9f, 0xFFFFFF);
}

void wxm_print_paint(const wxm_moment_t *m, const wxm_layout_t *l, uint8_t *arena, void (*yield)(void))
{
    img_t im[WXM_LAYERS];
    for (int i = 0; i < l->nl; i++) {
        const wxm_layer_t *a = &l->layer[i];
        im[i] = (img_t){(uint16_t *)(arena + a->at), arena + a->at + a->w * a->h * 2, a->w, a->h, a->x, a->y, yield};
    }
    if (m->kind == WXM_PRINT_DONE) {
        paint_part(&im[0]);
        paint_sparkle(&im[1]);
    } else {
        paint_printer(&im[0]);
        paint_badge(&im[1]);
    }
}
