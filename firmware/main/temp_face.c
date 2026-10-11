#include "temp_face.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wx_faces.h"
#include "wx_job.h"
#include "wx_paint.h"
#include "wx_temp.h"

LV_FONT_DECLARE(font_digits_96);
LV_FONT_DECLARE(font_face_36);

static const char *TAG = "tempface";

#define FACE_X 3
#define VALUE_Y 200
#define SUB_Y 302
#define END_X 96
#define END_Y 362
#define VALUE_SPACE (-2)
#define ARRIVE_FPS 30.0
#define FADE_FPS 15.0
#define SUB_FADE_S 0.48
#define DRIFT_PERIOD_S 8.0
#define GREY_RGB 0x5A5A5A
#define UNIT_GAP 6
#define SNOW_RGB 0xD0DCE8
#define BOLT_RGB 0xF0D040
#define LABEL_GREY_RGB 0x8A8A8A
#define SUB_RGB 0xBDBDBD

#define DROP(p) do { heap_caps_free(p); (p) = NULL; } while (0)

static lv_obj_t *s_root, *s_bg, *s_hero, *s_still, *s_drift, *s_thermo, *s_marker, *s_value, *s_unit, *s_lo, *s_hi, *s_sub;
static lv_obj_t *s_sheet[WX_SHEETS], *s_strip[WX_SHEETS][2], *s_top[WX_SHEETS], *s_bottom[WX_SHEETS], *s_cover[WX_SHEETS];
static lv_obj_t *s_drip[WX_DRIPS], *s_splash[WX_SPLASHES], *s_bolt;
static EXT_RAM_BSS_ATTR wx_rain_layout_t s_lay;
static EXT_RAM_BSS_ATTR bool s_rain;
static EXT_RAM_BSS_ATTR wx_bake_buf_t s_buf;
static uint8_t *s_thermo_buf, *s_marker_buf;
static EXT_RAM_BSS_ATTR uint8_t *s_hero_buf[2];
static EXT_RAM_BSS_ATTR int s_front;
static uint8_t s_none[2 * 2 * 3];
static EXT_RAM_BSS_ATTR wx_img_t s_thermo_img;
static EXT_RAM_BSS_ATTR wx_job_io_t s_io;
static EXT_RAM_BSS_ATTR wx_bake_out_t s_out;
static EXT_RAM_BSS_ATTR wx_shift_t s_shift;
static wx_job_t s_job;
static TaskHandle_t s_task;
static EXT_RAM_BSS_ATTR double s_t;
static EXT_RAM_BSS_ATTR int s_tick;
static EXT_RAM_BSS_ATTR wx_faces_t s_faces;
static EXT_RAM_BSS_ATTR int s_face;

typedef struct {
    char value[20], sub[24];
    uint32_t value_rgb, sub_rgb;
    int marker_x, marker_y, merc_q, sprite_opa, sub_opa, face_opa, drift, rain_opa, value_w;
    uint32_t merc_rgb, unit_rgb;
    wx_rain_frame_t rain;
} shown_t;

static EXT_RAM_BSS_ATTR shown_t s_shown;
static EXT_RAM_BSS_ATTR char s_lo_txt[20], s_hi_txt[20];

static void *psram(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }

static lv_obj_t *canvas(lv_obj_t *parent, lv_color_format_t cf)
{
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(c, s_none, 2, 2, cf);
    return c;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(l, align, x, y);
    lv_label_set_text_static(l, "");
    return l;
}

static void show_obj(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *a8(lv_obj_t *parent, uint32_t rgb)
{
    lv_obj_t *c = canvas(parent, LV_COLOR_FORMAT_A8);
    lv_obj_set_style_image_recolor(c, lv_color_hex(rgb), 0);
    lv_obj_set_style_image_recolor_opa(c, LV_OPA_COVER, 0);
    return c;
}

static void rain_create(void)
{
    wx_rain_layout(&s_lay);
    for (int s = 0; s < WX_SHEETS; s++) {
        wx_box_t b = wx_sheet(s)->box;
        lv_obj_t *o = s_sheet[s] = lv_obj_create(s_root);
        lv_obj_remove_style_all(o);
        lv_obj_set_pos(o, b.x0, b.y0);
        lv_obj_set_size(o, b.x1 - b.x0, b.y1 - b.y0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        s_strip[s][0] = a8(o, 0);
        s_strip[s][1] = a8(o, 0);
        if (s < WX_DRIPS) {
            s_drip[s] = a8(o, 0);
            lv_obj_set_x(s_drip[s], wx_drip_x(s) - b.x0);
        } else {
            for (int n = 0; n < WX_SPLASHES; n++) {
                wx_splash_at_t at;
                wx_splash_at(n, &at);
                s_splash[n] = a8(o, 0);
                lv_obj_set_x(s_splash[n], at.x - b.x0);
            }
        }
        s_top[s] = a8(o, 0);
        s_bottom[s] = a8(o, 0);
        lv_obj_set_y(s_bottom[s], b.y1 - b.y0 - s_lay.ramp_h[s]);
        if (s == WX_SHEETS - 1) {
            s_bolt = a8(o, BOLT_RGB);
            lv_obj_set_pos(s_bolt, WX_BOLT_X - b.x0, WX_BOLT_Y - b.y0);
        }
        s_cover[s] = canvas(o, LV_COLOR_FORMAT_RGB565A8);
        lv_obj_set_pos(s_cover[s], s_lay.cover_box[s].x0 - b.x0, s_lay.cover_box[s].y0 - b.y0);
    }
}

static void bake_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const wx_temp_t *t = wx_job_take(&s_job);
        if (!t) continue;
        int64_t t0 = esp_timer_get_time();
        wx_bake(t, s_io.kind, &s_buf, &s_io.out);
        wx_job_done(&s_job, (int)((esp_timer_get_time() - t0) / 1000));
    }
}

void temp_face_create(lv_obj_t *parent)
{
    if (s_root) return;
    wx_job_init(&s_job, &s_io);
    if (xTaskCreatePinnedToCoreWithCaps(bake_task, "wxbake", 4096, NULL, 2, &s_task, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "bake task not created: the weather page stays black");
        return;
    }
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, WX_FACE, WX_FACE);
    lv_obj_set_pos(s_root, FACE_X, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_bg = canvas(s_root, LV_COLOR_FORMAT_RGB565);
    s_hero = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
    s_still = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
    s_drift = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
    rain_create();
    s_thermo = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
    lv_obj_set_pos(s_thermo, WX_THERMO_X, WX_THERMO_Y);
    lv_obj_add_flag(s_hero, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_still, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_drift, LV_OBJ_FLAG_HIDDEN);

    int deg = lv_font_get_glyph_width(&font_digits_96, 0x00B0, 0) + VALUE_SPACE;
    s_value = label(s_root, &font_digits_96, LV_ALIGN_TOP_MID, deg / 2, VALUE_Y);
    lv_obj_set_style_text_letter_space(s_value, VALUE_SPACE, 0);
    s_lo = label(s_root, &font_face_36, LV_ALIGN_TOP_LEFT, END_X, END_Y);
    s_hi = label(s_root, &font_face_36, LV_ALIGN_TOP_RIGHT, -END_X, END_Y);
    s_sub = label(s_root, &lv_font_montserrat_30, LV_ALIGN_TOP_MID, 0, SUB_Y);
    s_unit = label(s_root, &lv_font_montserrat_30, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text_static(s_unit, "%");
    lv_obj_add_flag(s_unit, LV_OBJ_FLAG_HIDDEN);
    s_marker = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
}

static void detach(void)
{
    show_obj(s_root, false);
    lv_canvas_set_buffer(s_bg, s_none, 2, 2, LV_COLOR_FORMAT_RGB565);
    lv_canvas_set_buffer(s_hero, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    lv_canvas_set_buffer(s_still, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    lv_canvas_set_buffer(s_drift, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    for (int s = 0; s < WX_SHEETS; s++) {
        lv_canvas_set_buffer(s_strip[s][0], s_none, 2, 2, LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_strip[s][1], s_none, 2, 2, LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_top[s], s_none, 2, 2, LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_bottom[s], s_none, 2, 2, LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_cover[s], s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    }
    for (int i = 0; i < WX_DRIPS; i++) lv_canvas_set_buffer(s_drip[i], s_none, 2, 2, LV_COLOR_FORMAT_A8);
    for (int n = 0; n < WX_SPLASHES; n++) lv_canvas_set_buffer(s_splash[n], s_none, 2, 2, LV_COLOR_FORMAT_A8);
    lv_canvas_set_buffer(s_bolt, s_none, 2, 2, LV_COLOR_FORMAT_A8);
}

static void release(void)
{
    detach();
    lv_canvas_set_buffer(s_thermo, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    lv_canvas_set_buffer(s_marker, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    DROP(s_buf.bg);
    DROP(s_buf.lut);
    DROP(s_buf.ring);
    DROP(s_hero_buf[0]);
    DROP(s_hero_buf[1]);
    s_buf.hero = NULL;
    DROP(s_buf.still);
    DROP(s_buf.drift);
    DROP(s_thermo_buf);
    DROP(s_marker_buf);
    wx_job_released(&s_job);
}

static bool acquire(void)
{
    s_buf.bg = psram((size_t)WX_FACE * WX_FACE * 2);
    s_buf.lut = psram(WX_RING_LUT * sizeof *s_buf.lut);
    s_buf.ring = psram(sizeof *s_buf.ring);
    s_hero_buf[0] = psram(WX_HERO_BOX * WX_HERO_BOX * 3);
    s_hero_buf[1] = psram(WX_HERO_BOX * WX_HERO_BOX * 3);
    s_buf.still = psram((size_t)wx_clouds_max_px(false) * 3);
    s_buf.drift = psram((size_t)wx_clouds_max_px(true) * 3);
    s_thermo_buf = psram(WX_THERMO_W * WX_THERMO_H * 3);
    s_marker_buf = psram(WX_MARKER * WX_MARKER * 3);
    if (!s_buf.bg || !s_buf.lut || !s_buf.ring || !s_hero_buf[0] || !s_hero_buf[1] || !s_buf.still || !s_buf.drift || !s_thermo_buf || !s_marker_buf) {
        ESP_LOGE(TAG, "no PSRAM for the face (%u KB free, largest %u KB): black, retrying in 5 s",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                 (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
        release();
        return false;
    }
    s_front = 0;
    s_buf.hero = s_hero_buf[1];
    s_thermo_img = (wx_img_t){.px = (uint16_t *)s_thermo_buf, .a = s_thermo_buf + WX_THERMO_W * WX_THERMO_H * 2,
                              .w = WX_THERMO_W, .h = WX_THERMO_H};
    wx_img_t mk = {.px = (uint16_t *)s_marker_buf, .a = s_marker_buf + WX_MARKER * WX_MARKER * 2, .w = WX_MARKER,
                   .h = WX_MARKER};
    wx_paint_marker(&mk);
    lv_canvas_set_buffer(s_thermo, s_thermo_buf, WX_THERMO_W, WX_THERMO_H, LV_COLOR_FORMAT_RGB565A8);
    lv_canvas_set_buffer(s_marker, s_marker_buf, WX_MARKER, WX_MARKER, LV_COLOR_FORMAT_RGB565A8);
    memset(&s_shown, 0xFF, sizeof s_shown);
    s_shown.value[0] = s_shown.sub[0] = 0;
    return true;
}

static void deg_text(char *buf, size_t n, int v)
{
    if (v < 0) snprintf(buf, n, "\xE2\x88\x92%d\xC2\xB0", -v);
    else snprintf(buf, n, "%d\xC2\xB0", v);
}

static void end_label(lv_obj_t *l, char *buf, size_t n, bool on, int v)
{
    show_obj(l, on);
    if (!on) return;
    deg_text(buf, n, v);
    lv_label_set_text_static(l, buf);
    lv_obj_set_style_text_color(l, lv_color_hex(wx_temp_rgb((float)v)), 0);
}

static void pct_label(lv_obj_t *l, char *buf, size_t n, bool on, int v, uint32_t rgb)
{
    show_obj(l, on);
    if (!on) return;
    snprintf(buf, n, "%d%%", v);
    lv_label_set_text_static(l, buf);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
}

static void sprite(lv_obj_t *c, uint8_t *buf, wx_box_t b)
{
    int w = b.x1 - b.x0, h = b.y1 - b.y0;
    show_obj(c, w > 0 && h > 0);
    if (w <= 0 || h <= 0) return;
    lv_canvas_set_buffer(c, buf, w, h, LV_COLOR_FORMAT_RGB565A8);
    lv_obj_set_pos(c, b.x0, b.y0);
    lv_obj_invalidate(c);
}

static void rain_attach(const wx_rain_t *r)
{
    uint8_t *m = s_buf.still;
    uint32_t col = r->kind == WX_PRECIP_SNOW ? SNOW_RGB : wx_rain_rgb(r->intensity);
    for (int s = 0; s < WX_SHEETS; s++) {
        wx_box_t b = wx_sheet(s)->box, c = s_lay.cover_box[s];
        int w = b.x1 - b.x0;
        for (int k = 0; k < 2; k++) {
            lv_canvas_set_buffer(s_strip[s][k], m + s_lay.tile[s], w, WX_SHEET_PERIOD, LV_COLOR_FORMAT_A8);
            lv_obj_set_style_image_recolor(s_strip[s][k], lv_color_hex(col), 0);
        }
        lv_canvas_set_buffer(s_top[s], m + s_lay.top[s], w, s_lay.ramp_h[s], LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_bottom[s], m + s_lay.bottom[s], w, s_lay.ramp_h[s], LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_cover[s], m + s_lay.cover[s], c.x1 - c.x0, c.y1 - c.y0, LV_COLOR_FORMAT_RGB565A8);
        lv_obj_invalidate(s_sheet[s]);
    }
    for (int i = 0; i < WX_DRIPS; i++) {
        lv_obj_set_style_image_recolor(s_drip[i], lv_color_hex(col), 0);
        show_obj(s_drip[i], false);
    }
    for (int n = 0; n < WX_SPLASHES; n++) {
        lv_canvas_set_buffer(s_splash[n], m + s_lay.splash[n], WX_SPLASH_W, WX_SPLASH_H, LV_COLOR_FORMAT_A8);
        lv_obj_set_style_image_recolor(s_splash[n], lv_color_hex(col), 0);
        show_obj(s_splash[n], false);
    }
    lv_canvas_set_buffer(s_bolt, m + s_lay.bolt, WX_BOLT_W, WX_BOLT_H, LV_COLOR_FORMAT_A8);
    show_obj(s_bolt, false);
    memset(&s_shown.rain, 0xFF, sizeof s_shown.rain);
    s_shown.rain_opa = -1;
}

static void face_kind(const wx_temp_t *t)
{
    s_rain = t->rain.on;
    show_obj(s_thermo, !s_rain);
    show_obj(s_unit, s_rain);
    for (int s = 0; s < WX_SHEETS && !s_rain; s++) show_obj(s_sheet[s], false);
    int unit = lv_font_get_glyph_width(&lv_font_montserrat_30, '%', 0) + UNIT_GAP;
    int deg = lv_font_get_glyph_width(&font_digits_96, 0x00B0, 0) + VALUE_SPACE;
    lv_obj_align(s_value, LV_ALIGN_TOP_MID, s_rain ? -unit / 2 : deg / 2, VALUE_Y);
    s_shown.value[0] = 0;
    s_shown.value_w = -1;
}

static void apply(const wx_temp_t *t, const wx_bake_out_t *o, int kind)
{
    if (kind & WX_BAKE_SCENE) {
        lv_canvas_set_buffer(s_bg, s_buf.bg, WX_FACE, WX_FACE, LV_COLOR_FORMAT_RGB565);
        sprite(s_still, s_buf.still, o->still);
        sprite(s_drift, s_buf.drift, o->drift);
        face_kind(t);
        if (t->rain.on) rain_attach(&t->rain);
    } else if (kind & WX_BAKE_RING) {
        wx_ring_blit(s_buf.ring, s_buf.bg);
    }
    if (kind & (WX_BAKE_SCENE | WX_BAKE_RING)) lv_obj_invalidate(s_bg);
    if (kind & WX_BAKE_HERO) {
        s_front ^= 1;
        sprite(s_hero, s_hero_buf[s_front], o->hero);
        s_buf.hero = s_hero_buf[!s_front];
    }
    if (t->rain.on) {
        pct_label(s_lo, s_lo_txt, sizeof s_lo_txt, t->rain.has_pop, 0, LABEL_GREY_RGB);
        pct_label(s_hi, s_hi_txt, sizeof s_hi_txt, t->rain.has_pop, 100, wx_rain_rgb(t->rain.intensity));
    } else {
        end_label(s_lo, s_lo_txt, sizeof s_lo_txt, t->have && t->has_range, t->lo);
        end_label(s_hi, s_hi_txt, sizeof s_hi_txt, t->have && t->has_range, t->hi);
    }
    s_out = *o;
    s_tick = -1;
    s_shown.drift = 0x7FFFFFFF;
    ESP_LOGI(TAG, "baked parts %d (1 ring, 2 hero, 7 scene) face %d %d..%d hero %d clouds %d in %d ms", kind, t->face,
             t->lo, t->hi, (int)t->hero, t->cloud_band, s_io.ms);
}

static void shift_root(void)
{
    int dx, dy;
    wx_shift_xy(&s_shift, &dx, &dy);
    lv_obj_set_style_translate_x(s_root, dx, 0);
    lv_obj_set_style_translate_y(s_root, dy, 0);
}

static void set_value(const char *txt, uint32_t rgb)
{
    if (strcmp(txt, s_shown.value) != 0) {
        strlcpy(s_shown.value, txt, sizeof s_shown.value);
        lv_label_set_text_static(s_value, s_shown.value);
        lv_point_t sz = {0};
        if (s_rain) lv_text_get_size(&sz, s_shown.value, &font_digits_96, VALUE_SPACE, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (s_rain && sz.x != s_shown.value_w) {
            s_shown.value_w = sz.x;
            lv_obj_update_layout(s_value);
            lv_obj_align_to(s_unit, s_value, LV_ALIGN_OUT_RIGHT_BOTTOM, UNIT_GAP,
                            lv_font_montserrat_30.base_line - font_digits_96.base_line);
        }
    }
    if (rgb != s_shown.value_rgb) {
        s_shown.value_rgb = rgb;
        lv_obj_set_style_text_color(s_value, lv_color_hex(rgb), 0);
    }
}

static void set_unit(uint32_t rgb)
{
    if (rgb == s_shown.unit_rgb) return;
    s_shown.unit_rgb = rgb;
    lv_obj_set_style_text_color(s_unit, lv_color_hex(rgb), 0);
}

static void set_sub(const char *txt, uint32_t rgb, int opa)
{
    if (strcmp(txt, s_shown.sub) != 0) {
        strlcpy(s_shown.sub, txt, sizeof s_shown.sub);
        lv_label_set_text_static(s_sub, s_shown.sub);
    }
    if (rgb != s_shown.sub_rgb) {
        s_shown.sub_rgb = rgb;
        lv_obj_set_style_text_color(s_sub, lv_color_hex(rgb), 0);
    }
    if (opa != s_shown.sub_opa) {
        s_shown.sub_opa = opa;
        lv_obj_set_style_text_opa(s_sub, (lv_opa_t)opa, 0);
    }
}

static void set_marker(float frac, bool on)
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

static void set_mercury(float top, uint32_t rgb)
{
    int q = (int)lroundf(top * 2);
    if (q == s_shown.merc_q && rgb == s_shown.merc_rgb) return;
    s_shown.merc_q = q;
    s_shown.merc_rgb = rgb;
    wx_paint_mercury(&s_thermo_img, q / 2.0f, rgb);
    lv_obj_invalidate(s_thermo);
}

static void set_face_opa(int opa)
{
    if (opa == s_shown.face_opa) return;
    s_shown.face_opa = opa;
    lv_obj_set_style_image_opa(s_bg, (lv_opa_t)opa, 0);
    lv_obj_set_style_image_opa(s_thermo, (lv_opa_t)opa, 0);
    lv_obj_set_style_image_opa(s_marker, (lv_opa_t)opa, 0);
    lv_obj_set_style_text_opa(s_value, (lv_opa_t)opa, 0);
    lv_obj_set_style_text_opa(s_lo, (lv_opa_t)opa, 0);
    lv_obj_set_style_text_opa(s_hi, (lv_opa_t)opa, 0);
    lv_obj_set_style_text_opa(s_unit, (lv_opa_t)opa, 0);
    for (int s = 0; s < WX_SHEETS; s++) {
        lv_obj_set_style_image_opa(s_top[s], (lv_opa_t)opa, 0);
        lv_obj_set_style_image_opa(s_bottom[s], (lv_opa_t)opa, 0);
        lv_obj_set_style_image_opa(s_cover[s], (lv_opa_t)opa, 0);
    }
}

static void set_sprites(int opa, int drift)
{
    if (opa != s_shown.sprite_opa) {
        s_shown.sprite_opa = opa;
        lv_obj_set_style_image_opa(s_hero, (lv_opa_t)opa, 0);
        lv_obj_set_style_image_opa(s_still, (lv_opa_t)opa, 0);
        lv_obj_set_style_image_opa(s_drift, (lv_opa_t)opa, 0);
    }
    if (drift != s_shown.drift) {
        s_shown.drift = drift;
        lv_obj_set_x(s_drift, s_out.drift.x0 + drift);
    }
}

static void set_rain(const wx_rain_t *r, int opa, bool shown)
{
    for (int s = 0; s < WX_SHEETS; s++) show_obj(s_sheet[s], shown);
    if (!shown) return;
    wx_rain_frame_t f;
    wx_rain_frame(r, (int)floor(s_t * WX_RAIN_FPS), &f);
    wx_rain_ops_t op;
    wx_rain_ops(&s_shown.rain, s_shown.rain_opa, &f, opa, &op);
    s_shown.rain_opa = opa;
    for (int s = 0; s < WX_SHEETS; s++) {
        if (op.strip_y[s]) {
            lv_obj_set_y(s_strip[s][0], op.strip_top[s][0]);
            lv_obj_set_y(s_strip[s][1], op.strip_top[s][1]);
        }
        if (op.opa) {
            lv_obj_set_style_image_opa(s_strip[s][0], (lv_opa_t)op.layer_opa, 0);
            lv_obj_set_style_image_opa(s_strip[s][1], (lv_opa_t)op.layer_opa, 0);
        }
    }
    for (int i = 0; i < WX_DRIPS; i++) {
        if (op.drip_buf[i])
            lv_canvas_set_buffer(s_drip[i], s_buf.still + s_lay.drip[op.drip_pose[i]], WX_DRIP_W, WX_DRIP_H, LV_COLOR_FORMAT_A8);
        if (op.drip_y[i]) lv_obj_set_y(s_drip[i], op.drip_top[i]);
        if (op.drip_opa[i]) lv_obj_set_style_image_opa(s_drip[i], (lv_opa_t)op.drip_alpha[i], 0);
        show_obj(s_drip[i], op.drip_on[i]);
    }
    for (int n = 0; n < WX_SPLASHES; n++) {
        if (op.splash_y[n]) lv_obj_set_y(s_splash[n], op.splash_top[n]);
        if (op.splash_opa[n]) lv_obj_set_style_image_opa(s_splash[n], (lv_opa_t)op.splash_alpha[n], 0);
        show_obj(s_splash[n], op.splash_on[n]);
    }
    if (op.opa) lv_obj_set_style_image_opa(s_bolt, (lv_opa_t)op.layer_opa, 0);
    show_obj(s_bolt, op.bolt_on);
    s_shown.rain = f;
}

static int drift_px(double t)
{
    double p = fmod(t, DRIFT_PERIOD_S) / DRIFT_PERIOD_S;
    double tri = 1 - 4 * fabs(p - 0.5);
    return 2 * (int)lround(tri * 2);
}

static void paint(const wx_temp_t *t, const wx_obs_t *obs, bool offline, int fo)
{
    set_face_opa(fo);
    double ta = floor(s_t * ARRIVE_FPS) / ARRIVE_FPS;
    double a = wx_arrive(ta);
    double tf = floor(s_t * FADE_FPS) / FADE_FPS;
    int sprite_opa = (int)lround(fo * wx_arrive(tf));
    const wx_rain_t *r = wx_rain_same_scene(&t->rain, &s_io.key.rain) ? &t->rain : &s_io.key.rain;
    char txt[20];
    if (s_rain) {
        if (r->has_pop) {
            int k = wx_count_step(wx_count_n(r->pop), ta);
            snprintf(txt, sizeof txt, "%d", wx_count_value(r->pop, k));
            set_value(txt, wx_rain_rgb(r->intensity));
        } else {
            set_value("--", GREY_RGB);
        }
        set_marker(r->pop / 100.0f * (float)a, r->has_pop);
        set_unit(r->has_pop ? wx_rain_rgb(r->intensity) : GREY_RGB);
        set_rain(r, sprite_opa, fo == 255 && !r->dry);
    } else if (t->have) {
        int k = wx_count_step(wx_count_n(t->value), ta);
        int v = wx_count_value(t->value, k);
        uint32_t rgb = t->still ? GREY_RGB : wx_temp_rgb((float)v);
        deg_text(txt, sizeof txt, v);
        set_value(txt, rgb);
        float top = WX_MERC_BOTTOM - (WX_MERC_BOTTOM - wx_merc_top((float)t->value)) * (float)a;
        set_mercury(top, rgb);
    } else {
        set_value("--\xC2\xB0", GREY_RGB);
        set_mercury(WX_MERC_BOTTOM, GREY_RGB);
    }
    if (!s_rain) set_marker(t->frac * (float)a, t->have);
    set_sprites(sprite_opa, !s_rain && s_t >= WX_ARRIVE_S ? drift_px(s_t - WX_ARRIVE_S) : 0);
    double sf = (s_t - WX_ARRIVE_S) / SUB_FADE_S;
    int sub_opa = (sf <= 0 ? 0 : sf >= 1 ? 255 : 85 * (1 + (int)(sf * 3))) * fo / 255;
    char age[24];
    if (offline && obs && obs->valid) {
        wx_age_label(obs, age, sizeof age);
        set_sub(age, GREY_RGB, sub_opa);
    } else {
        set_sub(s_rain ? r->sub : t->have ? t->sub : "", s_rain ? SUB_RGB : t->sub_rgb, sub_opa);
    }
}

static void act(int a)
{
    if (a & WX_DO_RELEASE) release();
    if (a & WX_DO_ACQUIRE) {
        bool ok = acquire();
        wx_job_acquired(&s_job, ok);
        if (ok) shift_root();
    }
    if (a & WX_DO_DETACH) detach();
    if (a & WX_DO_BAKE) xTaskNotifyGive(s_task);
    if (a & WX_DO_APPLY) apply(&s_io.key, &s_io.out, s_io.kind);
    if (a & WX_DO_ARRIVE) {
        s_t = 0;
        s_tick = -1;
    }
}

void temp_face_show(bool on)
{
    if (!s_root || on == s_job.visible) return;
    s_t = 0;
    s_tick = -1;
    if (on) {
        wx_faces_open(&s_faces);
        s_face = -1;
    }
    if (!on) show_obj(s_root, false);
    act(wx_job_show(&s_job, on));
}

void temp_face_turn(int detents)
{
    if (!s_root || !s_job.visible) return;
    if (wx_faces_turn(&s_faces, wx_job_face(&s_job), s_face, detents)) wx_job_cut(&s_job);
}

void temp_face_hold(bool hold)
{
    if (s_root) act(wx_job_hold(&s_job, hold));
}

void temp_face_update(const wx_obs_t *obs, bool offline, double dt)
{
    if (!s_root) return;
    if (!isfinite(dt) || dt < 0) dt = 0;
    wx_temp_t t = {0};
    if (s_job.visible) {
        wx_temp_face(obs, s_job.baked ? &s_io.key : NULL, s_faces.pick, &t);
        s_face = t.face;
    }
    int a = wx_job_step(&s_job, &t, dt);
    act(a);
    if (!(a & WX_DO_PAINT)) return;
    if (wx_shift_tick(&s_shift, dt)) shift_root();
    s_t += dt;
    int tick = (int)floor(s_t * ARRIVE_FPS);
    if (s_t < WX_ARRIVE_S + SUB_FADE_S && tick == s_tick && s_job.opa == s_shown.face_opa) return;
    s_tick = tick;
    paint(&t, obs, offline, s_job.opa);
    show_obj(s_root, true);
}
