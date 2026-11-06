#include <math.h>
#include <string.h>

#include "wx_moment.h"
#include "wx_moment_poly.h"

static const wxp_pt_t HEX_C = {75.0f, 44.0f};
#define HEX_R 37.0f
static const wxp_pt_t SPARKLE[4] = {{22, 18}, {130, 34}, {34, 78}, {124, 78}};
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

static void paint_part(wxp_img_t *im)
{
    const float w = HEX_R * 0.8660254f, h = HEX_R * 0.5f;
    const wxp_pt_t hexv[6] = {{HEX_C.x, HEX_C.y - HEX_R}, {HEX_C.x + w, HEX_C.y - h}, {HEX_C.x + w, HEX_C.y + h},
                          {HEX_C.x, HEX_C.y + HEX_R}, {HEX_C.x - w, HEX_C.y + h}, {HEX_C.x - w, HEX_C.y - h}};
    wxp_poly(im, hexv, 6, WXM_PRINT_FIL);
    const wxp_pt_t right[4] = {HEX_C, hexv[1], hexv[2], hexv[3]};
    wxp_poly(im, right, 4, scale_rgb(WXM_PRINT_FIL, 0.7f));
    const wxp_pt_t top[4] = {hexv[0], hexv[1], HEX_C, hexv[5]};
    wxp_poly(im, top, 4, lift_rgb(WXM_PRINT_FIL, 0.3f));
}

static void paint_sparkle(wxp_img_t *im)
{
    const float c = SPARKLE_HALF, hw = 1.5f;
    wxp_rect(im, c - SPARKLE_R, c - hw, c + SPARKLE_R, c + hw, 0xFFFFFF);
    wxp_rect(im, c - hw, c - SPARKLE_R, c + hw, c + SPARKLE_R, 0xFFFFFF);
}

static void paint_printer(wxp_img_t *im)
{
    const float x = PRINTER_X, y = PRINTER_Y;
    wxp_rrect(im, x + 2, y + 4, x + 60, y + 64, 5, 0xB4B4B8);
    wxp_rrect(im, x + 6, y + 8, x + 56, y + 60, 2.5f, 0x141414);
    wxp_rect(im, x + 8, y + 18, x + 54, y + 22, 0x8A8A90);
    wxp_rect(im, x + 26, y + 14, x + 36, y + 25, 0xE8E8E8);
    const wxp_pt_t nozzle[3] = {{x + 28, y + 25}, {x + 34, y + 25}, {x + 31, y + 30}};
    wxp_poly(im, nozzle, 3, WXM_PRINT_RGB);
    wxp_rect(im, x + 12, y + 50, x + 50, y + 55, WXM_PRINT_RGB);
    wxp_rect(im, x + 22, y + 42, x + 40, y + 50, WXM_PRINT_FIL);
}

static void paint_badge(wxp_img_t *im)
{
    const float cx = BADGE_X + 14.0f, cy = BADGE_Y + 14.0f;
    wxp_disc(im, cx, cy, 12.6f, 0x000000);
    wxp_disc(im, cx, cy, 10.8f, WXM_PRINT_RGB);
    wxp_rrect(im, cx - 1.7f, cy - 7.0f, cx + 1.7f, cy + 2.0f, 1.5f, 0xFFFFFF);
    wxp_disc(im, cx, cy + 6.0f, 1.9f, 0xFFFFFF);
}

void wxm_print_paint(const wxm_moment_t *m, const wxm_layout_t *l, uint8_t *arena, void (*yield)(void))
{
    wxp_img_t im[WXM_LAYERS];
    for (int i = 0; i < l->nl; i++) {
        const wxm_layer_t *a = &l->layer[i];
        im[i] = (wxp_img_t){(uint16_t *)(arena + a->at), arena + a->at + a->w * a->h * 2, a->w, a->h, a->x, a->y, yield};
    }
    if (m->kind == WXM_PRINT_DONE) {
        paint_part(&im[0]);
        paint_sparkle(&im[1]);
    } else {
        paint_printer(&im[0]);
        paint_badge(&im[1]);
    }
}
