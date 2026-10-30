#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What page_fx.c and overlays.c call: objects with a parent, children, a hidden flag, an arc value, a text and a colour. test_page_ops.c implements it. */
typedef struct lv_obj_t lv_obj_t;
typedef struct { int rects, last_y1; } lv_layer_t;
typedef struct { lv_layer_t *layer; } lv_event_t;
typedef void (*lv_event_cb_t)(lv_event_t *e);
typedef struct { int32_t x1, y1, x2, y2; } lv_area_t;
typedef uint32_t lv_color_t;
typedef uint8_t lv_opa_t;
typedef struct {
    lv_color_t bg_color;
    lv_opa_t bg_opa;
} lv_draw_rect_dsc_t;

struct lv_obj_t {
    lv_obj_t *parent, *kids[8];
    int n, arc, invalid;
    bool used, hidden;
    const char *text;
    lv_color_t bg;
    lv_event_cb_t draw;
};

enum { LV_OBJ_FLAG_HIDDEN = 1, LV_OBJ_FLAG_SCROLLABLE = 2, LV_OBJ_FLAG_CLICKABLE = 4 };
enum { LV_EVENT_DRAW_MAIN = 1 };
#define LV_OPA_COVER 255
#define LV_RADIUS_CIRCLE 0x7FFF

lv_obj_t *lv_layer_top(void);
lv_obj_t *lv_obj_create(lv_obj_t *parent);
void lv_obj_delete(lv_obj_t *o);

#define lv_arc_create lv_obj_create
#define lv_label_create lv_obj_create
#define lv_pct(x) (x)
#define lv_color_hex(c) ((lv_color_t)(c))
#define lv_color_black() ((lv_color_t)0)
#define lv_color_white() ((lv_color_t)0xFFFFFF)
#define lv_obj_add_flag(o, f) ((o)->hidden = (o)->hidden || ((f) & LV_OBJ_FLAG_HIDDEN))
#define lv_obj_remove_flag(o, f) ((o)->hidden = (o)->hidden && !((f) & LV_OBJ_FLAG_HIDDEN))
#define lv_obj_add_event_cb(o, cb, ev, data) ((void)(ev), (void)(data), (o)->draw = (cb))
#define lv_obj_invalidate(o) ((o)->invalid++)
#define lv_obj_invalidate_area(o, a) ((void)(a), (o)->invalid++)
#define lv_obj_get_child_count(o) ((uint32_t)(o)->n)
#define lv_obj_get_child(o, i) ((o)->kids[i])
#define lv_obj_set_style_bg_color(o, c, sel) ((void)(sel), (o)->bg = (c))
#define lv_event_get_layer(e) ((e)->layer)
#define lv_draw_rect_dsc_init(d) ((d)->bg_color = 0, (d)->bg_opa = 0)
#define lv_draw_rect(l, d, a) ((void)(d), (l)->last_y1 = (a)->y1, (l)->rects++)
#define lv_arc_set_value(o, v) ((o)->arc = (v))
#define lv_label_set_text(o, s) ((o)->text = (s))
#define lv_label_set_text_static(o, s) ((o)->text = (s))

static inline void lv_obj_set_size(lv_obj_t *o, int w, int h) { (void)o, (void)w, (void)h; }
static inline void lv_obj_set_pos(lv_obj_t *o, int x, int y) { (void)o, (void)x, (void)y; }

#define lv_obj_remove_style_all(...) ((void)0)
#define lv_obj_remove_style(...) ((void)0)
#define lv_obj_center(...) ((void)0)
#define lv_obj_align(...) ((void)0)
#define lv_arc_set_bg_angles(...) ((void)0)
#define lv_arc_set_rotation(...) ((void)0)
#define lv_arc_set_range(...) ((void)0)
#define lv_obj_set_style_radius(...) ((void)0)
#define lv_obj_set_style_bg_opa(...) ((void)0)
#define lv_obj_set_style_arc_opa(...) ((void)0)
#define lv_obj_set_style_arc_width(...) ((void)0)
#define lv_obj_set_style_arc_color(...) ((void)0)
#define lv_obj_set_style_text_font(...) ((void)0)
#define lv_obj_set_style_text_color(...) ((void)0)
#define lv_obj_set_style_text_align(...) ((void)0)
#define lv_obj_set_style_pad_hor(...) ((void)0)
#define lv_obj_set_style_pad_ver(...) ((void)0)
