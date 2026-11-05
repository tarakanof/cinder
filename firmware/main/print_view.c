#include "print_view.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "np.h"
#include "np_cover.h"
#include "page_canvas.h"
#include "page_frame.h"
#include "page_ops.h"
#include "pomo_client.h"
#include "print_client.h"

LV_FONT_DECLARE(font_digits_96);

#define SCREEN_W 472   /* driver frame 472; visible circle 466 (docs/llm.md) */
#define SCREEN_H 466
#define FX ((SCREEN_W - PRINT_FRAME_PX) / 2)

_Static_assert(SCREEN_W * SCREEN_H * 2 <= PAGE_CANVAS_BYTES, "the face draws on the page canvas");

#define AMBIENT_S (1.0 / 15)
#define DONE_DIM_S 600.0
#define FACE_DIM_OPA 109
#define HOT_ROWS 3
#define GLINT_PERIOD_S 3.0
#define GLINT_SWEEP_S 1.2
#define GLINT_HALF 20
#define TEXT_W 340
#define UNIT_GAP 8

static const np_ring_t EDGE = {.cx = SCREEN_W / 2.0f, .cy = SCREEN_H / 2.0f, .r = NP_EDGE_R, .hw = NP_EDGE_HW};
static const print_hero_t HERO_K1 = {.cx = 233 + FX, .cy = 122, .sc = 0.95f};
static const print_hero_t HERO_K2 = {.cx = 233 + FX, .cy = 205, .sc = 1.6f};

enum { HERO_NONE, HERO_PRINTER, HERO_PICS };

typedef struct {
    uint8_t glow[NP_GLOW_PX * NP_GLOW_PX];
    print_snapshot_t snap;
    print_block_t b;
    print_pics_t pics;
    print_faces_t faces;
    print_text_t want, shown;
    np_edge_style_t style;
    bool drawn, ring_on, labels_on, labels_k1;
    int face, look, hero;
    float frac;
    print_pose_t pose;
    print_rect_t hero_box;
    uint16_t *lo, *hi;
    bool cache_ok;
    int clip, hot_y;
    int glint_y0, glint_y1;
    double amb_t;
    double done_t0;
    bool done_seen;
    int dim, word_opa;
    char val[16], unit[16], word[24], sub[64], small[2 * PRINT_NAME_MAX + 8];
} pv_mem_t;

static pv_mem_t *M;
static lv_obj_t *s_root, *s_canvas, *s_val, *s_unit, *s_word, *s_sub, *s_small, *s_shade;
static uint16_t *s_buf;
static EXT_RAM_BSS_ATTR uint32_t s_none[2];

static int top_for(const lv_font_t *f, int base) { return base - (f->line_height - f->base_line); }

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    lv_label_set_text_static(l, "");
    return l;
}

void print_view_create(lv_obj_t *parent)
{
    M = heap_caps_calloc(1, sizeof *M, MALLOC_CAP_SPIRAM);
    assert(M);
    np_glow_a8(M->glow);
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_canvas = lv_canvas_create(s_root);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(s_canvas, s_none, 2, 2, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, 0);

    s_val = label(s_root, &font_digits_96, 0);
    s_unit = label(s_root, &lv_font_montserrat_30, 0);
    s_word = label(s_root, &lv_font_montserrat_48, 400);
    s_sub = label(s_root, &lv_font_montserrat_30, TEXT_W);
    s_small = label(s_root, &lv_font_montserrat_24, TEXT_W - 20);

    s_shade = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_shade);
    lv_obj_set_size(s_shade, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_shade, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_shade, FACE_DIM_OPA, 0);
    lv_obj_remove_flag(s_shade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_shade, LV_OBJ_FLAG_HIDDEN);
}

static void cache_free(void)
{
    free(M->lo);
    free(M->hi);
    M->lo = M->hi = NULL;
    M->cache_ok = false;
}

static void attach(void)
{
    if (s_buf || lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) return;
    s_buf = page_canvas_take(PAGE_CANVAS_PRINT);
    if (!s_buf) return;
    memset(s_buf, 0, SCREEN_W * SCREEN_H * 2);
    lv_canvas_set_buffer(s_canvas, s_buf, SCREEN_W, SCREEN_H, LV_COLOR_FORMAT_RGB565);
    M->drawn = false;
}

static void detach(void)
{
    if (!s_buf) return;
    lv_canvas_set_buffer(s_canvas, s_none, 2, 2, LV_COLOR_FORMAT_RGB565);
    page_canvas_give(PAGE_CANVAS_PRINT);
    s_buf = NULL;
}

void page_print_show(bool on)
{
    if (!s_root) return;
    bool was = !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    if (on) lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    print_client_set_visible(on);
    if (on && !was) {
        print_faces_open(&M->faces);
        M->drawn = false;
    }
    if (on) {
        attach();
        return;
    }
    detach();
    cache_free();
}

void page_print_input(const page_input_t *in, double t)
{
    (void)t;
    if (!M || !in->detents) return;
    print_faces_turn(&M->faces, M->snap.has ? &M->b : NULL, in->detents);
}

static void invalidate(print_rect_t r)
{
    if (r.w <= 0 || r.h <= 0) return;
    lv_area_t a = {.x1 = r.x, .y1 = r.y, .x2 = r.x + r.w - 1, .y2 = r.y + r.h - 1};
    lv_obj_invalidate_area(s_canvas, &a);
}

static void ring_draw(float frac, const np_edge_style_t *st, bool full)
{
    if (full || !M->ring_on || !np_edge_style_eq(st, &M->style)) {
        M->style = *st;
        np_edge_render(&EDGE, s_buf, SCREEN_W, SCREEN_H, (np_rect_t){0, 0, SCREEN_W, SCREEN_H}, frac, st, M->glow);
        if (!full) lv_obj_invalidate(s_canvas);
    } else {
        np_rect_t d;
        if (!np_edge_dirty(&EDGE, M->frac, frac, SCREEN_W, SCREEN_H, &d)) return;
        np_edge_render(&EDGE, s_buf, SCREEN_W, SCREEN_H, d, frac, st, M->glow);
        invalidate((print_rect_t){d.x, d.y, d.w, d.h});
    }
    M->ring_on = true;
    M->frac = frac;
}

static const print_hero_t *hero_geom(void) { return M->face == PRINT_FACE_K1 ? &HERO_K1 : &HERO_K2; }

static void box_blend(float k)
{
    print_rect_t r = M->hero_box;
    int ik = (int)lroundf(k * 32);
    for (int y = 0; y < r.h; y++) {
        const uint16_t *a = M->lo + (size_t)y * r.w, *b = M->hi + (size_t)y * r.w;
        uint16_t *d = s_buf + (size_t)(r.y + y) * SCREEN_W + r.x;
        for (int x = 0; x < r.w; x++) {
            uint32_t pa = a[x], pb = b[x];
            uint32_t R = (((pa >> 11) * (32 - ik) + (pb >> 11) * ik) >> 5) & 0x1F;
            uint32_t G = ((((pa >> 5) & 0x3F) * (32 - ik) + ((pb >> 5) & 0x3F) * ik) >> 5) & 0x3F;
            uint32_t B = (((pa & 0x1F) * (32 - ik) + (pb & 0x1F) * ik) >> 5) & 0x1F;
            d[x] = (uint16_t)(R << 11 | G << 5 | B);
        }
    }
}

static bool pulse_cache(void)
{
    if (M->cache_ok) return true;
    print_rect_t r = M->hero_box;
    size_t n = (size_t)r.w * r.h;
    if (!M->lo) M->lo = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
    if (!M->hi) M->hi = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
    if (!M->lo || !M->hi) {
        cache_free();
        return false;
    }
    const print_hero_t *h = hero_geom();
    print_hero_t local = {h->cx - r.x, h->cy - r.y, h->sc};
    print_pose_t p = M->pose;
    print_rect_t all = {0, 0, r.w, r.h};
    p.pulse = 0.35f;
    print_hero_render(&local, &p, M->lo, r.w, r.h, all);
    p.pulse = 1;
    print_hero_render(&local, &p, M->hi, r.w, r.h, all);
    M->cache_ok = true;
    return true;
}

static void hero_printer(bool full, double t)
{
    const print_hero_t *h = hero_geom();
    print_pose_t want = M->pose;
    want.look = (print_look_t)M->look;
    want.progress = print_ring_frac(M->snap.has ? &M->b : NULL);
    if (want.look == PRINT_LOOK_HEATING) {
        want.heat = want.progress;
        want.progress = 0;
    } else {
        want.heat = 0;
    }
    want.head_x = print_head_x(want.look, t);
    want.pulse = print_pulse(want.look, t);
    bool pulsing = want.look == PRINT_LOOK_ATTENTION || want.look == PRINT_LOOK_ERROR;
    bool changed = full || want.look != M->pose.look || want.progress != M->pose.progress || want.heat != M->pose.heat;
    bool tick = t - M->amb_t >= AMBIENT_S;
    M->hero_box = print_hero_box(h);
    if (changed) {
        M->pose = want;
        M->cache_ok = false;
        print_hero_render(h, &want, s_buf, SCREEN_W, SCREEN_H, M->hero_box);
        if (!full) invalidate(M->hero_box);
        M->amb_t = t;
        return;
    }
    if (!tick) return;
    M->amb_t = t;
    if (want.look == PRINT_LOOK_PROGRESS && want.head_x != M->pose.head_x) {
        M->pose.head_x = want.head_x;
        print_rect_t hb = print_hero_head_box(h);
        print_hero_render(h, &M->pose, s_buf, SCREEN_W, SCREEN_H, hb);
        invalidate(hb);
    } else if (pulsing && want.pulse != M->pose.pulse) {
        M->pose.pulse = want.pulse;
        if (pulse_cache()) box_blend((want.pulse - 0.35f) / 0.65f);
        else print_hero_render(h, &M->pose, s_buf, SCREEN_W, SCREEN_H, M->hero_box);
        invalidate(M->hero_box);
    }
}

static void pic_rows(int y0, int y1)
{
    const print_pics_t *p = &M->pics;
    if (y0 < p->top) y0 = p->top;
    if (y1 > p->bottom) y1 = p->bottom;
    for (int y = y0; y < y1; y++) {
        const uint16_t *src = (y < M->clip ? p->ghost : p->solid) + (size_t)(y - p->top) * PRINT_PIC_W;
        memcpy(s_buf + (size_t)y * SCREEN_W + PRINT_PIC_X0 + FX, src, PRINT_PIC_W * 2);
    }
}

static print_rect_t rows_box(int y0, int y1)
{
    if (y0 < M->pics.top) y0 = M->pics.top;
    if (y1 > M->pics.bottom) y1 = M->pics.bottom;
    y0 &= ~1;
    y1 += y1 & 1;
    return (print_rect_t){(PRINT_PIC_X0 + FX) & ~1, y0, PRINT_PIC_W + 2, y1 > y0 ? y1 - y0 : 0};
}

static int pic_clip(void)
{
    const print_block_t *b = &M->b;
    switch ((print_look_t)M->look) {
    case PRINT_LOOK_DONE:
    case PRINT_LOOK_FINISHING: return M->pics.top;
    case PRINT_LOOK_HEATING: return M->pics.bottom;
    default: return print_clip_row(M->pics.top, M->pics.bottom, b->has_progress ? b->progress : 0);
    }
}

static void hot_line(double t, bool on, bool inval)
{
    const print_pics_t *p = &M->pics;
    int y0 = M->clip - HOT_ROWS;
    if (M->hot_y >= 0 && M->hot_y != y0) {
        pic_rows(M->hot_y, M->hot_y + HOT_ROWS);
        if (inval) invalidate(rows_box(M->hot_y, M->hot_y + HOT_ROWS));
        M->hot_y = -1;
    }
    if (!on || y0 < p->top) return;
    pic_rows(y0, M->clip);
    float k = (float)((sin(t * 3) + 1) / 2);
    for (int y = y0; y < M->clip; y++)
        print_hot_line(p->ghost + (size_t)(y - p->top) * PRINT_PIC_W, s_buf + (size_t)y * SCREEN_W + PRINT_PIC_X0 + FX,
                       PRINT_PIC_W, k);
    M->hot_y = y0;
    if (inval) invalidate(rows_box(y0, M->clip));
}

static void glint(double t, bool on)
{
    const print_pics_t *p = &M->pics;
    int y0 = -1, y1 = -1;
    double k = fmod(t, GLINT_PERIOD_S) / GLINT_SWEEP_S;
    if (on && k < 1) {
        int c = p->bottom - (int)lround((p->bottom - p->top) * k);
        y0 = c - GLINT_HALF;
        y1 = c + GLINT_HALF;
    }
    if (M->glint_y0 >= 0) {
        pic_rows(M->glint_y0, M->glint_y1);
        invalidate(rows_box(M->glint_y0, M->glint_y1));
    }
    M->glint_y0 = y0;
    M->glint_y1 = y1;
    if (y0 < 0) return;
    for (int y = y0 < p->top ? p->top : y0; y < y1 && y < p->bottom; y++) {
        float a = 1 - fabsf((float)(y - (y0 + GLINT_HALF))) / GLINT_HALF;
        print_glint_row(p->solid + (size_t)(y - p->top) * PRINT_PIC_W, s_buf + (size_t)y * SCREEN_W + PRINT_PIC_X0 + FX,
                        PRINT_PIC_W, a);
    }
    invalidate(rows_box(y0, y1));
}

static void hero_pics(bool full, double t)
{
    int clip = pic_clip();
    bool hot = M->look == PRINT_LOOK_PROGRESS;
    if (full) {
        M->clip = clip;
        M->hot_y = M->glint_y0 = M->glint_y1 = -1;
        pic_rows(M->pics.top, M->pics.bottom);
    } else if (clip != M->clip) {
        int a = clip < M->clip ? clip : M->clip, b = clip < M->clip ? M->clip : clip;
        if (M->hot_y >= 0 && M->hot_y < a) a = M->hot_y;
        M->clip = clip;
        pic_rows(a, b);
        M->hot_y = -1;
        hot_line(t, hot, false);
        invalidate(rows_box(a - HOT_ROWS, b));
        M->amb_t = t;
        return;
    }
    if (t - M->amb_t < AMBIENT_S && !full) return;
    M->amb_t = t;
    hot_line(t, hot, !full);
    glint(t, M->look == PRINT_LOOK_DONE);
}

static void dots(void)
{
    for (int k = 0; k < PRINT_FACES; k++) {
        float x, y;
        print_dot_xy(k, PRINT_FACES, 233 + FX, 233, &x, &y);
        print_dot_render(s_buf, SCREEN_W, SCREEN_H, x, y, PRINT_DOT_SIZE, k == M->face ? PRINT_COL_DOT_ON : PRINT_COL_DOT_OFF);
    }
}

static void text_set(lv_obj_t *l, char *shown, size_t cap, const char *txt)
{
    if (strcmp(shown, txt) == 0) return;
    strlcpy(shown, txt, cap);
    lv_label_set_text_static(l, shown);
}

static void show(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void color(lv_obj_t *l, uint32_t *shown, uint32_t rgb, bool force)
{
    if (!force && *shown == rgb) return;
    *shown = rgb;
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
}

static void pos(lv_obj_t *l, int x, int y)
{
    if (lv_obj_get_x(l) != x || lv_obj_get_y(l) != y) lv_obj_set_pos(l, x, y);
}

static void place(lv_obj_t *l, int base, int w)
{
    const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
    pos(l, (SCREEN_W - w) / 2, top_for(f, base));
}

static void labels(bool k1)
{
    const print_text_t *t = &M->want;
    print_text_t *o = &M->shown;
    bool first = !M->labels_on;
    if (!first && M->labels_k1 == k1 && memcmp(t, o, sizeof *t) == 0) return;
    if (first || M->labels_k1 != k1) lv_obj_set_style_text_font(s_sub, k1 ? &lv_font_montserrat_30 : &lv_font_montserrat_24, 0);
    M->labels_on = true;
    M->labels_k1 = k1;
    bool big = t->val[0] != 0, wd = t->word[0] != 0;
    text_set(s_val, M->val, sizeof M->val, t->val);
    text_set(s_unit, M->unit, sizeof M->unit, t->unit);
    text_set(s_word, M->word, sizeof M->word, t->word);
    text_set(s_sub, M->sub, sizeof M->sub, t->sub);
    text_set(s_small, M->small, sizeof M->small, t->small);
    show(s_val, big);
    show(s_unit, big && t->unit[0]);
    show(s_word, wd);
    show(s_sub, t->sub[0] != 0);
    show(s_small, t->small[0] != 0);
    color(s_val, &o->val_col, t->val_col, first);
    color(s_unit, &o->unit_col, t->unit_col, first);
    color(s_word, &o->word_col, t->word_col, first);
    color(s_sub, &o->sub_col, t->sub_col, first);
    color(s_small, &o->small_col, t->small_col, first);
    *o = *t;
    int vbase = k1 ? 300 : 388;
    if (big) {
        lv_point_t vs, us = {0, 0};
        lv_text_get_size(&vs, t->val, &font_digits_96, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (t->unit[0]) lv_text_get_size(&us, t->unit, &lv_font_montserrat_30, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int gap = t->unit[0] ? UNIT_GAP : 0, x0 = (SCREEN_W - (vs.x + gap + us.x)) / 2;
        pos(s_val, x0, top_for(&font_digits_96, vbase));
        pos(s_unit, x0 + vs.x + gap, top_for(&lv_font_montserrat_30, vbase));
    }
    place(s_word, k1 ? 292 : 384, 400);
    place(s_sub, k1 ? (wd ? 340 : 344) : (wd ? 418 : 420), TEXT_W);
    place(s_small, 380, TEXT_W - 20);
}

static void fold(const print_block_t *in, print_block_t *out)
{
    *out = *in;
    np_text_fold(in->name, out->name, sizeof out->name);
    np_text_fold(in->material, out->material, sizeof out->material);
    np_text_fold(in->printer, out->printer, sizeof out->printer);
}

static bool done_dim(double now)
{
    if (M->look != PRINT_LOOK_DONE) {
        M->done_seen = false;
        return false;
    }
    double local = pomo_client_now();
    if (!M->done_seen) {
        M->done_seen = true;
        M->done_t0 = local;
    }
    const print_event_t *e = &M->b.event;
    double age = now > 0 && e->kind == PRINT_EV_FINISHED && e->at ? now - (double)e->at : local - M->done_t0;
    return age >= DONE_DIM_S;
}

void page_print_frame(const page_frame_t *f)
{
    if (!M || lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN)) return;
    double t = f->t;
    print_pics_t np;
    if (print_client_take_pics(&np)) {
        print_pics_free(&M->pics);
        M->pics = np;
        if (M->hero == HERO_PICS || M->face == PRINT_FACE_K2) M->drawn = false;
    }
    print_client_get(&M->snap);
    const print_block_t *b = NULL;
    if (M->snap.has) {
        fold(&M->snap.b, &M->b);
        b = &M->b;
    }
    double now = M->snap.have_srv ? pomo_client_now() + M->snap.srv_offset : 0;
    int face = print_faces_shown(&M->faces, b);
    int look = print_look(b);
    bool pics = face == PRINT_FACE_K2 && b && M->pics.ghost && strcmp(M->pics.version, b->thumb_version) == 0 &&
                M->pics.top == b->thumb_top && M->pics.bottom == b->thumb_bottom;
    int hero = pics ? HERO_PICS : HERO_PRINTER;
    print_text(b, (print_face_t)face, now, M->snap.has_utc_offset, M->snap.utc_offset, &M->want);
    attach();
    bool full = !M->drawn || face != M->face || look != M->look || hero != M->hero;
    M->face = face;
    M->look = look;
    M->hero = hero;
    if (s_buf) {
        if (full) {
            memset(s_buf, 0, SCREEN_W * SCREEN_H * 2);
            M->ring_on = false;
            M->pose.look = PRINT_LOOK_NONE;
            M->cache_ok = false;
        }
        print_ring_look_t rl;
        print_ring_look((print_look_t)look, &rl);
        np_edge_style_t st;
        np_edge_style(&st, NP_RING_PROGRESS, rl.col);
        st.track = PRINT_COL_TRACK;
        st.head = rl.head;
        ring_draw(print_ring_frac(b), &st, full);
        if (hero == HERO_PICS) hero_pics(full, t);
        else hero_printer(full, t);
        if (full) {
            dots();
            lv_obj_invalidate(s_canvas);
        }
        M->drawn = true;
    }
    labels(face == PRINT_FACE_K1);
    int wopa = hero == HERO_PICS && (look == PRINT_LOOK_ATTENTION || look == PRINT_LOOK_ERROR)
                   ? (int)lroundf(255 * print_pulse((print_look_t)look, floor(t * 15) / 15))
                   : 255;
    if (wopa != M->word_opa) {
        M->word_opa = wopa;
        lv_obj_set_style_text_opa(s_word, (lv_opa_t)wopa, 0);
    }
    int dim = done_dim(now);
    if (dim != M->dim) {
        M->dim = dim;
        show(s_shade, dim);
    }
}
