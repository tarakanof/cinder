#include "np_cover_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arc_text.h"
#include "esp_heap_caps.h"
#include "np_ctl.h"
#include "page_canvas.h"
#include "timing.h"
#include "touch_swipe.h"

LV_FONT_DECLARE(font_digits_96);

#define SCREEN_W 472
#define SCREEN_H 466
#define FACE NP_COVER_PX
#define FACE_X ((SCREEN_W - FACE) / 2)

_Static_assert(FACE * FACE * 2 <= PAGE_CANVAS_BYTES, "the face without a cover draws on the page canvas");

#define TITLE_W 340
#define ARTIST_W 320
#define TIME_W 260
#define IDLE_W 340
#define AVATAR_DY 21
#define TITLE_DY 67
#define ARTIST_DY 101
#define TIME_DY 135
#define IDLE_DY 111
#define NUM_DY (-69)
#define VOL_ICON_DY (-134)
#define OFFLINE_DY NUM_DY
#define AGE_DY 22
#define IDLE_AGE_DY 143
#define OFFLINE_TEXT_DY (-9)
#define PAUSE_DY (-65)
#define PAUSE_PX 104
#define PAUSE_BAR_W 13
#define PAUSE_BAR_H 46
#define PAUSE_BAR_DX 12

#define COL_TEXT 0xF4F4F2
#define COL_SUB 0xBDBDBD
#define SCRIM_TOP (SCREEN_H / 2 - NP_DISC_R)
#define SCRIM_Y0 205
#define SCRIM_Y1 270
#define SCRIM_STOP(y) (((y) - SCRIM_TOP) * 255 / (2 * NP_DISC_R))
#define FACE_DIM_OPA 109
#define TEXT_DIM_OPA LV_OPA_50
#define V15_TIME_DY (-123)
#define V15_SHADE_R (NP_V15_R + 1)
#define COL_V15_ARTIST 0x9A9A9A

enum { CHG_NONE, CHG_ARMED, CHG_RUN };

static const np_ring_t EDGE = {.cx = FACE / 2.0f, .cy = FACE / 2.0f, .r = NP_EDGE_R, .hw = NP_EDGE_HW};

typedef struct {
    lv_image_dsc_t img_dsc, avatar_dsc, grey_dsc, vinyl_dsc;
    uint8_t glow[NP_GLOW_PX * NP_GLOW_PX];
    np_edge_style_t style;
    char title[2 * NP_TEXT_MAX + 1];
    char artist[2 * NP_TEXT_MAX + 1];
    char time[40];
    char idle[32];
    char badge[24];
    char age[16];
    char num[8];
    char shown[NP_COVERV_MAX + 1];
    char work[4 * NP_TEXT_MAX + 8];
    char fit[4 * NP_TEXT_MAX + 8];
    char arc_title[2 * NP_TEXT_MAX + 1];
    float arc_w[NP_TEXT_MAX];
    arc_item_t arc_it[NP_TEXT_MAX];
    char arc_artist[2 * NP_TEXT_MAX + 1];
    np_art_set_t pend;
    bool pend_vinyl, chg_from, swapped, buf_lent;
    int chg;
    double chg_t0;
    double sweep_t0, sweep_dur;
    bool sweeping, shown_live;
    uint32_t accent;
    bool accent_ok;
    int layout;
    int frame;
    bool text_dirty;
    int text_dx, text_dy;
    int shade_small;
    long long time_ms, time_dur;
    double nopic_t0;
    bool awaiting;
    double hold_t0;
    float hold_frac, prog_frac;
    bool prog_ok, shown_light;
    lv_obj_t *info;
    int info_opa;
    bool info_cover;
    double info_cover_t;
} npc_mem_t;
static npc_mem_t *M;

static lv_obj_t *s_img, *s_vinyl, *s_shade, *s_scrim, *s_avatar, *s_title, *s_artist, *s_time, *s_idle;
static lv_obj_t *s_pause, *s_vol_icon, *s_num, *s_badge, *s_badge_lbl, *s_age_lbl;
static uint16_t *s_buf;
static uint8_t *s_avatar_buf, *s_grey_buf;
static uint16_t *s_vinyl_buf;
static bool s_has_cover, s_no_cover, s_no_cover_painted, s_ring_on;
static int s_avatar_grey = -1;
static float s_frac;
static int s_state = -1;
static long long s_sec = -1;
static int s_shade_opa = -1;
static int s_scrim_opa = -1;
static int s_dim = -1;
static int s_dx, s_dy;
static uint32_t s_time_col;

static void dsc_init(lv_image_dsc_t *d, int px, lv_color_format_t cf)
{
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = cf;
    d->header.w = px;
    d->header.h = px;
    d->header.stride = px * 2;
    d->data_size = (uint32_t)px * px * (cf == LV_COLOR_FORMAT_RGB565A8 ? 3 : 2);
}

bool npc_init(void)
{
    M = heap_caps_calloc(1, sizeof *M, MALLOC_CAP_SPIRAM);
    if (!M) return false;
    dsc_init(&M->img_dsc, FACE, LV_COLOR_FORMAT_RGB565);
    dsc_init(&M->avatar_dsc, NP_AVATAR_PX, LV_COLOR_FORMAT_RGB565A8);
    dsc_init(&M->grey_dsc, NP_AVATAR_PX, LV_COLOR_FORMAT_RGB565A8);
    dsc_init(&M->vinyl_dsc, NP_VINYL_BOX, LV_COLOR_FORMAT_RGB565);
    np_glow_a8(M->glow);
    return true;
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *plain(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *root, const lv_font_t *font, int width, int dy, uint32_t col)
{
    lv_obj_t *l = lv_label_create(root);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if (width > 0) {
        lv_obj_set_size(l, width, lv_font_get_line_height(font));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    }
    lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text_static(l, "");
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

void npc_build(lv_obj_t *root)
{
    s_img = lv_image_create(root);
    lv_obj_set_pos(s_img, FACE_X, 0);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);

    s_vinyl = lv_image_create(root);
    lv_obj_align(s_vinyl, LV_ALIGN_CENTER, 0, NP_VINYL_DY);
    lv_obj_add_flag(s_vinyl, LV_OBJ_FLAG_HIDDEN);

    s_shade = plain(root, 2 * NP_DISC_R, 2 * NP_DISC_R);
    lv_obj_set_style_radius(s_shade, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_shade, lv_color_black(), 0);
    lv_obj_align(s_shade, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_shade, LV_OBJ_FLAG_HIDDEN);

    M->info = plain(root, 2 * NP_DISC_R, 2 * NP_DISC_R);
    lv_obj_align(M->info, LV_ALIGN_CENTER, 0, 0);
    M->info_opa = LV_OPA_COVER;
    M->info_cover = false;

    s_scrim = plain(M->info, 2 * NP_DISC_R, 2 * NP_DISC_R);
    lv_obj_set_style_radius(s_scrim, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_color(s_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_dir(s_scrim, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(s_scrim, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_main_stop(s_scrim, SCRIM_STOP(SCRIM_Y0), 0);
    lv_obj_set_style_bg_grad_stop(s_scrim, SCRIM_STOP(SCRIM_Y1), 0);
    lv_obj_align(s_scrim, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);

    s_avatar = lv_image_create(M->info);
    lv_obj_align(s_avatar, LV_ALIGN_CENTER, 0, AVATAR_DY);
    lv_obj_add_flag(s_avatar, LV_OBJ_FLAG_HIDDEN);

    s_title = label(M->info, &lv_font_montserrat_30, TITLE_W, TITLE_DY, COL_TEXT);
    s_artist = label(M->info, &lv_font_montserrat_24, ARTIST_W, ARTIST_DY, COL_SUB);
    s_time = label(M->info, &lv_font_montserrat_24, TIME_W, TIME_DY, 0xD6D6D6);
    s_idle = label(root, &lv_font_montserrat_30, IDLE_W, IDLE_DY, COL_SUB);

    s_pause = plain(root, PAUSE_PX, PAUSE_PX);
    lv_obj_set_style_radius(s_pause, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_pause, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_pause, 140, 0);
    lv_obj_align(s_pause, LV_ALIGN_CENTER, 0, PAUSE_DY);
    for (int i = -1; i <= 1; i += 2) {
        lv_obj_t *bar = plain(s_pause, PAUSE_BAR_W, PAUSE_BAR_H);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_align(bar, LV_ALIGN_CENTER, i * PAUSE_BAR_DX, 0);
    }
    lv_obj_add_flag(s_pause, LV_OBJ_FLAG_HIDDEN);

    s_vol_icon = label(root, &lv_font_montserrat_30, 0, VOL_ICON_DY, COL_TEXT);
    lv_label_set_text_static(s_vol_icon, LV_SYMBOL_VOLUME_MAX);
    s_num = label(root, &font_digits_96, 0, NUM_DY, COL_TEXT);

    s_badge = npc_round(root, 0);
    s_badge_lbl = lv_obj_get_child(s_badge, 0);
    s_age_lbl = lv_label_create(s_badge);
    lv_obj_set_style_text_font(s_age_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_age_lbl, lv_color_hex(COL_SUB), 0);
    lv_obj_set_style_text_align(s_age_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(s_age_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text_static(s_age_lbl, "");
    lv_obj_align(s_age_lbl, LV_ALIGN_CENTER, 0, AGE_DY);
    lv_obj_add_flag(s_age_lbl, LV_OBJ_FLAG_HIDDEN);

    s_state = -1;
    s_sec = -1;
    s_shade_opa = -1;
    s_scrim_opa = -1;
    s_dim = -1;
    s_dx = s_dy = 0;
    s_ring_on = false;
    s_avatar_grey = -1;
    M->title[0] = M->artist[0] = M->time[0] = M->idle[0] = M->badge[0] = M->num[0] = M->age[0] = 0;
    M->arc_title[0] = M->arc_artist[0] = 0;
    M->chg = CHG_NONE;
    M->sweeping = M->shown_live = false;
    M->layout = -1;
    M->frame = -1;
    M->shade_small = -1;
    M->accent_ok = false;
    M->nopic_t0 = -1;
    M->awaiting = M->prog_ok = M->shown_light = false;
}

static void vinyl_free(void)
{
    heap_caps_free(s_vinyl_buf);
    s_vinyl_buf = NULL;
}

static void chg_drop(void)
{
    np_art_set_free(&M->pend);
    M->pend_vinyl = false;
    M->chg = CHG_NONE;
}

static void buf_drop(void)
{
    if (M->buf_lent) page_canvas_give(PAGE_CANVAS_NP);
    else heap_caps_free(s_buf);
    M->buf_lent = false;
}

static void buf_free(void)
{
    if (s_img) lv_image_set_src(s_img, NULL);
    M->img_dsc.data = NULL;
    buf_drop();
    s_buf = NULL;
    s_has_cover = s_no_cover = s_no_cover_painted = s_ring_on = false;
    chg_drop();
}

void npc_destroy(void)
{
    buf_free();
    vinyl_free();
    heap_caps_free(s_avatar_buf);
    heap_caps_free(s_grey_buf);
    s_avatar_buf = s_grey_buf = NULL;
    s_img = s_vinyl = s_shade = s_scrim = s_avatar = s_title = s_artist = s_time = s_idle = NULL;
    s_pause = s_vol_icon = s_num = s_badge = s_badge_lbl = s_age_lbl = NULL;
    if (M) M->info = NULL;
    s_state = -1;
}

bool npc_has_cover(void) { return s_has_cover || s_no_cover; }

static void set_buf(uint16_t *b)
{
    if (b != s_buf) buf_drop();
    s_buf = b;
    M->img_dsc.data = (const uint8_t *)b;
    lv_image_set_src(s_img, &M->img_dsc);
    lv_obj_invalidate(s_img);
    s_ring_on = false;
}

static bool ensure_buf(void)
{
    if (s_buf) return true;
    uint16_t *b = page_canvas_take(PAGE_CANVAS_NP);
    if (!b) return false;
    memset(b, 0, (size_t)FACE * FACE * 2);
    set_buf(b);
    M->buf_lent = true;
    return true;
}

static void clear_buf(void)
{
    memset(s_buf, 0, (size_t)FACE * FACE * 2);
    lv_obj_invalidate(s_img);
    s_ring_on = false;
}

static void avatar_src(bool grey)
{
    s_avatar_grey = grey;
    lv_image_set_src(s_avatar, grey && s_grey_buf ? &M->grey_dsc : &M->avatar_dsc);
}

static uint32_t accent_of(const np_info_t *np, bool vinyl)
{
    if (np->has_accent) return np->accent;
    return vinyl ? NP_ACCENT_VINYL : NP_ACCENT_DEFAULT;
}

static void paint_no_cover(void)
{
    if (s_no_cover_painted || !ensure_buf()) return;
    int64_t t0 = timing_now();
    np_v15_render(s_buf, FACE, FACE, (np_rect_t){0, 0, FACE, FACE}, M->frame < 0 ? 0 : M->frame, M->accent);
    timing_add(SPAN_V15_RENDER, timing_now() - t0);
    lv_obj_invalidate(s_img);
    s_ring_on = false;
    s_no_cover_painted = true;
    M->text_dirty = true;
}

static void avatar_take(uint8_t *avatar)
{
    lv_obj_add_flag(s_avatar, LV_OBJ_FLAG_HIDDEN);
    heap_caps_free(s_avatar_buf);
    heap_caps_free(s_grey_buf);
    s_avatar_buf = avatar;
    s_grey_buf = NULL;
    M->avatar_dsc.data = s_avatar_buf;
    if (s_avatar_buf) {
        const size_t n = (size_t)NP_AVATAR_PX * NP_AVATAR_PX;
        s_grey_buf = heap_caps_malloc(n * 3, MALLOC_CAP_SPIRAM);
        if (s_grey_buf) {
            np_grey565((const uint16_t *)s_avatar_buf, (uint16_t *)s_grey_buf, (int)n, 0.75f);
            memcpy(s_grey_buf + n * 2, s_avatar_buf + n * 2, n);
        }
        M->grey_dsc.data = s_grey_buf;
        avatar_src(false);
    }
}

static void apply_set(np_art_set_t *in)
{
    if (in->cover) {
        set_buf(in->cover);
        in->cover = NULL;
        s_has_cover = true;
        s_no_cover = false;
    } else if (in->no_cover) {
        s_has_cover = s_no_cover_painted = false;
        s_no_cover = true;
    } else if (npc_has_cover() && s_buf) {
        clear_buf();
        s_has_cover = s_no_cover = false;
    }
    strlcpy(M->shown, npc_has_cover() ? in->version : "", sizeof M->shown);
    avatar_take(in->avatar);
    in->avatar = NULL;
}

static void chg_arm(void)
{
    if (M->chg == CHG_RUN && !M->swapped) return;
    M->chg_from = npc_has_cover();
    M->chg = CHG_ARMED;
}

static bool same_shown(const np_art_set_t *in)
{
    if (strcmp(in->version, M->shown) != 0) return false;
    return in->cover ? s_has_cover : in->no_cover && s_no_cover;
}

void npc_adopt(np_art_set_t *in)
{
    if (!s_img) return;
    if (M->shown_live && same_shown(in)) {
        chg_drop();
        if (in->cover) {
            apply_set(in);
        } else {
            avatar_take(in->avatar);
            in->avatar = NULL;
        }
        return;
    }
    if (M->shown_live && (in->cover || in->no_cover)) {
        np_art_set_free(&M->pend);
        M->pend = *in;
        memset(in, 0, sizeof *in);
        M->pend_vinyl = false;
        chg_arm();
        return;
    }
    chg_drop();
    apply_set(in);
    M->accent_ok = false;
    s_state = -1;
}

static void chg_vinyl(void)
{
    np_art_set_free(&M->pend);
    M->pend_vinyl = true;
    chg_arm();
}

static void chg_swap(const np_info_t *np)
{
    if (M->pend_vinyl) {
        M->accent = accent_of(np, true);
        s_has_cover = s_no_cover_painted = false;
        s_no_cover = true;
        M->shown[0] = 0;
        avatar_take(NULL);
    } else {
        M->accent = accent_of(np, !M->pend.cover);
        apply_set(&M->pend);
        np_art_set_free(&M->pend);
    }
    if (s_no_cover) paint_no_cover();
    M->pend_vinyl = false;
    M->accent_ok = true;
    s_ring_on = false;
}

void npc_fit_text(char *txt, const lv_font_t *font, int max_w)
{
    lv_point_t sz;
    lv_text_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x <= max_w) return;
    size_t lo = 0, hi = strlen(txt);
    char *tmp = M->fit;
    if (hi + 4 > sizeof M->fit) hi = sizeof M->fit - 4;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        memcpy(tmp, txt, mid);
        strcpy(tmp + mid, "...");
        lv_text_get_size(&sz, tmp, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= max_w) lo = mid;
        else hi = mid - 1;
    }
    while (lo > 0 && (txt[lo - 1] == ' ' || txt[lo - 1] == '-')) lo--;
    strcpy(txt + lo, "...");
}

static void text_set(lv_obj_t *l, char *shown, size_t cap, const char *txt)
{
    if (strcmp(shown, txt) == 0) return;
    strlcpy(shown, txt, cap);
    lv_label_set_text_static(l, shown);
}

static void ring_draw(float frac, const np_edge_style_t *st)
{
    int64_t t0 = timing_now();
    if (!s_ring_on || !np_edge_style_eq(st, &M->style)) {
        M->style = *st;
        np_edge_render(&EDGE, s_buf, FACE, FACE, (np_rect_t){0, 0, FACE, FACE}, frac, st, M->glow);
        lv_obj_invalidate(s_img);
    } else {
        np_rect_t d;
        if (!np_edge_dirty(&EDGE, s_frac, frac, FACE, FACE, &d)) return;
        np_edge_render(&EDGE, s_buf, FACE, FACE, d, frac, st, M->glow);
        lv_area_t a = {.x1 = d.x + FACE_X, .y1 = d.y, .x2 = d.x + FACE_X + d.w - 1, .y2 = d.y + d.h - 1};
        lv_obj_invalidate_area(s_img, &a);
    }
    s_ring_on = true;
    s_frac = frac;
    timing_add(SPAN_RING, timing_now() - t0);
}

static void place_text(int dx, int dy)
{
    if (M->layout == 1) {
        lv_obj_align(s_avatar, LV_ALIGN_CENTER, 0, NP_V15_CY - FACE / 2);
        lv_obj_align(s_time, LV_ALIGN_CENTER, dx, V15_TIME_DY + dy);
        return;
    }
    lv_obj_align(s_avatar, LV_ALIGN_CENTER, dx, AVATAR_DY + dy);
    lv_obj_align(s_title, LV_ALIGN_CENTER, dx, TITLE_DY + dy);
    lv_obj_align(s_artist, LV_ALIGN_CENTER, dx, ARTIST_DY + dy);
    lv_obj_align(s_time, LV_ALIGN_CENTER, dx, TIME_DY + dy);
}

static void info_fade(int opa)
{
    if (opa == M->info_opa) return;
    int64_t t0 = timing_now();
    M->info_opa = opa;
    lv_obj_set_style_opa(M->info, (lv_opa_t)opa, 0);
    show(M->info, opa > 0);
    timing_add(SPAN_FADE, timing_now() - t0);
}

static void hide_overlays(void)
{
    show(s_pause, false);
    show(s_vol_icon, false);
    show(s_num, false);
    show(s_badge, false);
    show(s_shade, false);
    show(s_scrim, false);
    M->badge[0] = M->num[0] = M->age[0] = 0;
    lv_label_set_text_static(s_age_lbl, M->age);
    show(s_age_lbl, false);
    lv_obj_align(s_badge_lbl, LV_ALIGN_CENTER, 0, 0);
    s_shade_opa = 0;
    s_scrim_opa = 0;
}

static bool vinyl_ready(void)
{
    if (s_vinyl_buf) return true;
    s_vinyl_buf = heap_caps_malloc((size_t)NP_VINYL_BOX * NP_VINYL_BOX * 2, MALLOC_CAP_SPIRAM);
    if (!s_vinyl_buf) return false;
    np_vinyl_render(s_vinyl_buf, NP_VINYL_BOX, NP_VINYL_BOX, NP_VINYL_BOX / 2.0f, NP_VINYL_BOX / 2.0f);
    M->vinyl_dsc.data = (const uint8_t *)s_vinyl_buf;
    lv_image_set_src(s_vinyl, &M->vinyl_dsc);
    return true;
}

static void idle_update(const npc_frame_t *f, bool relayout)
{
    const char *line = f->state == NPF_OFFLINE_EMPTY ? "Ember offline"
                       : f->state == NPF_WAITING   ? "Waiting for Ember"
                                                   : "Nothing playing";
    if (relayout) {
        show(s_avatar, false);
        show(s_title, false);
        show(s_artist, false);
        show(s_time, false);
        hide_overlays();
        show(s_img, false);
        if (npc_has_cover()) np_client_forget_art();
        buf_free();
        M->shown_live = M->sweeping = M->awaiting = false;
        M->layout = -1;
        M->nopic_t0 = -1;
        show(s_vinyl, vinyl_ready());
        show(s_idle, true);
        info_fade(LV_OPA_COVER);
        M->info_cover = false;
        lv_obj_align(s_vinyl, LV_ALIGN_CENTER, f->dx, NP_VINYL_DY + f->dy);
        lv_obj_align(s_idle, LV_ALIGN_CENTER, f->dx, IDLE_DY + f->dy);
        lv_obj_align(s_time, LV_ALIGN_CENTER, f->dx, IDLE_AGE_DY + f->dy);
        lv_obj_set_style_text_color(s_time, lv_color_hex(COL_SUB), 0);
        lv_obj_set_style_text_opa(s_time, LV_OPA_COVER, 0);
        s_time_col = 0;
        s_dx = f->dx;
        s_dy = f->dy;
        s_dim = -1;
    }
    if (f->dx != s_dx || f->dy != s_dy) {
        s_dx = f->dx;
        s_dy = f->dy;
        lv_obj_align(s_vinyl, LV_ALIGN_CENTER, f->dx, NP_VINYL_DY + f->dy);
        lv_obj_align(s_idle, LV_ALIGN_CENTER, f->dx, IDLE_DY + f->dy);
        lv_obj_align(s_time, LV_ALIGN_CENTER, f->dx, IDLE_AGE_DY + f->dy);
    }
    text_set(s_idle, M->idle, sizeof M->idle, line);
    const char *age = f->state == NPF_OFFLINE_EMPTY && f->offline_age ? f->offline_age : "";
    text_set(s_time, M->time, sizeof M->time, age);
    show(s_time, age[0] != 0);
}

static void badge_update(const npc_frame_t *f, const np_face_look_t *look)
{
    bool number = f->badge == NPC_BADGE_NUMBER && f->badge_text;
    const char *num = number ? f->badge_text : "";
    if (strcmp(num, M->num) != 0) {
        strlcpy(M->num, num, sizeof M->num);
        lv_label_set_text_static(s_num, M->num);
        int nx, ny;
        npc_ink_offset(&font_digits_96, M->num, &nx, &ny);
        lv_obj_align(s_num, LV_ALIGN_CENTER, nx, NUM_DY + ny);
        show(s_num, number);
        show(s_vol_icon, number);
    }
    const char *b = "";
    const char *age = "";
    int dy = 0;
    const lv_font_t *font = &lv_font_montserrat_18;
    if (f->badge == NPC_BADGE_ICON && f->badge_text) {
        b = f->badge_text;
        font = &lv_font_montserrat_30;
    } else if (f->badge == NPC_BADGE_ERROR && f->badge_text) {
        b = f->badge_text;
    } else if (look->offline_badge) {
        b = "Ember\noffline";
        dy = OFFLINE_DY;
        if (f->offline_age) age = f->offline_age;
    }
    int y = TS_CTL_Y + dy - TS_CTL_PX / 2;
    if (lv_obj_get_style_y(s_badge, 0) != y) lv_obj_set_y(s_badge, y);
    if (strcmp(b, M->badge) != 0) {
        strlcpy(M->badge, b, sizeof M->badge);
        lv_obj_set_style_text_font(s_badge_lbl, font, 0);
        lv_label_set_text_static(s_badge_lbl, M->badge);
        show(s_badge, M->badge[0] != 0);
    }
    if (strcmp(age, M->age) != 0) {
        strlcpy(M->age, age, sizeof M->age);
        lv_label_set_text_static(s_age_lbl, M->age);
        show(s_age_lbl, M->age[0] != 0);
        lv_obj_align(s_badge_lbl, LV_ALIGN_CENTER, 0, M->age[0] ? OFFLINE_TEXT_DY : 0);
    }
    show(s_pause, look->pause_icon && !M->badge[0] && !number && !f->controls);
    show(s_avatar, s_avatar_buf != NULL && (!M->badge[0] || dy) && !f->controls);
}

static void arc_line(lv_obj_t *canvas, const lv_font_t *font, const char *text, float r, uint32_t rgb, int dx, int dy)
{
    float *w = M->arc_w;
    arc_item_t *it = M->arc_it;
    int n = 0;
    for (const char *p = text; *p && n < NP_TEXT_MAX; p++)
        w[n++] = (float)lv_font_get_glyph_width(font, (uint8_t)p[0], (uint8_t)p[1]);
    if (!n) return;
    arc_text_layout(FACE / 2.0f + dx, FACE / 2.0f + dy, r, w, n, it);
    for (int i = 0; i < n; i++) {
        lv_draw_letter_dsc_t d;
        lv_draw_letter_dsc_init(&d);
        d.font = font;
        d.color = lv_color_hex(rgb);
        d.unicode = (uint8_t)text[i];
        d.rotation = (int32_t)lroundf(it[i].rot_deg * 10);
        lv_point_t pt = {(int32_t)lroundf(it[i].x), (int32_t)lroundf(it[i].y)};
        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);
        lv_draw_letter(&layer, &d, &pt);
        lv_canvas_finish_layer(canvas, &layer);
    }
}

static void v15_text(int dx, int dy)
{
    lv_obj_t *c = lv_canvas_create(NULL);
    if (!c) return;
    int64_t t0 = timing_now();
    np_rect_t b = np_v15_text_box();
    np_v15_render(s_buf, FACE, FACE, b, M->frame < 0 ? 0 : M->frame, M->accent);
    timing_add(SPAN_V15_RENDER, timing_now() - t0);
    lv_canvas_set_buffer(c, s_buf, FACE, FACE, LV_COLOR_FORMAT_RGB565);
    int64_t t1 = timing_now();
    arc_line(c, &lv_font_montserrat_30, M->arc_title, NP_V15_TITLE_R, COL_TEXT, dx, dy);
    arc_line(c, &lv_font_montserrat_24, M->arc_artist, NP_V15_ARTIST_R, COL_V15_ARTIST, dx, dy);
    timing_add(SPAN_ARC_TEXT, timing_now() - t1);
    lv_obj_delete(c);
    lv_area_t a = {.x1 = b.x + FACE_X, .y1 = b.y, .x2 = b.x + FACE_X + b.w - 1, .y2 = b.y + b.h - 1};
    lv_obj_invalidate_area(s_img, &a);
    M->text_dx = dx;
    M->text_dy = dy;
    M->text_dirty = false;
    timing_add(SPAN_LABEL, timing_now() - t0);
}

static void v15_strings(const np_info_t *np)
{
    int64_t t0 = timing_now();
    char *w = M->work;
    np_text_fold(np->title[0] ? np->title : "Unknown track", w, sizeof M->arc_title);
    npc_fit_text(w, &lv_font_montserrat_30, NP_V15_TEXT_W);
    if (strcmp(w, M->arc_title) != 0) {
        strlcpy(M->arc_title, w, sizeof M->arc_title);
        M->text_dirty = true;
    }
    np_text_fold(np->artist[0] ? np->artist : np->album, w, sizeof M->arc_artist);
    npc_fit_text(w, &lv_font_montserrat_24, NP_V15_TEXT_W);
    if (strcmp(w, M->arc_artist) != 0) {
        strlcpy(M->arc_artist, w, sizeof M->arc_artist);
        M->text_dirty = true;
    }
    timing_add(SPAN_LABEL, timing_now() - t0);
}

static void v15_spin(int frame)
{
    if (frame == M->frame) return;
    M->frame = frame;
    if (!s_no_cover_painted) return;
    np_rect_t b = np_v15_label_box();
    int64_t t0 = timing_now();
    np_v15_render(s_buf, FACE, FACE, b, frame, M->accent);
    timing_add(SPAN_V15_RENDER, timing_now() - t0);
    lv_area_t a = {.x1 = b.x + FACE_X, .y1 = b.y, .x2 = b.x + FACE_X + b.w - 1, .y2 = b.y + b.h - 1};
    lv_obj_invalidate_area(s_img, &a);
}

static void layout_set(bool v15)
{
    M->layout = v15;
    show(s_title, !v15);
    show(s_artist, !v15);
    show(s_time, true);
    if (v15) M->arc_title[0] = M->arc_artist[0] = 0;
}

static void shade_geometry(bool small)
{
    if ((int)small == M->shade_small) return;
    M->shade_small = small;
    int px = 2 * (small ? V15_SHADE_R : NP_DISC_R);
    lv_obj_set_size(s_shade, px, px);
    lv_obj_align(s_shade, LV_ALIGN_CENTER, 0, small ? NP_V15_CY - FACE / 2 : 0);
}

static float sweep_k(double now)
{
    if (!M->sweeping) return 1;
    float k = np_sweep_k(now - M->sweep_t0, M->sweep_dur);
    if (k >= 1) M->sweeping = false;
    return k;
}

static void sweep_start(double now, double dur)
{
    M->sweeping = true;
    M->sweep_t0 = now;
    M->sweep_dur = dur;
}

static uint8_t chg_step(const npc_frame_t *f, float *keep)
{
    *keep = 1;
    if (M->chg == CHG_NONE) return 0;
    if (M->chg == CHG_ARMED) {
        M->chg = CHG_RUN;
        M->chg_t0 = f->now;
        M->swapped = false;
    }
    np_change_t c;
    np_change_at(f->now - M->chg_t0, M->chg_from, &c);
    if (c.swap && !M->swapped) {
        M->swapped = true;
        int64_t t0 = timing_now();
        chg_swap(f->np);
        timing_add(SPAN_M8, timing_now() - t0);
        if (M->chg_from) sweep_start(f->now, NP_REGROW_S);
    }
    if (c.done) M->chg = CHG_NONE;
    *keep = c.keep;
    return c.fade;
}

static void time_text(const npc_frame_t *f, long long shown_ms, bool v15)
{
    char p[16], d[16];
    if (f->state == NPF_PAUSED) {
        strlcpy(M->work, "Paused", sizeof M->work);
    } else if (v15 && f->dur_ms > 0) {
        long long left = f->dur_ms - shown_ms;
        np_format_time(left < 0 ? 0 : left, p, sizeof p);
        snprintf(M->work, sizeof M->work, "-%s", p);
    } else {
        np_format_time(shown_ms, p, sizeof p);
        np_format_time(f->dur_ms, d, sizeof d);
        if (f->dur_ms > 0) snprintf(M->work, sizeof M->work, "%s / %s", p, d);
        else strlcpy(M->work, p, sizeof M->work);
    }
    text_set(s_time, M->time, sizeof M->time, M->work);
}

static void chg_revalidate(const np_info_t *np)
{
    if (M->chg == CHG_NONE || (M->chg == CHG_RUN && M->swapped)) return;
    bool ok = M->pend_vinyl ? !np->cover_version[0] : strcmp(M->pend.version, np->cover_version) == 0;
    if (!ok) chg_drop();
}

static void nopic_step(const np_info_t *np, double now)
{
    if (np->cover_version[0]) {
        M->nopic_t0 = -1;
        return;
    }
    if (M->nopic_t0 < 0) M->nopic_t0 = now;
    if (now - M->nopic_t0 < NP_NOPIC_GRACE_S || (M->chg != CHG_NONE && M->pend_vinyl)) return;
    if (!s_no_cover) {
        chg_vinyl();
    } else if (M->shown[0]) {
        M->shown[0] = 0;
        avatar_take(NULL);
    }
}

static float ring_target(const npc_frame_t *f, const np_info_t *np)
{
    float live = f->dur_ms > 0 ? (float)((double)(s_sec * 1000) / (double)f->dur_ms) : 0;
    bool awaiting = npc_has_cover() && strcmp(M->shown, np->cover_version) != 0;
    if (awaiting && !M->awaiting) {
        M->hold_t0 = f->now;
        M->hold_frac = M->prog_ok ? M->prog_frac : live;
    }
    M->awaiting = awaiting;
    return awaiting && f->now - M->hold_t0 < NP_RING_HOLD_S ? M->hold_frac : live;
}

static void update_run(const npc_frame_t *f)
{
    if (!s_img || !M) return;
    bool idle = f->state == NPF_WAITING || f->state == NPF_NOTHING || f->state == NPF_OFFLINE_EMPTY;
    if (idle) {
        idle_update(f, (int)f->state != s_state);
        s_state = f->state;
        return;
    }
    if (!ensure_buf()) return;
    const np_info_t *np = f->np;
    if (!M->shown_live && np_face_live(f->state)) sweep_start(f->now, NP_ARRIVE_S);
    M->shown_live = true;
    chg_revalidate(np);
    nopic_step(np, f->now);
    float keep;
    bool changing = M->chg != CHG_NONE;
    int64_t t0 = timing_now();
    uint8_t fade = chg_step(f, &keep);
    if (changing) timing_add(SPAN_FADE, timing_now() - t0);
    bool held = M->accent_ok && npc_has_cover() && strcmp(M->shown, np->cover_version) != 0;
    if (!held) {
        uint32_t a = accent_of(np, s_no_cover);
        if (a != M->accent && s_no_cover_painted) s_no_cover_painted = false;
        M->accent = a;
        M->accent_ok = true;
    }
    if (s_no_cover) paint_no_cover();
    bool v15 = s_no_cover;
    bool relayout = (int)f->state != s_state || (int)v15 != M->layout;
    np_face_look_t look;
    np_face_look(f->state, f->ring, M->accent, &look);
    bool cut = M->sweeping && f->state == NPF_OFFLINE;
    bool frozen = f->state == NPF_OFFLINE && s_sec >= 0 && s_state != NPF_PAUSED && !cut;
    if (relayout) {
        show(s_idle, false);
        show(s_vinyl, false);
        vinyl_free();
        show(s_img, true);
        layout_set(v15);
    }
    if (s_avatar_buf && (int)look.avatar_grey != s_avatar_grey) avatar_src(look.avatar_grey);

    if (v15) {
        if (f->text_fresh || relayout || !M->arc_title[0]) v15_strings(np);
        if (M->text_dirty || f->dx != M->text_dx || f->dy != M->text_dy) v15_text(f->dx, f->dy);
        int frame = f->state == NPF_PLAYING && M->chg == CHG_NONE ? (int)fmod(floor(f->now * NP_V15_FPS), NP_V15_FRAMES)
                                                                   : M->frame;
        v15_spin(frame < 0 ? 0 : frame);
    } else if (f->text_fresh || relayout) {
        char *w = M->work;
        np_text_fold(np->title[0] ? np->title : "Unknown track", w, sizeof M->title);
        npc_fit_text(w, &lv_font_montserrat_30, TITLE_W);
        text_set(s_title, M->title, sizeof M->title, w);
        np_text_fold(np->artist[0] ? np->artist : np->album, w, sizeof M->artist);
        npc_fit_text(w, &lv_font_montserrat_24, ARTIST_W);
        text_set(s_artist, M->artist, sizeof M->artist, w);
    }

    if (f->ring == NP_RING_VOLUME || f->state == NPF_OFFLINE) M->sweeping = false;
    float k = sweep_k(f->now);
    if (!frozen) {
        s_sec = f->pos_ms / 1000;
        long long shown = (long long)((double)(s_sec * 1000) * k);
        if (shown / 1000 != M->time_ms / 1000 || relayout || f->text_fresh || f->dur_ms != M->time_dur) {
            M->time_ms = shown;
            M->time_dur = f->dur_ms;
            time_text(f, shown, v15);
        }
    }
    if (look.time_col != s_time_col || relayout) {
        s_time_col = look.time_col;
        lv_obj_set_style_text_color(s_time, lv_color_hex(v15 && look.time_col == 0xD6D6D6 ? COL_SUB : look.time_col), 0);
    }

    float frac;
    float target = ring_target(f, np);
    if (f->ring == NP_RING_VOLUME) frac = f->vol / 100.0f;
    else if (frozen && s_ring_on) frac = s_frac;
    else frac = target * k * keep;
    if (frac > 1) frac = 1;
    np_edge_style_t st;
    np_edge_style(&st, f->ring, look.ring);
    ring_draw(frac, &st);
    if (f->ring == NP_RING_PROGRESS) {
        M->prog_frac = frac;
        M->prog_ok = true;
    }

    shade_geometry(v15 && M->chg == CHG_NONE);
    int shade = np_shade_mix(look.shade, fade);
    if (shade != s_shade_opa) {
        s_shade_opa = shade;
        lv_obj_set_style_bg_opa(s_shade, (lv_opa_t)shade, 0);
        show(s_shade, shade > 0);
    }
    if (strcmp(M->shown, np->cover_version) == 0) M->shown_light = np->cover_light;
    int scrim = s_has_cover ? np_scrim_opa(M->shown_light, look.shade) : 0;
    if (scrim != s_scrim_opa) {
        s_scrim_opa = scrim;
        lv_obj_set_style_bg_grad_opa(s_scrim, (lv_opa_t)scrim, 0);
        show(s_scrim, scrim > 0);
    }
    badge_update(f, &look);
    bool cover = s_has_cover && !v15;
    if (cover && !M->info_cover) M->info_cover_t = f->now;
    M->info_cover = cover;
    int arrive = cover ? np_fade_opa(f->now - M->info_cover_t, 0) : LV_OPA_COVER;
    info_fade(LV_OPA_COVER - f->text_out > arrive ? LV_OPA_COVER - f->text_out : arrive);

    if ((int)f->dim != s_dim) {
        s_dim = f->dim;
        lv_obj_set_style_image_opa(s_img, f->dim ? FACE_DIM_OPA : LV_OPA_COVER, 0);
        lv_obj_set_style_image_opa(s_avatar, f->dim ? TEXT_DIM_OPA : LV_OPA_COVER, 0);
        lv_obj_set_style_text_opa(s_title, f->dim ? TEXT_DIM_OPA : LV_OPA_COVER, 0);
        lv_obj_set_style_text_opa(s_artist, f->dim ? TEXT_DIM_OPA : LV_OPA_COVER, 0);
        lv_obj_set_style_text_opa(s_time, f->dim ? TEXT_DIM_OPA : LV_OPA_COVER, 0);
        lv_obj_set_style_text_opa(s_badge_lbl, f->dim ? TEXT_DIM_OPA : LV_OPA_COVER, 0);
    }
    if (relayout || f->dx != s_dx || f->dy != s_dy) {
        s_dx = f->dx;
        s_dy = f->dy;
        place_text(f->dx, f->dy);
    }
    s_state = f->state;
}

bool npc_vinyl(void) { return s_no_cover; }

const char *npc_time_text(void) { return M ? M->time : ""; }

void npc_ink_offset(const lv_font_t *font, const char *text, int *dx, int *dy)
{
    int x = 0, left = 0, right = 0, top = 0, bottom = 0;
    bool any = false;
    for (const char *p = text; *p; p++) {
        lv_font_glyph_dsc_t g;
        if (!lv_font_get_glyph_dsc(font, &g, (uint8_t)*p, (uint8_t)p[1])) continue;
        if (g.box_w && g.box_h) {
            int l = x + g.ofs_x, r = l + g.box_w, t = g.ofs_y + g.box_h;
            if (!any || l < left) left = l;
            if (!any || r > right) right = r;
            if (!any || t > top) top = t;
            if (!any || g.ofs_y < bottom) bottom = g.ofs_y;
            any = true;
        }
        x += g.adv_w;
    }
    int h = lv_font_get_line_height(font);
    *dx = any ? (2 * (x / 2) - left - right) / 2 : 0;
    *dy = any ? (2 * (h / 2) - 2 * (h - font->base_line) + top + bottom) / 2 : 0;
}

lv_obj_t *npc_round(lv_obj_t *root, int dx)
{
    lv_obj_t *o = plain(root, TS_CTL_PX, TS_CTL_PX);
    lv_obj_set_pos(o, TS_CX + dx - TS_CTL_PX / 2, TS_CTL_Y - TS_CTL_PX / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, NPC_ROUND_OPA, 0);
    lv_obj_t *l = lv_label_create(o);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text_static(l, "");
    lv_obj_center(l);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

void npc_round_lit(lv_obj_t *o, bool lit)
{
    lv_obj_set_style_bg_color(o, lit ? lv_color_hex(COL_TEXT) : lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, lit ? LV_OPA_COVER : NPC_ROUND_OPA, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(o, 0), lit ? lv_color_black() : lv_color_hex(COL_TEXT), 0);
}

void npc_update(const npc_frame_t *f)
{
    int64_t t0 = timing_now();
    update_run(f);
    timing_add(SPAN_UPDATE, timing_now() - t0);
}
