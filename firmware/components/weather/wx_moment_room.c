#include "wx_moment.h"
#include "wx_moment_poly.h"

#define WIN_X 42
#define WIN_Y 8
#define WIN_W 70
#define WIN_H 70
#define AIR_H 7
#define AIR_GAP_L 35
#define AIR_GAP_R 119

static const int AIR_LEN[3] = {22, 27, 18};
static const int AIR_Y[3] = {24, 40, 56};

static int add_layer(wxm_layout_t *l, int x, int y, int w, int h)
{
    l->layer[l->nl] = (wxm_layer_t){x, y, w, h, l->total};
    l->total += w * h * 3;
    return l->nl++;
}

static void add_el(wxm_layout_t *l, int layer, int x, int y) { l->el[l->ne++] = (wxm_place_t){layer, x, y}; }

static int air_x(int side, int i) { return side ? AIR_GAP_R - 1 : AIR_GAP_L - AIR_LEN[i] - 1; }

void wxm_room_layout(const wxm_moment_t *m, wxm_layout_t *l)
{
    (void)m;
    add_el(l, add_layer(l, WIN_X, WIN_Y, WIN_W, WIN_H), WIN_X, WIN_Y);
    int air[3];
    for (int i = 0; i < 3; i++) air[i] = add_layer(l, air_x(0, i), AIR_Y[i], AIR_LEN[i] + 2, AIR_H);
    for (int side = 0; side < 2; side++)
        for (int i = 0; i < 3; i++) add_el(l, air[i], air_x(side, i), AIR_Y[i]);
}

static void paint_window(wxp_img_t *im)
{
    const float x0 = 43.5f, y0 = 9, x1 = 110.5f, y1 = 77, cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, b = 4.5f;
    wxp_rrect(im, x0, y0, x1, y1, 7.5f, WXM_ROOM_FRAME);
    wxp_rrect(im, x0 + b, y0 + b, x1 - b, y1 - b, 3, WXM_ROOM_PANE);
    wxp_rect(im, cx - 1.75f, y0 + b, cx + 1.75f, y1 - b, WXM_ROOM_FRAME);
    wxp_rect(im, x0 + b, cy - 1.75f, x1 - b, cy + 1.75f, WXM_ROOM_FRAME);
}

static void paint_air(wxp_img_t *im, int len, uint32_t rgb)
{
    float x = (float)im->x0 + 1, y = (float)im->y0 + 1;
    wxp_rrect(im, x, y, x + (float)len, y + AIR_H - 2, (AIR_H - 2) / 2.0f, rgb);
}

void wxm_room_paint(const wxm_moment_t *m, const wxm_layout_t *l, uint8_t *arena, void (*yield)(void))
{
    wxp_img_t im[WXM_LAYERS];
    for (int i = 0; i < l->nl; i++) {
        const wxm_layer_t *a = &l->layer[i];
        im[i] = (wxp_img_t){(uint16_t *)(arena + a->at), arena + a->at + a->w * a->h * 2, a->w, a->h, a->x, a->y, yield};
    }
    paint_window(&im[0]);
    uint32_t air = m->kind == WXM_ROOM_FRESH ? WXM_ROOM_FRESH_AIR : WXM_ROOM_STALE_AIR;
    for (int i = 0; i < 3; i++) paint_air(&im[1 + i], AIR_LEN[i], air);
}
