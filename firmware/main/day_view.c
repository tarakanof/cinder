#include "day_view.h"

#include <math.h>
#include <string.h>

LV_FONT_DECLARE(font_digits_96);

#define SCREEN_W 472   /* driver frame 472; visible circle 466 (docs/llm.md) */
#define SCREEN_H 466
#define CX 236
#define CY 233
#define RING_R 222
#define RING_W 12
#define OFF_RING_W 6
#define TICK_OUT 230
#define TICK_W 28
#define DOT 16
#define GLOW 24
#define MARK_R 186
#define TEXT_W 360
#define LINE3_W 340
#define CHECK_Y 151
#define MOON 92
#define BITE 76

typedef struct {
    lv_obj_t *o;
    bool on;
    int a0, a1;
    uint32_t rgb;
} day_arc_t;

typedef struct {
    int hero, line2, line3;
} day_bases_t;

static const day_bases_t BASES = {262, 306, 352};
static const day_bases_t BASES_SUMMARY = {282, 326, 372};
static const day_bases_t BASES_OFF = {282, 330, 376};

static lv_obj_t *s_root, *s_track, *s_dot, *s_moon, *s_bite, *s_check, *s_hero, *s_line2, *s_line3;
static lv_obj_t *s_marks[4];
static day_arc_t s_seg[DAY_SEGS], s_tick;
static day_model_t s_m;
static bool s_drawn;

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *disc(lv_obj_t *parent, int d, int x, int y, uint32_t rgb)
{
    lv_obj_t *o = plain(parent);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, x - d / 2, y - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *arc(lv_obj_t *parent, int outer, int w, bool rounded)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, 2 * outer, 2 * outer);
    lv_obj_set_pos(a, CX - outer, CY - outer);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(a, 0);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, rounded, LV_PART_INDICATOR);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    return a;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *f, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_label_set_text_static(l, "");
    return l;
}

static int top_for(const lv_font_t *f, int base) { return base - (f->line_height - f->base_line); }

static void place(lv_obj_t *l, int w, int base)
{
    lv_obj_set_pos(l, CX - w / 2, top_for(lv_obj_get_style_text_font(l, 0), base));
}

static int deg(int min) { return ((min + 2) / 4 + 90) % 360; }

static void show(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void arc_set(day_arc_t *a, bool on, int a0, int a1, uint32_t rgb)
{
    on = on && a0 != a1;
    if (on != a->on) {
        show(a->o, on);
        a->on = on;
    }
    if (!on) return;
    if (rgb != a->rgb) {
        lv_obj_set_style_arc_color(a->o, lv_color_hex(rgb), LV_PART_INDICATOR);
        a->rgb = rgb;
    }
    if (a0 != a->a0 || a1 != a->a1) {
        lv_arc_set_angles(a->o, a0, a1);
        a->a0 = a0;
        a->a1 = a1;
    }
}

void day_view_create(lv_obj_t *parent)
{
    s_root = plain(parent);
    lv_obj_set_size(s_root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_track = arc(s_root, RING_R + RING_W / 2, RING_W, false);
    lv_obj_set_style_arc_opa(s_track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_track, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(s_track, 0, 360);
    lv_obj_remove_flag(s_track, LV_OBJ_FLAG_HIDDEN);

    static const char *const MARKS[4] = {"12", "18", "00", "06"};
    for (int i = 0; i < 4; i++) {
        s_marks[i] = text(s_root, &lv_font_montserrat_18, 40);
        lv_label_set_text_static(s_marks[i], MARKS[i]);
        lv_obj_set_style_text_color(s_marks[i], lv_color_hex(DAY_MARK), 0);
        int dx = i == 1 ? MARK_R : i == 3 ? -MARK_R : 0, dy = i == 0 ? -MARK_R : i == 2 ? MARK_R : 0;
        lv_obj_set_pos(s_marks[i], CX + dx - 20, CY + dy - lv_font_montserrat_18.line_height / 2);
        lv_obj_add_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < DAY_SEGS; i++) s_seg[i] = (day_arc_t){.o = arc(s_root, RING_R + RING_W / 2, RING_W, true), .a0 = -1};
    s_tick = (day_arc_t){.o = arc(s_root, TICK_OUT, TICK_W, false), .a0 = -1};

    s_dot = disc(s_root, DOT, CX, CY - RING_R, DAY_TEXT);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    s_moon = disc(s_root, MOON, CX + 10, CY - 67, DAY_MARK);
    s_bite = disc(s_root, BITE, CX + 41, CY - 87, 0x000000);
    lv_obj_add_flag(s_moon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_bite, LV_OBJ_FLAG_HIDDEN);

    s_check = text(s_root, &lv_font_montserrat_48, 120);
    lv_label_set_text_static(s_check, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(s_check, lv_color_hex(DAY_ACCENT), 0);
    lv_obj_set_pos(s_check, CX - 60, CHECK_Y - lv_font_montserrat_48.line_height / 2);
    lv_obj_add_flag(s_check, LV_OBJ_FLAG_HIDDEN);

    s_hero = text(s_root, &font_digits_96, TEXT_W);
    s_line2 = text(s_root, &lv_font_montserrat_24, TEXT_W);
    s_line3 = text(s_root, &lv_font_montserrat_24, LINE3_W);
    lv_label_set_long_mode(s_line3, LV_LABEL_LONG_MODE_WRAP);
    s_drawn = false;
}

void day_view_show(bool on)
{
    if (!s_root) return;
    show(s_root, on);
    if (on) s_drawn = false;
}

static void set_text(lv_obj_t *l, const char *now, const char *was, uint32_t rgb, uint32_t was_rgb)
{
    if (!s_drawn || strcmp(now, was) != 0) lv_label_set_text(l, now);
    if (!s_drawn || rgb != was_rgb) lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
}

static void layout(const day_model_t *m)
{
    const day_bases_t *b = m->face == DAY_FACE_SUMMARY ? &BASES_SUMMARY : m->face == DAY_FACE_OFF ? &BASES_OFF : &BASES;
    lv_obj_set_style_text_font(s_hero, m->hero_digits ? &font_digits_96 : &lv_font_montserrat_48, 0);
    place(s_hero, TEXT_W, b->hero);
    place(s_line2, TEXT_W, b->line2);
    place(s_line3, LINE3_W, b->line3);
}

static void dot_set(const day_model_t *m)
{
    show(s_dot, m->dot != DAY_NONE);
    if (m->dot == DAY_NONE) return;
    if (s_drawn && m->dot == s_m.dot && m->dot_rgb == s_m.dot_rgb && m->glow == s_m.glow && s_m.dot != DAY_NONE) return;
    float a = (float)((m->dot / 4.0 + 90.0) * M_PI / 180.0);
    int x = CX + (int)lroundf(RING_R * cosf(a)), y = CY + (int)lroundf(RING_R * sinf(a));
    lv_obj_set_pos(s_dot, x - DOT / 2, y - DOT / 2);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(m->dot_rgb), 0);
    lv_obj_set_style_shadow_width(s_dot, m->glow ? GLOW : 0, 0);
    lv_obj_set_style_shadow_color(s_dot, lv_color_hex(DAY_ACCENT), 0);
    lv_obj_set_style_shadow_opa(s_dot, LV_OPA_70, 0);
}

void day_view_update(const day_model_t *m)
{
    if (!s_root) return;
    if (!s_drawn || m->face != s_m.face || m->hero_digits != s_m.hero_digits) layout(m);
    if (!s_drawn || m->off_ring != s_m.off_ring) {
        int w = m->off_ring ? OFF_RING_W : RING_W;
        lv_obj_set_size(s_track, 2 * (RING_R + w / 2), 2 * (RING_R + w / 2));
        lv_obj_set_pos(s_track, CX - RING_R - w / 2, CY - RING_R - w / 2);
        lv_obj_set_style_arc_width(s_track, w, LV_PART_MAIN);
        lv_obj_set_style_arc_color(s_track, lv_color_hex(m->off_ring ? DAY_OFF_RING : DAY_TRACK), LV_PART_MAIN);
    }
    if (!s_drawn || m->marks != s_m.marks)
        for (int i = 0; i < 4; i++) show(s_marks[i], m->marks);
    for (int i = 0; i < DAY_SEGS; i++) {
        const day_seg_t *g = &m->seg[i];
        arc_set(&s_seg[i], i < m->n_segs, deg(g->from), deg(g->to), g->rgb);
    }
    int t0 = m->tick != DAY_NONE ? deg(m->tick) : 0;
    arc_set(&s_tick, m->tick != DAY_NONE, t0, t0 + 1, m->tick_rgb);
    dot_set(m);
    show(s_moon, m->moon);
    show(s_bite, m->moon);
    show(s_check, m->check);
    set_text(s_hero, m->hero, s_m.hero, m->hero_rgb, s_m.hero_rgb);
    set_text(s_line2, m->line2, s_m.line2, m->line2_rgb, s_m.line2_rgb);
    set_text(s_line3, m->line3, s_m.line3, m->line3_rgb, s_m.line3_rgb);
    s_m = *m;
    s_drawn = true;
}
