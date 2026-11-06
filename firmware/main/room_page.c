#include "page_ops.h"
#include "room_page.h"
#include "sdkconfig.h"

#if CONFIG_CINDER_ROOM

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "dim_ctl.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "j2.h"
#include "lvgl.h"
#include "page_canvas.h"
#include "page_frame.h"
#include "room_face.h"
#include "wx_paint.h"
#include "wx_shift.h"
#include "wx_temp.h"

LV_FONT_DECLARE(font_digits_96);
LV_FONT_DECLARE(font_face_36);

static const char *TAG = "roompage";

_Static_assert(WX_FACE * WX_FACE * 2 <= PAGE_CANVAS_BYTES, "the background paints on the page canvas");

#define FACE_X 3
#define VALUE_BASE 250
#define LINE2_BASE 298
#define CLIMATE_LINE2_BASE 306
#define LINE3_BASE 342
#define END_BASE 412
#define END_DX 73
#define VALUE_SPACE (-2)
#define ARRIVE_FPS 30.0
#define SUB_FADE_S 0.48
#define BAKE_YIELD_US 25000
#define BAKE_STACK 4096
#define RETRY_S 5.0

typedef struct {
    char value[16], line2[24], line3[24];
    uint32_t value_rgb, line2_rgb, line3_rgb;
    int marker_x, marker_y, sub_opa, line2_y, face, ends;
} shown_t;

static lv_obj_t *s_parent, *s_root, *s_bg, *s_marker, *s_value, *s_line2, *s_line3, *s_lo, *s_hi;
static TaskHandle_t s_task;
static atomic_bool s_baking;
static EXT_RAM_BSS_ATTR wx_ring_t *s_ring;
static EXT_RAM_BSS_ATTR uint16_t *s_lut, *s_bgbuf;
static EXT_RAM_BSS_ATTR uint8_t *s_marker_buf;
static EXT_RAM_BSS_ATTR int s_req_face, s_ring_face, s_bg_face;
static EXT_RAM_BSS_ATTR bool s_req_grey, s_ring_grey, s_bg_grey, s_open, s_short;
static EXT_RAM_BSS_ATTR unsigned s_bg_mask, s_mask;
static EXT_RAM_BSS_ATTR int64_t s_yield_at;
static EXT_RAM_BSS_ATTR room_faces_t s_faces;
static EXT_RAM_BSS_ATTR double s_t, s_retry_t;
static EXT_RAM_BSS_ATTR wx_shift_t s_shift;
static EXT_RAM_BSS_ATTR shown_t s_shown;
static uint8_t s_none[2 * 2 * 3];

static int top_for(const lv_font_t *f, int base) { return base - (f->line_height - f->base_line); }

static void show_obj(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *canvas(lv_obj_t *parent, lv_color_format_t cf)
{
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(c, s_none, 2, 2, cf);
    return c;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int dx, int base)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(l, LV_ALIGN_TOP_MID, dx, top_for(font, base));
    lv_label_set_text_static(l, "");
    return l;
}

static void bake_yield(void)
{
    if (esp_timer_get_time() - s_yield_at < BAKE_YIELD_US) return;
    vTaskDelay(1);
    s_yield_at = esp_timer_get_time();
}

static void bake_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!atomic_load(&s_baking)) continue;
        int64_t t0 = esp_timer_get_time();
        s_yield_at = t0;
        room_ring_lut(s_req_face, s_req_grey, s_lut);
        s_ring->yield = bake_yield;
        wx_paint_ring_lut(s_ring, s_lut);
        ESP_LOGI(TAG, "ring face %d grey %d in %d ms", s_req_face, (int)s_req_grey, (int)((esp_timer_get_time() - t0) / 1000));
        atomic_store(&s_baking, false);
    }
}

void room_page_init(lv_obj_t *parent) { s_parent = parent; }

static bool create(void)
{
    if (s_root) return true;
    if (!s_parent) return false;
    if (xTaskCreatePinnedToCoreWithCaps(bake_task, "roombake", BAKE_STACK, NULL, 2, &s_task, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "bake task not created: the room page stays black");
        return false;
    }
    s_ring_face = s_bg_face = -1;
    s_root = lv_obj_create(s_parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, WX_FACE, WX_FACE);
    lv_obj_set_pos(s_root, FACE_X, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_bg = canvas(s_root, LV_COLOR_FORMAT_RGB565);
    s_value = label(s_root, &font_digits_96, 0, VALUE_BASE);
    lv_obj_set_style_text_letter_space(s_value, VALUE_SPACE, 0);
    s_line2 = label(s_root, &lv_font_montserrat_30, 0, LINE2_BASE);
    s_line3 = label(s_root, &lv_font_montserrat_24, 0, LINE3_BASE);
    s_lo = label(s_root, &font_face_36, -END_DX, END_BASE);
    s_hi = label(s_root, &font_face_36, END_DX, END_BASE);
    lv_label_set_text_static(s_lo, "16");
    lv_label_set_text_static(s_hi, "28");
    lv_obj_set_style_text_color(s_lo, lv_color_hex(wx_temp_rgb(ROOM_TEMP_LO)), 0);
    lv_obj_set_style_text_color(s_hi, lv_color_hex(wx_temp_rgb(ROOM_TEMP_HI)), 0);
    s_marker = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
    return true;
}

static void release(void)
{
    lv_canvas_set_buffer(s_bg, s_none, 2, 2, LV_COLOR_FORMAT_RGB565);
    lv_canvas_set_buffer(s_marker, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    if (s_bgbuf) page_canvas_give(PAGE_CANVAS_ROOM);
    s_bgbuf = NULL;
    s_bg_face = -1;
    heap_caps_free(s_marker_buf);
    s_marker_buf = NULL;
    if (atomic_load(&s_baking)) return;
    heap_caps_free(s_ring);
    heap_caps_free(s_lut);
    s_ring = NULL;
    s_lut = NULL;
    s_ring_face = -1;
}

static bool acquire(double t)
{
    if (s_bgbuf && s_ring && s_lut && s_marker_buf) return true;
    if (s_short && t < s_retry_t) return false;
    if (!s_ring) s_ring = heap_caps_malloc(sizeof *s_ring, MALLOC_CAP_SPIRAM);
    if (!s_lut) s_lut = heap_caps_malloc(WX_RING_LUT * sizeof *s_lut, MALLOC_CAP_SPIRAM);
    if (!s_marker_buf) s_marker_buf = heap_caps_malloc(WX_MARKER * WX_MARKER * 3, MALLOC_CAP_SPIRAM);
    if (!s_bgbuf) s_bgbuf = page_canvas_take(PAGE_CANVAS_ROOM);
    bool mem = s_ring && s_lut && s_marker_buf;
    if (!mem && !s_short)
        ESP_LOGE(TAG, "no PSRAM for the face (%u KB free, largest %u KB): black, retrying",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
    s_short = !mem;
    if (!mem) s_retry_t = t + RETRY_S;
    if (!mem || !s_bgbuf) return false;
    wx_img_t mk = {.px = (uint16_t *)s_marker_buf, .a = s_marker_buf + WX_MARKER * WX_MARKER * 2, .w = WX_MARKER, .h = WX_MARKER};
    wx_paint_marker(&mk);
    lv_canvas_set_buffer(s_marker, s_marker_buf, WX_MARKER, WX_MARKER, LV_COLOR_FORMAT_RGB565A8);
    lv_canvas_set_buffer(s_bg, s_bgbuf, WX_FACE, WX_FACE, LV_COLOR_FORMAT_RGB565);
    s_bg_face = -1;
    return true;
}

static void reset_shown(void)
{
    memset(&s_shown, 0xFF, sizeof s_shown);
    s_shown.value[0] = s_shown.line2[0] = s_shown.line3[0] = 0;
    s_t = 0;
}

static void shift_root(void)
{
    int dx, dy;
    wx_shift_xy(&s_shift, &WX_SHIFT_SQUARE, &dx, &dy);
    lv_obj_set_style_translate_x(s_root, dx, 0);
    lv_obj_set_style_translate_y(s_root, dy, 0);
}

static void paint_bg(int face, unsigned mask)
{
    wx_img_t bg = {.px = s_bgbuf, .w = WX_FACE, .h = WX_FACE};
    wx_paint_clear(&bg);
    wx_ring_blit(s_ring, s_bgbuf);
    wx_paint_face_dots(&bg, room_face_pos(mask, face), room_faces_n(mask));
    lv_obj_invalidate(s_bg);
}

static bool ready(int face, bool grey, unsigned mask)
{
    if (face != s_bg_face && s_bg_face >= 0) {
        show_obj(s_root, false);
        s_bg_face = -1;
    }
    if (atomic_load(&s_baking)) return s_bg_face == face;
    if (face != s_ring_face || grey != s_ring_grey) {
        s_ring_face = s_req_face = face;
        s_ring_grey = s_req_grey = grey;
        atomic_store(&s_baking, true);
        xTaskNotifyGive(s_task);
        return s_bg_face == face;
    }
    if (face != s_bg_face || mask != s_bg_mask) {
        if (face != s_bg_face) reset_shown();
        paint_bg(face, mask);
    } else if (grey != s_bg_grey) {
        wx_ring_blit(s_ring, s_bgbuf);
        lv_obj_invalidate(s_bg);
    }
    s_bg_face = face;
    s_bg_grey = grey;
    s_bg_mask = mask;
    return true;
}

static void set_text(lv_obj_t *l, char *shown, size_t cap, const char *txt)
{
    if (strcmp(shown, txt) == 0) return;
    strlcpy(shown, txt, cap);
    lv_label_set_text_static(l, shown);
}

static void set_rgb(lv_obj_t *l, uint32_t *shown, uint32_t rgb)
{
    if (*shown == rgb) return;
    *shown = rgb;
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
}

static void set_marker(bool on, float frac)
{
    show_obj(s_marker, on);
    if (!on) return;
    float x, y;
    wx_gauge_xy(frac, &x, &y);
    int mx = (int)lroundf(x) - WX_MARKER / 2, my = (int)lroundf(y) - WX_MARKER / 2;
    if (mx == s_shown.marker_x && my == s_shown.marker_y) return;
    s_shown.marker_x = mx;
    s_shown.marker_y = my;
    lv_obj_set_pos(s_marker, mx, my);
}

static void paint(const room_view_t *v)
{
    double ta = floor(s_t * ARRIVE_FPS) / ARRIVE_FPS;
    int cv = v->have ? wx_count_value(v->value, wx_count_step(wx_count_n(v->value), ta)) : 0;
    char txt[16];
    room_value_text(v, cv, txt, sizeof txt);
    if (v->face != s_shown.face) {
        s_shown.face = v->face;
        int deg = v->face == ROOM_FACE_CLIMATE ? lv_font_get_glyph_width(&font_digits_96, 0x00B0, 0) + VALUE_SPACE : 0;
        lv_obj_align(s_value, LV_ALIGN_TOP_MID, deg / 2, top_for(&font_digits_96, VALUE_BASE));
    }
    set_text(s_value, s_shown.value, sizeof s_shown.value, txt);
    set_rgb(s_value, &s_shown.value_rgb, room_value_rgb(v, cv));
    set_marker(v->have, v->frac * (float)wx_arrive(ta));
    int ends = v->face == ROOM_FACE_CLIMATE && v->have;
    if (ends != s_shown.ends) {
        s_shown.ends = ends;
        show_obj(s_lo, ends);
        show_obj(s_hi, ends);
    }
    int y2 = top_for(&lv_font_montserrat_30, v->face == ROOM_FACE_CLIMATE ? CLIMATE_LINE2_BASE : LINE2_BASE);
    if (y2 != s_shown.line2_y) {
        s_shown.line2_y = y2;
        lv_obj_align(s_line2, LV_ALIGN_TOP_MID, 0, y2);
    }
    set_text(s_line2, s_shown.line2, sizeof s_shown.line2, v->line2);
    set_rgb(s_line2, &s_shown.line2_rgb, v->line2_rgb);
    set_text(s_line3, s_shown.line3, sizeof s_shown.line3, v->line3);
    set_rgb(s_line3, &s_shown.line3_rgb, v->line3_rgb);
    double sf = (s_t - WX_ARRIVE_S) / SUB_FADE_S;
    int opa = sf <= 0 ? 0 : sf >= 1 ? 255 : 85 * (1 + (int)(sf * 3));
    if (opa != s_shown.sub_opa) {
        s_shown.sub_opa = opa;
        lv_obj_set_style_text_opa(s_line2, (lv_opa_t)opa, 0);
        lv_obj_set_style_text_opa(s_line3, (lv_opa_t)opa, 0);
    }
}

void page_room_frame(const page_frame_t *f)
{
    if (!s_open) return;
    room_t r;
    j2_room_get(&r);
    int64_t now = esp_timer_get_time() / 1000;
    unsigned mask = s_mask = room_faces_mask(r.has);
    int face = room_faces_resolve(&s_faces, mask, &r, now);
    if (face < 0 || !acquire(f->t)) {
        show_obj(s_root, false);
        s_bg_face = -1;
        return;
    }
    room_view_t v;
    room_view(&r, now, face, dim_ctl_panel_level(), &v);
    if (!ready(face, !v.have, mask)) return;
    double dt = isfinite(f->dt) && f->dt > 0 ? f->dt : 0;
    if (wx_shift_tick(&s_shift, &WX_SHIFT_SQUARE, dt)) shift_root();
    paint(&v);
    s_t += dt;
    show_obj(s_root, true);
}

void page_room_show(bool on)
{
    if (on == s_open) return;
    if (on && !create()) return;
    s_open = on;
    if (!on) {
        show_obj(s_root, false);
        release();
        return;
    }
    room_faces_open(&s_faces);
    s_short = false;
    reset_shown();
    shift_root();
}

void page_room_input(const page_input_t *in, double t)
{
    (void)t;
    if (s_open && in->detents) room_faces_turn(&s_faces, s_mask, in->detents);
}

bool page_room_visible(void) { return room_faces_mask(j2_room_found()) != 0; }

#else

void room_page_init(lv_obj_t *parent) { (void)parent; }

void page_room_show(bool on) { (void)on; }

void page_room_input(const page_input_t *in, double t)
{
    (void)in;
    (void)t;
}

void page_room_frame(const page_frame_t *f) { (void)f; }

bool page_room_visible(void) { return false; }

#endif
