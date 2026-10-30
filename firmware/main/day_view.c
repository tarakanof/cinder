#include "day_view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "day_client.h"
#include "esp_attr.h"
#include "page_frame.h"
#include "page_ops.h"
#include "pomo_client.h"

LV_FONT_DECLARE(font_digits_60);
LV_FONT_DECLARE(font_digits_78);
LV_FONT_DECLARE(font_digits_96);
LV_FONT_DECLARE(font_face_36);

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
#define MARK_R 184
#define MARK_W 44
#define TEXT_W 400
#define CHECK_Y 151
#define MOON 80
#define BITE 66
#define HINT_Y 118

typedef struct {
    lv_obj_t *o;
    bool on;
    int a0, a1;
    uint32_t rgb;
} day_arc_t;

typedef struct {
    lv_obj_t *val, *cap;
    char vtxt[32], ctxt[40];
    const lv_font_t *vf, *cf;
    int vy, cy;
    uint32_t rgb;
    uint8_t opa;
    bool on;
} day_text_t;

static lv_obj_t *s_root;
static EXT_RAM_BSS_ATTR lv_obj_t *s_track, *s_dot, *s_moon, *s_bite, *s_check, *s_hint;
static EXT_RAM_BSS_ATTR char s_hint_txt[48];
static EXT_RAM_BSS_ATTR day_text_t s_txt[DAY_DRUM_SLOTS];
static EXT_RAM_BSS_ATTR day_roll_t s_roll;
static EXT_RAM_BSS_ATTR lv_obj_t *s_marks[4];
static EXT_RAM_BSS_ATTR day_arc_t s_seg[DAY_SEGS], s_tick;
static EXT_RAM_BSS_ATTR day_model_t s_m;
static EXT_RAM_BSS_ATTR bool s_drawn;
static EXT_RAM_BSS_ATTR day_ui_t s_day_ui;

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

static const lv_font_t *font_px(int px)
{
    switch (px) {
    case 24: return &lv_font_montserrat_24;
    case 30: return &lv_font_montserrat_30;
    case 36: return &font_face_36;
    case 48: return &lv_font_montserrat_48;
    case 60: return &font_digits_60;
    case 78: return &font_digits_78;
    default: return &font_digits_96;
    }
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
    day_ui_init(&s_day_ui);
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
        s_marks[i] = text(s_root, &lv_font_montserrat_24, MARK_W);
        lv_label_set_text_static(s_marks[i], MARKS[i]);
        lv_obj_set_style_text_color(s_marks[i], lv_color_hex(DAY_MARK), 0);
        int dx = i == 1 ? MARK_R : i == 3 ? -MARK_R : 0, dy = i == 0 ? -MARK_R : i == 2 ? MARK_R : 0;
        lv_obj_set_pos(s_marks[i], CX + dx - MARK_W / 2, CY + dy - lv_font_montserrat_24.line_height / 2);
        lv_obj_add_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < DAY_SEGS; i++) s_seg[i] = (day_arc_t){.o = arc(s_root, RING_R + RING_W / 2, RING_W, true), .a0 = -1};
    s_tick = (day_arc_t){.o = arc(s_root, TICK_OUT, TICK_W, false), .a0 = -1};

    s_dot = disc(s_root, DOT, CX, CY - RING_R, DAY_TEXT);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    s_moon = disc(s_root, MOON, CX + 10, CY - 63, 0x3A3A3A);
    s_bite = disc(s_root, BITE, CX + 37, CY - 80, 0x000000);
    lv_obj_add_flag(s_moon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_bite, LV_OBJ_FLAG_HIDDEN);

    s_check = text(s_root, &lv_font_montserrat_48, 120);
    lv_label_set_text_static(s_check, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(s_check, lv_color_hex(DAY_ACCENT), 0);
    lv_obj_set_pos(s_check, CX - 60, CHECK_Y - lv_font_montserrat_48.line_height / 2);
    lv_obj_add_flag(s_check, LV_OBJ_FLAG_HIDDEN);

    s_hint = text(s_root, &lv_font_montserrat_24, TEXT_W);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(DAY_NOTE), 0);
    lv_obj_set_pos(s_hint, CX - TEXT_W / 2, top_for(&lv_font_montserrat_24, HINT_Y));
    for (int i = 0; i < DAY_DRUM_SLOTS; i++) {
        day_text_t *x = &s_txt[i];
        *x = (day_text_t){.val = text(s_root, &font_digits_96, TEXT_W), .cap = text(s_root, &lv_font_montserrat_30, TEXT_W)};
        lv_obj_add_flag(x->val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(x->cap, LV_OBJ_FLAG_HIDDEN);
    }
    s_drawn = false;
}

void day_view_show(bool on)
{
    if (!s_root) return;
    show(s_root, on);
    if (on) s_drawn = false;
}

static void line_set(lv_obj_t *l, char *shown, size_t cap, const char *txt, const lv_font_t **font,
                     const lv_font_t *f, int *y, int base, bool restyle)
{
    if (restyle || strcmp(shown, txt) != 0) {
        snprintf(shown, cap, "%s", txt);
        lv_label_set_text(l, shown);
    }
    if (restyle || *font != f) {
        *font = f;
        lv_obj_set_style_text_font(l, f, 0);
    }
    int top = top_for(f, base);
    if (restyle || *y != top) {
        *y = top;
        lv_obj_set_pos(l, CX - TEXT_W / 2, top);
    }
}

static void text_hide(day_text_t *x)
{
    if (!x->on) return;
    show(x->val, false);
    show(x->cap, false);
    x->on = false;
}

static void text_set(day_text_t *x, const day_slot_t *sl)
{
    day_pose_t q;
    day_panel_pose(&sl->p, sl->u, sl->fade, &q);
    if (q.opa == 0) {
        text_hide(x);
        return;
    }
    bool restyle = !x->on;
    line_set(x->val, x->vtxt, sizeof x->vtxt, sl->p.val, &x->vf, font_px(q.val_px), &x->vy, q.val_y, restyle);
    line_set(x->cap, x->ctxt, sizeof x->ctxt, sl->p.cap, &x->cf, font_px(q.cap_px), &x->cy, q.cap_y, restyle);
    if (restyle || q.rgb != x->rgb || q.opa != x->opa) {
        x->rgb = q.rgb;
        x->opa = q.opa;
        for (int k = 0; k < 2; k++) {
            lv_obj_t *l = k ? x->cap : x->val;
            lv_obj_set_style_text_color(l, lv_color_hex(q.rgb), 0);
            lv_obj_set_style_text_opa(l, q.opa, 0);
        }
    }
    show(x->val, true);
    show(x->cap, sl->p.cap[0] != 0);
    x->on = true;
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

void day_view_update(const day_model_t *m, double t)
{
    if (!s_root) return;
    day_roll_start(&s_roll, &s_m, m, !s_drawn, t);
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
    if (!s_drawn || strcmp(m->hint, s_hint_txt) != 0) {
        snprintf(s_hint_txt, sizeof s_hint_txt, "%s", m->hint);
        lv_label_set_text(s_hint, s_hint_txt);
    }
    static EXT_RAM_BSS_ATTR day_slot_t sl[DAY_DRUM_SLOTS];
    int n = day_roll_slots(&s_roll, m, t, sl);
    for (int i = 0; i < DAY_DRUM_SLOTS; i++) {
        if (i < n) text_set(&s_txt[i], &sl[i]);
        else text_hide(&s_txt[i]);
    }
    s_m = *m;
    s_drawn = true;
}

void page_day_show(bool on)
{
    day_view_show(on);
    day_act_t a[DAY_ACTS];
    if (!on)
        for (int i = 0, n = day_ui_leave(&s_day_ui, pomo_client_now(), a); i < n; i++) day_client_act(&a[i]);
}

static day_in_t day_now(day_snapshot_t *snap)
{
    day_client_get(snap);
    double now = pomo_client_now();
    bool exact;
    int now_min = day_client_now_min(snap, now, &exact);
    return (day_in_t){.b = snap->has ? &snap->b : NULL, .fed = snap->fed, .age_s = now - snap->fed_at,
                      .now_min = now_min, .now_exact = exact, .busy = day_client_busy(snap, now)};
}

static void day_send(const day_act_t *a, int n)
{
    for (int i = 0; i < n; i++) day_client_act(&a[i]);
}

void page_day_input(const page_input_t *in, double t)
{
    day_snapshot_t snap;
    day_in_t di = day_now(&snap);
    day_act_t a[DAY_ACTS];
    day_ui_sync(&s_day_ui, &di, t);
    if (in->detents) day_send(a, day_ui_turn(&s_day_ui, &di, in->detents, t, a));
    if (in->pushes) day_send(a, day_ui_push(&s_day_ui, &di, t, a));
    else if (in->longs) day_send(a, day_ui_long(&s_day_ui, &di, t, a));
}

void page_day_frame(const page_frame_t *f)
{
    static EXT_RAM_BSS_ATTR day_snapshot_t snap;
    static EXT_RAM_BSS_ATTR day_model_t m;
    day_in_t di = day_now(&snap);
    day_act_t a[DAY_ACTS];
    day_ui_sync(&s_day_ui, &di, f->t);
    day_send(a, day_ui_tick(&s_day_ui, &di, f->t, a));
    day_model(&s_day_ui, &di, f->t, day_client_note(&snap, pomo_client_now()), &m);
    day_view_update(&m, f->t);
}

const char *day_view_hold_label(void)
{
    static EXT_RAM_BSS_ATTR day_snapshot_t snap;
    day_in_t di = day_now(&snap);
    return day_hold_label(day_ui_face(&s_day_ui, &di, pomo_client_now()));
}
