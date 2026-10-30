#include "page_fx.h"

#include "esp_attr.h"
#include "lvgl.h"
#include "touch_swipe.h"

#define SCREEN_W 472
#define DOT_PX 8
#define DOT_PITCH 16
#define DOTS_X 442
static lv_obj_t *s_wipe, *s_dots;
static int s_wipe_dir, s_wipe_edge = -TS_WIPE_BAND_PX;
static EXT_RAM_BSS_ATTR double s_wipe_t0, s_dots_until;

static void wipe_rect(lv_layer_t *layer, const lv_draw_rect_dsc_t *d, int a, int b)
{
    int y1, y2;
    if (!ts_wipe_span(a, b, s_wipe_dir, &y1, &y2)) return;
    lv_area_t area = {0, y1, SCREEN_W - 1, y2};
    lv_draw_rect(layer, d, &area);
}

static void wipe_draw_cb(lv_event_t *e)
{
    int edge = s_wipe_edge;
    if (!s_wipe_dir || edge <= -TS_WIPE_BAND_PX) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_black();
    d.bg_opa = LV_OPA_COVER;
    wipe_rect(layer, &d, 0, edge);
    for (int dy = 0; dy < TS_WIPE_BAND_PX; dy += 2) {
        d.bg_opa = (lv_opa_t)ts_wipe_band_opa(dy);
        wipe_rect(layer, &d, edge + dy, edge + dy + 2);
    }
}

static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void page_fx_create(void)
{
    s_wipe = plain_obj(lv_layer_top());
    lv_obj_set_size(s_wipe, SCREEN_W, TS_H);
    lv_obj_add_event_cb(s_wipe, wipe_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

void wipe_start(int dir, double t)
{
    s_wipe_dir = dir;
    s_wipe_t0 = t;
    s_wipe_edge = TS_H;
    lv_obj_invalidate(s_wipe);
}

static void dots_show(const pages_nav_t *nav, double until)
{
    if (!s_dots) {
        int h = nav->n * DOT_PITCH - (DOT_PITCH - DOT_PX);
        s_dots = plain_obj(lv_layer_top());
        lv_obj_set_size(s_dots, DOT_PX, h);
        lv_obj_set_pos(s_dots, DOTS_X, (TS_CY - h / 2) & ~1);
        for (int i = 0; i < nav->n; i++) {
            lv_obj_t *d = plain_obj(s_dots);
            lv_obj_set_size(d, DOT_PX, DOT_PX);
            lv_obj_set_pos(d, 0, i * DOT_PITCH);
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        }
    }
    for (int i = 0; i < (int)lv_obj_get_child_count(s_dots); i++)
        lv_obj_set_style_bg_color(lv_obj_get_child(s_dots, i), lv_color_hex(i == nav->pos ? 0xFFFFFF : 0x4A4A4A), 0);
    s_dots_until = until;
}

void dots_refresh(const pages_nav_t *nav)
{
    if (!s_dots) return;
    lv_obj_delete(s_dots);
    s_dots = NULL;
    if (nav->n > 1) dots_show(nav, s_dots_until);
}

void page_fx_frame(double t)
{
    if (s_dots && t >= s_dots_until) {
        lv_obj_delete(s_dots);
        s_dots = NULL;
    }
    if (!s_wipe_dir) return;
    int e = ts_wipe_edge(t - s_wipe_t0);
    if (e == s_wipe_edge) return;
    int y1, y2;
    if (ts_wipe_rows(s_wipe_edge, e, s_wipe_dir, &y1, &y2)) {
        lv_area_t a = {0, y1, SCREEN_W - 1, y2};
        lv_obj_invalidate_area(s_wipe, &a);
    }
    s_wipe_edge = e;
    if (e <= -TS_WIPE_BAND_PX) s_wipe_dir = 0;
}

void page_go(pages_nav_t *nav, int steps, double t)
{
    if (!pages_step(nav, steps)) return;
    wipe_start(steps > 0 ? 1 : -1, t);
    dots_show(nav, t + TS_DOTS_S);
}

void page_fx_pause(void)
{
    if (s_wipe) s_wipe_dir = 0;
    if (s_dots) {
        lv_obj_delete(s_dots);
        s_dots = NULL;
    }
}
