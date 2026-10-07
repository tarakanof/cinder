#include "nowplaying_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "np.h"
#include "np_ctl.h"
#include "nowplaying_client.h"

#define SCREEN_W 472   /* driver frame 472; visible circle 466 (docs/llm.md) */
#define SCREEN_H 466
#define FACE NP_BACKDROP_PX
#define FACE_X ((SCREEN_W - FACE) / 2)
#define ALBUM_DY (-44)

#define COL_TRACK 0x2E2E2E
#define COL_ARC 0xF4F4F2
#define COL_ARC_PAUSED 0x6E6E6E
#define COL_TEXT 0xF4F4F2
#define COL_SUB 0xBDBDBD
#define COL_META 0x8A8A8A
#define COL_IDLE 0x5A5A5A
#define COL_PLACEHOLDER 0x1C1C1C

#define TITLE_W 290
#define SUB_W 260
#define BURN_IN_S 180.0
#define FACE_DIM_OPA 109
#define TEXT_DIM_OPA LV_OPA_50
#define AVATAR_EVERY_S 2.0
#define DRIFT_EVERY_S 60.0
#define DRIFT_PX 8
#define DWELL_S 0.4
#define VOL_ARC_PX 430
#define VOL_ARC_W 8
#define BADGE_PX 132
#define FLASH_S 0.8
#define ERROR_S 2.5

static const np_ring_t RING = {.cx = FACE / 2.0f, .cy = FACE / 2.0f, .r = 198, .hw = 3};

enum { MODE_UNSET = -1, MODE_IDLE, MODE_PLAYING, MODE_PAUSED };

static lv_obj_t *s_parent;
static lv_obj_t *s_root, *s_face, *s_album, *s_album_ph, *s_avatar, *s_dot;
static lv_obj_t *s_title, *s_sub, *s_meta, *s_idle;
static lv_obj_t *s_vol_arc, *s_badge, *s_badge_lbl;
static uint16_t *s_face_buf;

typedef struct {
    lv_image_dsc_t face_dsc, album_dsc, avatar_dsc;
    np_art_set_t art;
    np_snapshot_t snap;
    char title[2 * NP_TEXT_MAX + 1];
    char sub[4 * NP_TEXT_MAX + 8];
    char meta[64];
    char idle[32];
    char work[4 * NP_TEXT_MAX + 8];
    char fit[4 * NP_TEXT_MAX + 8];
    np_ctl_t ctl;
    char badge[24];
} view_mem_t;
static view_mem_t *V;
#define s_face_dsc (V->face_dsc)
#define s_album_dsc (V->album_dsc)
#define s_avatar_dsc (V->avatar_dsc)
#define s_art (V->art)

static uint32_t s_gen;
static bool s_shown, s_ring_valid;
static float s_frac;
static uint32_t s_arc_col;
static float s_avatar_frac = -1;
static double s_avatar_t;
static int s_mode = MODE_UNSET;
static long long s_meta_sec = -1;
static double s_last_input;
static bool s_dimmed;
static int s_drift;
static double s_drift_t;
static double s_show_t;
static bool s_told;
static int s_idle_drift;
static double s_idle_drift_t;
static int s_vol_shown = -2;
static uint32_t s_result_seq;
static int s_error;
static double s_error_t = -1e9;

static lv_obj_t *label_create(const lv_font_t *font, int width, int dy, uint32_t col)
{
    lv_obj_t *l = lv_label_create(s_root);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(l, width, lv_font_get_line_height(font));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text_static(l, "");
    return l;
}

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

static lv_obj_t *circle_create(int size, uint32_t col)
{
    lv_obj_t *o = lv_obj_create(s_root);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

void np_view_create(lv_obj_t *parent)
{
    s_parent = parent;
    V = heap_caps_calloc(1, sizeof *V, MALLOC_CAP_SPIRAM);
    if (!V) return;
    dsc_init(&s_face_dsc, FACE, LV_COLOR_FORMAT_RGB565);
    dsc_init(&s_album_dsc, NP_ALBUM_PX, LV_COLOR_FORMAT_RGB565A8);
    dsc_init(&s_avatar_dsc, NP_ARTIST_PX, LV_COLOR_FORMAT_RGB565A8);
}

static void build(void)
{
    s_root = lv_obj_create(s_parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_face = lv_image_create(s_root);
    lv_obj_set_pos(s_face, FACE_X, 0);
    lv_obj_add_flag(s_face, LV_OBJ_FLAG_HIDDEN);

    s_album_ph = circle_create(NP_ALBUM_PX, COL_PLACEHOLDER);
    lv_obj_align(s_album_ph, LV_ALIGN_CENTER, 0, ALBUM_DY);
    lv_obj_t *note = lv_label_create(s_album_ph);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(0x4A4A4A), 0);
    lv_label_set_text_static(note, LV_SYMBOL_AUDIO);
    lv_obj_center(note);
    lv_obj_add_flag(s_album_ph, LV_OBJ_FLAG_HIDDEN);

    s_album = lv_image_create(s_root);
    lv_obj_align(s_album, LV_ALIGN_CENTER, 0, ALBUM_DY);
    lv_obj_add_flag(s_album, LV_OBJ_FLAG_HIDDEN);

    s_dot = circle_create(16, COL_ARC);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    s_avatar = lv_image_create(s_root);
    lv_obj_add_flag(s_avatar, LV_OBJ_FLAG_HIDDEN);

    s_title = label_create(&lv_font_montserrat_24, TITLE_W, 96, COL_TEXT);
    s_sub = label_create(&lv_font_montserrat_18, SUB_W, 126, COL_SUB);
    s_meta = label_create(&lv_font_montserrat_14, 220, 150, COL_META);
    s_idle = label_create(&lv_font_montserrat_24, 300, 0, COL_IDLE);

    s_vol_arc = lv_arc_create(s_root);
    lv_obj_set_size(s_vol_arc, VOL_ARC_PX, VOL_ARC_PX);
    lv_obj_align(s_vol_arc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(s_vol_arc, 135);
    lv_arc_set_bg_angles(s_vol_arc, 0, 270);
    lv_arc_set_range(s_vol_arc, 0, 100);
    lv_obj_remove_style(s_vol_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_vol_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_vol_arc, VOL_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_vol_arc, VOL_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_vol_arc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_vol_arc, lv_color_hex(COL_ARC), LV_PART_INDICATOR);
    lv_obj_add_flag(s_vol_arc, LV_OBJ_FLAG_HIDDEN);

    s_badge = circle_create(BADGE_PX, 0x000000);
    lv_obj_set_style_bg_opa(s_badge, LV_OPA_70, 0);
    lv_obj_align(s_badge, LV_ALIGN_CENTER, 0, ALBUM_DY);
    s_badge_lbl = lv_label_create(s_badge);
    lv_obj_set_style_text_color(s_badge_lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_align(s_badge_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_badge_lbl, BADGE_PX - 16);
    lv_label_set_text_static(s_badge_lbl, "");
    lv_obj_center(s_badge_lbl);
    lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
}

static void free_pictures(void)
{
    lv_obj_add_flag(s_face, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_album, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_avatar, LV_OBJ_FLAG_HIDDEN);
    bool held = s_art.album || s_art.artist;
    free(s_face_buf);
    s_face_buf = NULL;
    np_art_set_free(&s_art);
    s_ring_valid = false;
    if (held) np_client_forget_art();
}

void np_view_show(bool show)
{
    if (!V || show == s_shown) return;
    s_shown = show;
    if (show) {
        build();
        s_show_t = esp_timer_get_time() / 1e6;
        s_idle_drift = 0;
        s_idle_drift_t = s_show_t;
        s_mode = MODE_UNSET;
        s_gen--;
        s_last_input = esp_timer_get_time() / 1e6;
        s_dimmed = false;
        s_avatar_frac = -1;
        np_ctl_init(&V->ctl);
        s_vol_shown = -2;
        V->badge[0] = 0;
        s_error_t = -1e9;
        np_ctl_result_t r;
        np_client_control_result(&r);
        s_result_seq = r.seq;
    } else {
        free_pictures();
        lv_obj_delete(s_root);
        s_root = s_face = s_album = s_album_ph = s_avatar = s_dot = NULL;
        s_title = s_sub = s_meta = s_idle = NULL;
        s_vol_arc = s_badge = s_badge_lbl = NULL;
        V->title[0] = V->sub[0] = V->meta[0] = V->idle[0] = 0;
        np_client_set_visible(false);
        s_told = false;
    }
}

static void set_face_buf(uint16_t *buf)
{
    if (buf != s_face_buf) free(s_face_buf);
    s_face_buf = buf;
    s_face_dsc.data = (const uint8_t *)buf;
    lv_image_set_src(s_face, &s_face_dsc);
    s_ring_valid = false;
}

static bool ensure_face(void)
{
    if (s_face_buf) return true;
    uint16_t *b = heap_caps_calloc((size_t)FACE * FACE, 2, MALLOC_CAP_SPIRAM);
    if (!b) return false;
    set_face_buf(b);
    return true;
}

static void adopt_art(np_art_set_t *in)
{
    if (in->backdrop) {
        set_face_buf(in->backdrop);
        in->backdrop = NULL;
    } else if (s_face_buf) {
        memset(s_face_buf, 0, (size_t)FACE * FACE * 2);
        lv_obj_invalidate(s_face);
        s_ring_valid = false;
    }
    lv_obj_add_flag(s_album, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_avatar, LV_OBJ_FLAG_HIDDEN);
    s_album_dsc.data = NULL;
    s_avatar_dsc.data = NULL;
    np_art_set_free(&s_art);
    s_art = *in;
    memset(in, 0, sizeof *in);
    if (s_art.album) {
        s_album_dsc.data = s_art.album;
        lv_image_set_src(s_album, &s_album_dsc);
    }
    if (s_art.artist) {
        s_avatar_dsc.data = s_art.artist;
        lv_image_set_src(s_avatar, &s_avatar_dsc);
    }
    s_mode = MODE_UNSET;
    s_avatar_frac = -1;
}

static void ring_update(float frac, uint32_t arc)
{
    if (!s_face_buf) return;
    if (!s_ring_valid || arc != s_arc_col) {
        np_ring_render(&RING, s_face_buf, FACE, FACE, (np_rect_t){0, 0, FACE, FACE}, frac, arc, COL_TRACK);
        lv_obj_invalidate(s_face);
    } else {
        np_rect_t d;
        if (!np_ring_dirty(&RING, s_frac, frac, FACE, FACE, &d)) return;
        np_ring_render(&RING, s_face_buf, FACE, FACE, d, frac, arc, COL_TRACK);
        lv_area_t a = {.x1 = d.x + FACE_X, .y1 = d.y, .x2 = d.x + FACE_X + d.w - 1, .y2 = d.y + d.h - 1};
        lv_obj_invalidate_area(s_face, &a);
    }
    s_ring_valid = true;
    s_frac = frac;
    s_arc_col = arc;
}

static void place_avatar(float frac, double now, bool force)
{
    lv_obj_t *o = s_art.artist ? s_avatar : s_dot;
    lv_obj_t *other = s_art.artist ? s_dot : s_avatar;
    lv_obj_add_flag(other, LV_OBJ_FLAG_HIDDEN);
    if (!force && s_avatar_frac >= 0 && now - s_avatar_t < AVATAR_EVERY_S) return;
    if (!force && s_avatar_frac == frac) return;
    float x, y;
    np_ring_point(&RING, frac, &x, &y);
    int size = s_art.artist ? NP_ARTIST_PX : 16;
    lv_obj_set_pos(o, (int)lroundf(x) + FACE_X - size / 2, (int)lroundf(y) - size / 2);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    s_avatar_frac = frac;
    s_avatar_t = now;
}

static void fit_text(char *txt, const lv_font_t *font, int max_w)
{
    lv_point_t sz;
    lv_text_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x <= max_w) return;
    size_t lo = 0, hi = strlen(txt);
    char *tmp = V->fit;
    if (hi + 4 > sizeof V->fit) hi = sizeof V->fit - 4;
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

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void source_name(const char *src, char *out, size_t cap)
{
    if (strcmp(src, "music") == 0) src = "Apple Music";
    else if (strcmp(src, "plex") == 0) src = "Plex";
    np_text_fold(src, out, cap);
    for (char *p = out; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 32;
}

static void burn_in(double now)
{
    bool dim = s_mode == MODE_PLAYING && now - s_last_input >= BURN_IN_S;
    if (dim != s_dimmed) {
        s_dimmed = dim;
        lv_obj_set_style_image_opa(s_face, dim ? FACE_DIM_OPA : LV_OPA_COVER, 0);
        lv_obj_set_style_text_opa(s_title, dim ? TEXT_DIM_OPA : LV_OPA_COVER, 0);
        lv_obj_set_style_text_opa(s_sub, dim ? TEXT_DIM_OPA : LV_OPA_COVER, 0);
        lv_obj_set_style_text_opa(s_meta, dim ? TEXT_DIM_OPA : LV_OPA_COVER, 0);
        s_drift = 0;
        s_drift_t = now;
        lv_obj_align(s_album, LV_ALIGN_CENTER, 0, ALBUM_DY);
        lv_obj_align(s_album_ph, LV_ALIGN_CENTER, 0, ALBUM_DY);
    }
    if (dim && now - s_drift_t >= DRIFT_EVERY_S) {
        s_drift_t = now;
        s_drift = (s_drift + 1) % 8;
        double a = s_drift * M_PI / 4;
        int dx = (int)lround(DRIFT_PX * sin(a)), dy = (int)lround(-DRIFT_PX * cos(a));
        lv_obj_align(s_album, LV_ALIGN_CENTER, dx, ALBUM_DY + dy);
        lv_obj_align(s_album_ph, LV_ALIGN_CENTER, dx, ALBUM_DY + dy);
    }
}

void np_view_input(double now)
{
    s_last_input = now;
    if (s_shown) burn_in(now);
}

void np_view_turn(int detents, double now)
{
    if (s_shown && V) np_ctl_turn(&V->ctl, detents, now);
}

void np_view_push(double now)
{
    if (s_shown && V) np_ctl_push(&V->ctl, now);
}

void np_view_long_push(double now)
{
    if (s_shown && V) np_ctl_long_push(&V->ctl, now);
}

static const char *error_text(int status)
{
    switch (status) {
    case -1: return "Ember\noffline";
    case 401:   /* an Ember without #280: its catch-all wants the owner token */
    case 404: return "Update\nEmber";
    case 409: return "Nothing\nplaying";
    case 429: return "Slow\ndown";
    case 503: return "No\ncontrol";
    default: return "Player\nerror";
    }
}

static void controls_update(double now)
{
    np_ctl_t *c = &V->ctl;
    np_cmd_t cmds[4];
    int n = np_ctl_tick(c, now, cmds, 4);
    const np_info_t *np = &V->snap.info;
    for (int i = 0; i < n; i++) np_client_control(&cmds[i], np->source, np->track_id);

    np_ctl_result_t r;
    np_client_control_result(&r);
    if (r.seq != s_result_seq) {
        s_result_seq = r.seq;
        if (r.status) {
            s_error = r.status;
            s_error_t = now;
            np_ctl_failed(c);
        }
    }

    bool turning = np_ctl_volume_active(c, now);
    int vol = np_ctl_volume_shown(c, now);
    int arc = turning && vol >= 0 ? vol : -2;
    if (arc != s_vol_shown) {
        if (arc >= 0) lv_arc_set_value(s_vol_arc, arc);
        show(s_vol_arc, arc >= 0);
        s_vol_shown = arc;
    }

    char text[24] = "";
    const lv_font_t *font = &lv_font_montserrat_48;
    if (turning) {
        if (vol >= 0) snprintf(text, sizeof text, "%d", vol);
        else snprintf(text, sizeof text, "%+d", c->vol_gesture);
    } else if (now - c->last_t < FLASH_S && c->last != NP_CMD_VOLUME) {
        switch (c->last) {
        case NP_CMD_PLAY:
        case NP_CMD_PAUSE:
            strlcpy(text, c->last == NP_CMD_PLAY ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE, sizeof text);
            break;
        case NP_CMD_NEXT: strlcpy(text, LV_SYMBOL_NEXT, sizeof text); break;
        case NP_CMD_PREVIOUS: strlcpy(text, LV_SYMBOL_PREV, sizeof text); break;
        default: break;
        }
    } else if (now - s_error_t < ERROR_S) {
        strlcpy(text, error_text(s_error), sizeof text);
        font = &lv_font_montserrat_18;
    }
    if (strcmp(text, V->badge) != 0) {
        strlcpy(V->badge, text, sizeof V->badge);
        lv_obj_set_style_text_font(s_badge_lbl, font, 0);
        lv_label_set_text_static(s_badge_lbl, V->badge);
        show(s_badge, V->badge[0] != 0);
    }
}

void np_view_update(double now, bool offline)
{
    if (!s_shown || !V) return;
    np_art_set_t in;
    if (!s_told && now - s_show_t >= DWELL_S) {
        np_client_set_visible(true);
        s_told = true;
    }
    if (np_client_take_art(&in)) adopt_art(&in);
    bool fresh = np_client_get(&V->snap, &s_gen);
    const np_info_t *np = &V->snap.info;
    bool live = !offline && V->snap.have && np->state != NP_NONE;
    if (fresh || !live) np_ctl_server(&V->ctl, live ? np->state : NP_NONE, live ? np->volume : -1, now);
    if (live) {
        controls_update(now);
    } else if (s_vol_shown != -2 || V->badge[0]) {
        show(s_vol_arc, false);
        show(s_badge, false);
        s_vol_shown = -2;
        V->badge[0] = 0;
    }
    np_play_t shown = np_ctl_play_shown(&V->ctl, now);
    int mode = !live ? MODE_IDLE : shown == NP_PLAYING ? MODE_PLAYING : MODE_PAUSED;

    if (mode == MODE_IDLE) {
        const char *line = offline ? "Ember offline" : V->snap.have ? "Nothing playing" : "Waiting for Ember";
        if (s_mode != MODE_IDLE) {
            free_pictures();
            show(s_album_ph, false);
            show(s_dot, false);
            show(s_title, false);
            show(s_sub, false);
            show(s_meta, false);
            show(s_idle, true);
            s_mode = MODE_IDLE;
        }
        text_set(s_idle, V->idle, sizeof V->idle, line);
        if (now - s_idle_drift_t >= DRIFT_EVERY_S) {
            s_idle_drift_t = now;
            s_idle_drift = (s_idle_drift + 1) % 8;
            double a = s_idle_drift * M_PI / 4;
            lv_obj_align(s_idle, LV_ALIGN_CENTER, (int)lround(DRIFT_PX * sin(a)), (int)lround(-DRIFT_PX * cos(a)));
        }
        burn_in(now);
        return;
    }
    if (!ensure_face()) return;
    bool relayout = s_mode != mode;
    if (relayout) {
        show(s_idle, false);
        show(s_face, true);
        show(s_album, s_art.album != NULL);
        show(s_album_ph, s_art.album == NULL);
        show(s_title, true);
        show(s_sub, true);
        show(s_meta, true);
        s_mode = mode;
        s_meta_sec = -1;
    }

    if (fresh || relayout) {
        char *w = V->work;
        np_text_fold(np->title[0] ? np->title : "Unknown track", w, sizeof V->title);
        fit_text(w, &lv_font_montserrat_24, TITLE_W);
        text_set(s_title, V->title, sizeof V->title, w);
        np_text_fold(np->artist, w, sizeof V->work);
        if (np->album[0]) {
            if (w[0]) strlcat(w, " - ", sizeof V->work);
            size_t n = strlen(w);
            np_text_fold(np->album, w + n, sizeof V->work - n);
        }
        fit_text(w, &lv_font_montserrat_18, SUB_W);
        text_set(s_sub, V->sub, sizeof V->sub, w);
    }

    const np_anchor_t *an = &V->snap.anchor;
    long long pos = np_anchor_at(an, mode == MODE_PAUSED && an->playing && V->ctl.play_set ? V->ctl.play_t : now);
    if (pos / 1000 != s_meta_sec || relayout) {
        s_meta_sec = pos / 1000;
        char src[24], p[16], d[16], meta[64];
        source_name(np->source, src, sizeof src);
        np_format_time(pos, p, sizeof p);
        np_format_time(an->dur_ms, d, sizeof d);
        if (mode == MODE_PAUSED) snprintf(meta, sizeof meta, "PAUSED  %s / %s", p, d);
        else if (an->dur_ms > 0) snprintf(meta, sizeof meta, "%s  %s / %s", src, p, d);
        else snprintf(meta, sizeof meta, "%s  %s", src, p);
        text_set(s_meta, V->meta, sizeof V->meta, meta);
    }

    float frac = an->dur_ms > 0 ? (float)((double)(s_meta_sec * 1000) / (double)an->dur_ms) : 0;
    if (frac > 1) frac = 1;
    ring_update(frac, mode == MODE_PLAYING ? COL_ARC : COL_ARC_PAUSED);
    if (relayout) lv_obj_set_style_bg_color(s_dot, lv_color_hex(mode == MODE_PLAYING ? COL_ARC : COL_ARC_PAUSED), 0);
    place_avatar(frac, now, relayout);
    burn_in(now);
}
