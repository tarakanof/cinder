#include "music_view.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_timer.h"

enum { MU_IDLE, MU_PRE, MU_PLAY };

static EXT_RAM_BSS_ATTR lv_obj_t *s_parent;
static EXT_RAM_BSS_ATTR uint8_t s_arena[MM_ARENA];
static EXT_RAM_BSS_ATTR lv_image_dsc_t s_band, s_cup[2], s_note;
static EXT_RAM_BSS_ATTR mm_frame_t s_shown, s_cur;
static EXT_RAM_BSS_ATTR bool s_any, s_held;
static EXT_RAM_BSS_ATTR int s_state, s_j, s_cur_j, s_settle_at;
static EXT_RAM_BSS_ATTR float s_settle_from;
static EXT_RAM_BSS_ATTR atomic_int s_count, s_paint_ms;
static EXT_RAM_BSS_ATTR double s_t0;

static lv_image_dsc_t dsc(lv_color_format_t cf, int w, int h, const uint8_t *data, int bpp)
{
    return (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = cf, .w = w, .h = h, .stride = w * (cf == LV_COLOR_FORMAT_A8 ? 1 : 2)},
        .data_size = (uint32_t)(w * h * bpp),
        .data = data,
    };
}

void music_view_create(lv_obj_t *parent)
{
    s_parent = parent;
    s_cur_j = -1;
    s_settle_at = -1;
    int64_t t0 = esp_timer_get_time();
    mm_paint(s_arena);
    atomic_store(&s_paint_ms, (int)((esp_timer_get_time() - t0 + 999) / 1000));
    s_band = dsc(LV_COLOR_FORMAT_A8, MM_BAND_W, MM_BAND_H, s_arena + MM_BAND_AT, 1);
    for (int i = 0; i < 2; i++) s_cup[i] = dsc(LV_COLOR_FORMAT_RGB565A8, MM_CUP_W, MM_CUP_H, s_arena + MM_CUP_AT(i), 3);
    s_note = dsc(LV_COLOR_FORMAT_A8, MM_NOTE_W, MM_NOTE_H, s_arena + MM_NOTE_AT, 1);
}

static void image(lv_layer_t *layer, const lv_image_dsc_t *src, int x, int y, int opa, bool tint)
{
    if (opa <= 0) return;
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = src;
    d.opa = (lv_opa_t)opa;
    if (tint) d.recolor = lv_color_hex(MM_RGB);
    lv_area_t a = {x, y, x + (int)src->header.w - 1, y + (int)src->header.h - 1};
    lv_draw_image(layer, &d, &a);
}

static void origin(int *ox, int *oy)
{
    lv_area_t c;
    lv_obj_get_coords(s_parent, &c);
    *ox = c.x1;
    *oy = c.y1;
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const mm_frame_t *f = &s_shown;
    int ox, oy;
    origin(&ox, &oy);
    image(layer, &s_band, ox + MM_BAND_X, oy + MM_BAND_Y + f->phones_dy, f->phones_opa, true);
    int cy = oy + MM_CUP_Y + f->phones_dy + f->cup_dy;
    image(layer, &s_cup[0], ox + MM_CUP_L_X, cy, f->phones_opa, false);
    image(layer, &s_cup[1], ox + MM_CUP_R_X, cy, f->phones_opa, false);
    for (int i = 0; i < MM_NOTES; i++) image(layer, &s_note, ox + f->note[i].x, oy + f->note[i].y, f->note[i].opa, true);
}

bool music_view_busy(void) { return s_state != MU_IDLE; }

bool music_view_start(double t)
{
    if (s_held || s_state != MU_IDLE) return false;
    s_state = MU_PRE;
    s_t0 = t + MM_PRE_S;
    return true;
}

static void invalidate(const mm_box_t *b)
{
    int ox, oy;
    origin(&ox, &oy);
    lv_area_t a = {ox + b->x0, oy + b->y0, ox + b->x1, oy + b->y1};
    lv_obj_invalidate_area(s_parent, &a);
}

static void end(void)
{
    static const mm_frame_t none;
    mm_box_t b;
    if (s_state == MU_PLAY) {
        lv_obj_remove_event_cb(s_parent, draw_cb);
        if (s_any && mm_diff(&s_shown, &none, &b)) invalidate(&b);
    }
    s_state = MU_IDLE;
    s_any = false;
    s_cur_j = -1;
    s_settle_at = -1;
    memset(&s_shown, 0, sizeof s_shown);
}

double music_view_poll(double t)
{
    if (s_state == MU_PRE && t >= s_t0) {
        lv_obj_add_event_cb(s_parent, draw_cb, LV_EVENT_DRAW_POST, NULL);
        s_state = MU_PLAY;
        s_any = false;
        s_j = -1;
        s_settle_at = -1;
        atomic_store(&s_count, atomic_load(&s_count) + 1);
    }
    return s_state == MU_PLAY ? t - s_t0 : -1;
}

static int tick(double t) { return (int)floor((t - s_t0) * MM_FPS); }

static const mm_frame_t *at(int j)
{
    if (j != s_cur_j) {
        mm_frame(j, &s_cur);
        s_cur_j = j;
    }
    return &s_cur;
}

float music_view_eye_px(double t)
{
    if (s_state != MU_PLAY) return 0;
    int j = tick(t);
    if (s_settle_at >= 0 && j >= s_settle_at) return mm_eye_settle(s_settle_from, j - s_settle_at);
    return at(j)->eye_px;
}

void music_view_frame(double t)
{
    if (s_state != MU_PLAY) return;
    int j = tick(t);
    if (j >= MM_TICKS) {
        end();
        return;
    }
    if (j == s_j || j < 0) return;
    const mm_frame_t *f = at(j);
    mm_box_t b;
    if (mm_diff(s_any ? &s_shown : NULL, f, &b)) invalidate(&b);
    s_shown = *f;
    s_any = true;
    s_j = j;
}

void music_view_abort(double t)
{
    if (s_state == MU_PRE) {
        s_state = MU_IDLE;
        return;
    }
    if (s_state != MU_PLAY) return;
    int k = mm_abort_tick(s_j);
    if (k == s_j) return;
    if (s_j >= 0 && s_settle_at < 0) {
        s_settle_from = at(s_j)->eye_px;
        s_settle_at = k;
    }
    s_t0 = t - (k + 0.5) / MM_FPS;
}

void music_view_stop(void)
{
    if (s_state != MU_IDLE) end();
}

void music_view_hold(bool hold)
{
    s_held = hold;
    if (hold) music_view_stop();
}

void music_view_stats(int *shown, int *paint_ms)
{
    *shown = atomic_load(&s_count);
    *paint_ms = atomic_load(&s_paint_ms);
}
