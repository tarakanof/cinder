#include "temp_face.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "page_canvas.h"
#include "page_ops.h"
#include "wx_air.h"
#include "wx_cast.h"
#include "wx_days.h"
#include "wx_faces.h"
#include "wx_job.h"
#include "wx_paint.h"
#include "wx_sun.h"
#include "wx_temp.h"
#include "wx_wind.h"

LV_FONT_DECLARE(font_digits_96);
LV_FONT_DECLARE(font_face_36);

static const char *TAG = "tempface";

_Static_assert(WX_SCENE_ALIGN % LV_DRAW_BUF_ALIGN == 0, "cloud sprites start on LVGL's draw buffer alignment");
_Static_assert(WX_FACE * WX_FACE * 2 <= PAGE_CANVAS_BYTES, "the background bakes on the page canvas");

#define FACE_X 3
#define VALUE_Y 200
#define SUB_Y WX_SUB_Y
#define END_X 96
#define END_Y 362
#define VALUE_SPACE (-2)
#define ARRIVE_FPS 30.0
#define FADE_FPS 15.0
#define SUB_FADE_S 0.48
#define GREY_RGB 0x5A5A5A
#define UNIT_GAP 6
#define SNOW_RGB 0xD0DCE8
#define BOLT_RGB 0xF0D040
#define LABEL_GREY_RGB 0x8A8A8A
#define SUB_RGB 0xBDBDBD
#define BAKE_YIELD_US 25000

#define DROP(p) do { heap_caps_free(p); (p) = NULL; } while (0)

static lv_obj_t *s_root, *s_bg, *s_hero, *s_thermo, *s_marker, *s_value, *s_unit, *s_lo, *s_hi, *s_sub;
static lv_obj_t *s_sheet[WX_SHEETS], *s_strip[WX_SHEETS][2], *s_top[WX_SHEETS], *s_bottom[WX_SHEETS], *s_cover[WX_SHEETS];
static lv_obj_t *s_drip[WX_DRIPS], *s_splash[WX_SPLASHES], *s_bolt;
static lv_obj_t *s_cbox[WX_CAST_SIDES], *s_cloud[WX_CAST_SIDES], *s_csheet[WX_CAST_SIDES], *s_cstrip[WX_CAST_SIDES][2];
static lv_obj_t *s_ctop[WX_CAST_SIDES], *s_cbottom[WX_CAST_SIDES], *s_drop, *s_clabel;
static lv_obj_t *s_wbox, *s_wsock, *s_wdark, *s_wstreak[WX_WIND_ROWS], *s_wleaf[WX_WIND_LEAVES];
static lv_obj_t *s_dbox, *s_drow[WX_DAYS], *s_dicon[WX_DAYS], *s_dday[WX_DAYS], *s_dlo[WX_DAYS], *s_dhi[WX_DAYS], *s_dpop[WX_DAYS];
static lv_obj_t *s_srays, *s_sbox, *s_ssun, *s_abox, *s_apetal;
static EXT_RAM_BSS_ATTR bool s_air;
static EXT_RAM_BSS_ATTR wx_sun_layout_t s_slay;
static EXT_RAM_BSS_ATTR bool s_sun;
static EXT_RAM_BSS_ATTR wx_days_layout_t s_dlay;
static EXT_RAM_BSS_ATTR bool s_days;
static EXT_RAM_BSS_ATTR char s_dlo_txt[WX_DAYS][12], s_dhi_txt[WX_DAYS][12], s_dpop_txt[WX_DAYS][8];
static EXT_RAM_BSS_ATTR wx_wind_layout_t s_wlay;
static EXT_RAM_BSS_ATTR bool s_wind;
static EXT_RAM_BSS_ATTR wx_cast_layout_t s_clay;
static EXT_RAM_BSS_ATTR bool s_cast, s_unit_on;
static uint8_t *s_drop_buf;
static EXT_RAM_BSS_ATTR wx_img_t s_drop_img;
static EXT_RAM_BSS_ATTR wx_rain_layout_t s_lay;
static EXT_RAM_BSS_ATTR bool s_rain;
static EXT_RAM_BSS_ATTR wx_bake_buf_t s_buf;
static EXT_RAM_BSS_ATTR int64_t s_yield_at;
static uint8_t *s_thermo_buf, *s_marker_buf;
static EXT_RAM_BSS_ATTR uint8_t *s_hero_buf[2];
static EXT_RAM_BSS_ATTR int s_front;
static uint8_t s_none[2 * 2 * 3];
static EXT_RAM_BSS_ATTR wx_img_t s_thermo_img;
static EXT_RAM_BSS_ATTR wx_job_io_t s_io;
static EXT_RAM_BSS_ATTR wx_bake_out_t s_out;
static EXT_RAM_BSS_ATTR wx_shift_t s_shift;
static EXT_RAM_BSS_ATTR lv_obj_t *s_cl[WX_CLOUDS_MAX];
static EXT_RAM_BSS_ATTR wx_drift_t s_cd;
static wx_job_t s_job;
static TaskHandle_t s_task;
static EXT_RAM_BSS_ATTR double s_t;
static EXT_RAM_BSS_ATTR int s_tick;
static EXT_RAM_BSS_ATTR wx_faces_t s_faces;
static EXT_RAM_BSS_ATTR int s_face;

typedef struct {
    char value[20], sub[40], label[8], label_sub[40];
    uint32_t value_rgb, sub_rgb;
    int marker_x, marker_y, merc_q, sprite_opa, sub_opa, face_opa, rain_opa, value_w;
    uint32_t merc_rgb, unit_rgb;
    wx_rain_frame_t rain;
    int cast_dx[WX_CAST_SIDES], cast_opa[WX_CAST_SIDES], cast_off[WX_CAST_SIDES], cast_gate, drop_q, drop_opa, label_opa;
    bool label_clear;
    const char *unit;
    int wind_pose, sock_opa, streak_dx[WX_WIND_ROWS], streak_opa[WX_WIND_ROWS];
    int leaf_x[WX_WIND_LEAVES], leaf_y[WX_WIND_LEAVES], leaf_rot[WX_WIND_LEAVES], leaf_opa[WX_WIND_LEAVES];
    uint32_t sock_rgb;
    int day_opa[WX_DAYS], day_dy[WX_DAYS], day_lo[WX_DAYS], day_hi[WX_DAYS];
    int sun_dy, sun_opa, rays_opa;
    int petal_x, petal_y, petal_rot, petal_opa;
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

static lv_obj_t *box_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static void cast_create(void)
{
    for (int s = 0; s < WX_CAST_SIDES; s++) {
        s_cbox[s] = box_obj(s_root);
        s_csheet[s] = box_obj(s_root);
        s_cstrip[s][0] = a8(s_csheet[s], WX_CAST_RGB);
        s_cstrip[s][1] = a8(s_csheet[s], WX_CAST_RGB);
        s_ctop[s] = a8(s_csheet[s], 0);
        s_cbottom[s] = a8(s_csheet[s], 0);
        s_cloud[s] = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
        lv_obj_add_flag(s_cloud[s], LV_OBJ_FLAG_HIDDEN);
    }
}

static void wind_create(void)
{
    s_wbox = box_obj(s_root);
    for (int r = 0; r < WX_WIND_ROWS; r++) {
        s_wstreak[r] = a8(s_root, WX_WIND_STREAK_RGB);
        lv_obj_add_flag(s_wstreak[r], LV_OBJ_FLAG_HIDDEN);
    }
    s_wsock = a8(s_root, 0);
    s_wdark = a8(s_root, 0);
    lv_obj_add_flag(s_wsock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_wdark, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < WX_WIND_LEAVES; i++) {
        s_wleaf[i] = a8(s_root, WX_WIND_LEAF_RGB);
        lv_obj_add_flag(s_wleaf[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void days_create(void)
{
    s_dbox = box_obj(s_root);
    for (int i = 0; i < WX_DAYS; i++) {
        lv_obj_t *r = s_drow[i] = box_obj(s_root);
        s_dicon[i] = canvas(r, LV_COLOR_FORMAT_RGB565A8);
        s_dday[i] = label(r, &lv_font_montserrat_30, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_text_color(s_dday[i], lv_color_hex(WX_DAYS_NAME_RGB), 0);
        s_dlo[i] = label(r, &font_face_36, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_set_style_text_opa(s_dlo[i], WX_DAYS_LO_OPA, 0);
        s_dhi[i] = label(r, &font_face_36, LV_ALIGN_TOP_LEFT, 0, 0);
        s_dpop[i] = label(r, &lv_font_montserrat_24, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_text_color(s_dpop[i], lv_color_hex(WX_DAYS_POP_RGB), 0);
    }
}

static void sun_create(void)
{
    s_srays = a8(s_root, WX_SUN_GOLD_RGB);
    lv_obj_add_flag(s_srays, LV_OBJ_FLAG_HIDDEN);
    s_sbox = box_obj(s_root);
    s_ssun = canvas(s_sbox, LV_COLOR_FORMAT_RGB565A8);
}

static void air_create(void)
{
    s_abox = box_obj(s_root);
    s_apetal = canvas(s_abox, LV_COLOR_FORMAT_RGB565A8);
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
        const wx_temp_t *t = wx_job_take(&s_job);
        if (!t) continue;
        int64_t t0 = esp_timer_get_time();
        s_yield_at = t0;
        wx_bake(t, s_io.kind, &s_buf, &s_io.out);
        wx_job_done(&s_job, (int)((esp_timer_get_time() - t0) / 1000));
    }
}

void temp_face_create(lv_obj_t *parent)
{
    if (s_root) return;
    wx_job_init(&s_job, &s_io);
    s_buf.yield = bake_yield;
    if (xTaskCreatePinnedToCoreWithCaps(bake_task, "wxbake", 5120, NULL, 2, &s_task, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
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
    for (int i = 0; i < WX_CLOUDS_MAX; i++) s_cl[i] = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
    rain_create();
    cast_create();
    wind_create();
    days_create();
    sun_create();
    air_create();
    s_thermo = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
    lv_obj_set_pos(s_thermo, WX_THERMO_X, WX_THERMO_Y);
    lv_obj_add_flag(s_hero, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < WX_CLOUDS_MAX; i++) lv_obj_add_flag(s_cl[i], LV_OBJ_FLAG_HIDDEN);

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
    s_drop = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);
    lv_obj_add_flag(s_drop, LV_OBJ_FLAG_HIDDEN);
    s_clabel = label(s_root, &lv_font_montserrat_24, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_color(s_clabel, lv_color_hex(WX_CAST_RGB), 0);
    lv_obj_add_flag(s_clabel, LV_OBJ_FLAG_HIDDEN);
}

static void detach(void)
{
    show_obj(s_root, false);
    lv_canvas_set_buffer(s_bg, s_none, 2, 2, LV_COLOR_FORMAT_RGB565);
    lv_canvas_set_buffer(s_hero, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    for (int i = 0; i < WX_CLOUDS_MAX; i++) lv_canvas_set_buffer(s_cl[i], s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
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
    for (int s = 0; s < WX_CAST_SIDES; s++) {
        lv_canvas_set_buffer(s_cloud[s], s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
        lv_canvas_set_buffer(s_cstrip[s][0], s_none, 2, 2, LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_cstrip[s][1], s_none, 2, 2, LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_ctop[s], s_none, 2, 2, LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_cbottom[s], s_none, 2, 2, LV_COLOR_FORMAT_A8);
    }
    lv_canvas_set_buffer(s_wsock, s_none, 2, 2, LV_COLOR_FORMAT_A8);
    lv_canvas_set_buffer(s_wdark, s_none, 2, 2, LV_COLOR_FORMAT_A8);
    for (int r = 0; r < WX_WIND_ROWS; r++) lv_canvas_set_buffer(s_wstreak[r], s_none, 2, 2, LV_COLOR_FORMAT_A8);
    for (int i = 0; i < WX_WIND_LEAVES; i++) lv_canvas_set_buffer(s_wleaf[i], s_none, 2, 2, LV_COLOR_FORMAT_A8);
    for (int i = 0; i < WX_DAYS; i++) lv_canvas_set_buffer(s_dicon[i], s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    lv_canvas_set_buffer(s_srays, s_none, 2, 2, LV_COLOR_FORMAT_A8);
    lv_canvas_set_buffer(s_ssun, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    lv_canvas_set_buffer(s_apetal, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
}

static void release(void)
{
    detach();
    lv_canvas_set_buffer(s_thermo, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    lv_canvas_set_buffer(s_marker, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    lv_canvas_set_buffer(s_drop, s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    s_buf.bg = NULL;
    page_canvas_give(PAGE_CANVAS_WEATHER);
    DROP(s_buf.lut);
    DROP(s_buf.ring);
    DROP(s_hero_buf[0]);
    DROP(s_hero_buf[1]);
    s_buf.hero = NULL;
    DROP(s_buf.scene);
    DROP(s_thermo_buf);
    DROP(s_marker_buf);
    DROP(s_drop_buf);
    wx_job_released(&s_job);
}

static bool acquire(void)
{
    s_buf.bg = page_canvas_take(PAGE_CANVAS_WEATHER);
    s_buf.lut = psram(WX_RING_LUT * sizeof *s_buf.lut);
    s_buf.ring = psram(sizeof *s_buf.ring);
    s_hero_buf[0] = psram(WX_HERO_BOX * WX_HERO_BOX * 3);
    s_hero_buf[1] = psram(WX_HERO_BOX * WX_HERO_BOX * 3);
    s_buf.scene = psram((size_t)wx_scene_px() * 3);
    s_thermo_buf = psram(WX_THERMO_W * WX_THERMO_H * 3);
    s_marker_buf = psram(WX_MARKER * WX_MARKER * 3);
    s_drop_buf = psram(WX_DROP_BOX * WX_DROP_BOX * 3);
    if (!s_buf.bg || !s_buf.lut || !s_buf.ring || !s_hero_buf[0] || !s_hero_buf[1] || !s_buf.scene || !s_thermo_buf || !s_marker_buf ||
        !s_drop_buf) {
        ESP_LOGE(TAG, "no PSRAM for the face (canvas owner %d, %u KB free, largest %u KB): black, retrying in 5 s", (int)page_canvas_owner(),
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
    s_drop_img = (wx_img_t){.px = (uint16_t *)s_drop_buf, .a = s_drop_buf + WX_DROP_BOX * WX_DROP_BOX * 2, .w = WX_DROP_BOX,
                            .h = WX_DROP_BOX};
    lv_canvas_set_buffer(s_drop, s_drop_buf, WX_DROP_BOX, WX_DROP_BOX, LV_COLOR_FORMAT_RGB565A8);
    memset(&s_shown, 0xFF, sizeof s_shown);
    s_shown.value[0] = s_shown.sub[0] = s_shown.label[0] = s_shown.label_sub[0] = 0;
    s_shown.unit = NULL;
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

static void fixed_label(lv_obj_t *l, const char *txt, bool on, uint32_t rgb)
{
    show_obj(l, on);
    if (!on) return;
    lv_label_set_text_static(l, txt);
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
    uint8_t *m = s_buf.scene;
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

static void cast_attach(const wx_cast_t *c)
{
    uint8_t *m = s_buf.scene;
    wx_cast_layout(c->variant, &s_clay);
    uint32_t col = c->snow ? SNOW_RGB : WX_CAST_RGB;
    for (int s = 0; s < WX_CAST_SIDES; s++) {
        bool on = s < s_clay.sides;
        show_obj(s_cbox[s], on);
        show_obj(s_cloud[s], on);
        show_obj(s_csheet[s], false);
        s_shown.cast_dx[s] = s_shown.cast_opa[s] = s_shown.cast_off[s] = INT_MIN;
        if (!on) continue;
        const wx_cast_sheet_t *sh = wx_cast_sheet(c->variant, s);
        wx_box_t b = s_clay.box[s], cb = s_clay.cloud[s], sb = sh->box;
        int w = sb.x1 - sb.x0, h = sb.y1 - sb.y0;
        lv_obj_set_pos(s_cbox[s], b.x0, b.y0);
        lv_obj_set_size(s_cbox[s], b.x1 - b.x0, b.y1 - b.y0);
        lv_canvas_set_buffer(s_cloud[s], m + s_clay.cloud_at[s], cb.x1 - cb.x0, cb.y1 - cb.y0, LV_COLOR_FORMAT_RGB565A8);
        lv_obj_set_pos(s_cloud[s], cb.x0, cb.y0);
        lv_obj_set_pos(s_csheet[s], sb.x0, sb.y0);
        lv_obj_set_size(s_csheet[s], w, h);
        for (int k = 0; k < 2; k++) {
            lv_canvas_set_buffer(s_cstrip[s][k], m + s_clay.strip[s], w, 2 * sh->h, LV_COLOR_FORMAT_A8);
            lv_obj_set_style_image_recolor(s_cstrip[s][k], lv_color_hex(col), 0);
        }
        lv_canvas_set_buffer(s_ctop[s], m + s_clay.top[s], w, s_clay.ramp_h[s], LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_cbottom[s], m + s_clay.bottom[s], w, s_clay.ramp_h[s], LV_COLOR_FORMAT_A8);
        lv_obj_set_y(s_cbottom[s], h - s_clay.ramp_h[s]);
        lv_obj_invalidate(s_cbox[s]);
    }
    s_shown.cast_gate = s_shown.drop_q = s_shown.drop_opa = s_shown.label_opa = INT_MIN;
    s_shown.label[0] = s_shown.label_sub[0] = 0;
}

static void wind_attach(const wx_wind_t *w)
{
    uint8_t *m = s_buf.scene;
    wx_wind_layout(w->level, &s_wlay);
    wx_box_t a = s_wlay.area, b = s_wlay.sock;
    int sw = b.x1 - b.x0, sh = b.y1 - b.y0;
    lv_obj_set_pos(s_wbox, a.x0, a.y0);
    lv_obj_set_size(s_wbox, a.x1 - a.x0, a.y1 - a.y0);
    show_obj(s_wbox, true);
    lv_canvas_set_buffer(s_wsock, m + s_wlay.fill[1], sw, sh, LV_COLOR_FORMAT_A8);
    lv_canvas_set_buffer(s_wdark, m + s_wlay.dark[1], sw, sh, LV_COLOR_FORMAT_A8);
    lv_obj_set_pos(s_wsock, b.x0, b.y0);
    lv_obj_set_pos(s_wdark, b.x0, b.y0);
    show_obj(s_wsock, true);
    show_obj(s_wdark, true);
    for (int r = 0; r < WX_WIND_ROWS; r++) {
        show_obj(s_wstreak[r], false);
        s_shown.streak_dx[r] = s_shown.streak_opa[r] = INT_MIN;
        if (r >= s_wlay.nrows) continue;
        lv_canvas_set_buffer(s_wstreak[r], m + s_wlay.streak[r], s_wlay.streak_w[r], WX_STREAK_H, LV_COLOR_FORMAT_A8);
        lv_obj_set_y(s_wstreak[r], wx_wind_streak_y(w->level, r));
    }
    for (int i = 0; i < WX_WIND_LEAVES; i++) {
        show_obj(s_wleaf[i], false);
        s_shown.leaf_x[i] = s_shown.leaf_y[i] = s_shown.leaf_rot[i] = s_shown.leaf_opa[i] = INT_MIN;
    }
    s_shown.wind_pose = 1;
    s_shown.sock_opa = INT_MIN;
    s_shown.sock_rgb = 0xFFFFFFFF;
    lv_obj_invalidate(s_wbox);
}

static void days_attach(const wx_days_t *d)
{
    uint8_t *m = s_buf.scene;
    wx_days_layout(d, &s_dlay);
    const wx_days_cols_t *c = &s_dlay.c;
    wx_box_t a = s_dlay.area, sp = s_dlay.sprite;
    lv_obj_set_pos(s_dbox, a.x0, a.y0);
    lv_obj_set_size(s_dbox, a.x1 - a.x0, a.y1 - a.y0);
    show_obj(s_dbox, s_dlay.n > 0);
    int temp_y = WX_DAYS_ROW_UP - font_face_36.line_height / 2;
    for (int i = 0; i < WX_DAYS; i++) {
        bool on = i < s_dlay.n;
        show_obj(s_drow[i], on);
        s_shown.day_opa[i] = s_shown.day_dy[i] = s_shown.day_lo[i] = s_shown.day_hi[i] = INT_MIN;
        if (!on) continue;
        const wx_days_row_t *r = &d->row[i];
        wx_box_t rb = wx_days_row_box(&s_dlay, i, 0);
        int w = rb.x1 - rb.x0;
        lv_obj_set_pos(s_drow[i], rb.x0, rb.y0);
        lv_obj_set_size(s_drow[i], w, rb.y1 - rb.y0);
        lv_canvas_set_buffer(s_dicon[i], m + s_dlay.at[i], sp.x1 - sp.x0, sp.y1 - sp.y0, LV_COLOR_FORMAT_RGB565A8);
        lv_obj_set_pos(s_dicon[i], sp.x0 - rb.x0, sp.y0);
        lv_label_set_text_static(s_dday[i], wx_weekday_name(r->wday));
        lv_obj_align(s_dday[i], LV_ALIGN_TOP_LEFT, c->day_x - rb.x0, WX_DAYS_ROW_UP - lv_font_montserrat_30.line_height / 2);
        lv_obj_align(s_dlo[i], LV_ALIGN_TOP_RIGHT, c->lo_x + c->lo_w - rb.x1, temp_y);
        lv_obj_align(s_dhi[i], LV_ALIGN_TOP_LEFT, c->hi_x - rb.x0, temp_y);
        show_obj(s_dpop[i], r->pop >= 0);
        if (r->pop < 0) continue;
        snprintf(s_dpop_txt[i], sizeof s_dpop_txt[i], "%d%%", r->pop);
        lv_label_set_text_static(s_dpop[i], s_dpop_txt[i]);
        lv_obj_align(s_dpop[i], LV_ALIGN_TOP_MID, c->icon_x + WX_DAYS_ICON / 2 - rb.x0 - w / 2, WX_DAYS_ROW_UP + WX_DAYS_POP_DY);
    }
    lv_obj_invalidate(s_dbox);
}

static void sun_attach(void)
{
    uint8_t *m = s_buf.scene;
    wx_sun_layout(&s_slay);
    wx_box_t r = s_slay.rays, b = s_slay.sun;
    lv_canvas_set_buffer(s_srays, m + s_slay.rays_at, r.x1 - r.x0, r.y1 - r.y0, LV_COLOR_FORMAT_A8);
    lv_obj_set_pos(s_srays, r.x0, r.y0);
    lv_obj_set_pos(s_sbox, b.x0, b.y0);
    lv_obj_set_size(s_sbox, b.x1 - b.x0, b.y1 - b.y0);
    lv_canvas_set_buffer(s_ssun, m + s_slay.sun_at, b.x1 - b.x0, b.y1 - b.y0, LV_COLOR_FORMAT_RGB565A8);
    show_obj(s_srays, false);
    show_obj(s_sbox, true);
    s_shown.sun_dy = s_shown.sun_opa = s_shown.rays_opa = INT_MIN;
    lv_obj_invalidate(s_sbox);
}

static void air_attach(const wx_air_t *a)
{
    wx_box_t b = wx_air_petal_box();
    lv_obj_set_pos(s_abox, b.x0, b.y0);
    lv_obj_set_size(s_abox, b.x1 - b.x0, b.y1 - b.y0);
    show_obj(s_abox, a->wilted);
    show_obj(s_apetal, false);
    s_shown.petal_x = s_shown.petal_y = s_shown.petal_rot = s_shown.petal_opa = INT_MIN;
}

static void face_kind(const wx_temp_t *t)
{
    s_rain = t->rain.on;
    s_cast = t->cast.on;
    s_wind = t->wind.on;
    s_days = t->days.on;
    s_sun = t->sun.on;
    s_air = t->air.on;
    s_unit_on = s_rain || s_cast || s_wind || s_sun || s_air;
    show_obj(s_thermo, !s_rain && !s_cast && !s_wind && !s_days && !s_sun && !s_air);
    show_obj(s_unit, s_rain);
    lv_label_set_text_static(s_unit, "%");
    s_shown.unit = NULL;
    for (int s = 0; s < WX_SHEETS && !s_rain; s++) show_obj(s_sheet[s], false);
    for (int s = 0; s < WX_CAST_SIDES && !s_cast; s++) {
        show_obj(s_cbox[s], false);
        show_obj(s_cloud[s], false);
        show_obj(s_csheet[s], false);
    }
    if (!s_wind) {
        show_obj(s_wbox, false);
        show_obj(s_wsock, false);
        show_obj(s_wdark, false);
        for (int r = 0; r < WX_WIND_ROWS; r++) show_obj(s_wstreak[r], false);
        for (int i = 0; i < WX_WIND_LEAVES; i++) show_obj(s_wleaf[i], false);
    }
    if (!s_days) {
        show_obj(s_dbox, false);
        for (int i = 0; i < WX_DAYS; i++) show_obj(s_drow[i], false);
    }
    if (!s_sun) {
        show_obj(s_srays, false);
        show_obj(s_sbox, false);
    }
    if (!s_air) show_obj(s_abox, false);
    bool rows = s_days && t->days.have;
    show_obj(s_value, !rows);
    lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, rows ? WX_DAYS_SUB_Y : SUB_Y);
    show_obj(s_drop, false);
    show_obj(s_clabel, false);
    lv_label_set_recolor(s_sub, s_cast || s_wind);
    int unit = lv_font_get_glyph_width(&lv_font_montserrat_30, '%', 0) + UNIT_GAP;
    int deg = lv_font_get_glyph_width(&font_digits_96, 0x00B0, 0) + VALUE_SPACE;
    lv_obj_align(s_value, LV_ALIGN_TOP_MID, s_rain ? -unit / 2 : s_cast || s_wind || s_days || s_sun || s_air ? 0 : deg / 2, VALUE_Y);
    s_shown.value[0] = 0;
    s_shown.value_w = -1;
}

static void apply(const wx_temp_t *t, const wx_bake_out_t *o, int kind)
{
    if (kind & WX_BAKE_SCENE) {
        lv_canvas_set_buffer(s_bg, s_buf.bg, WX_FACE, WX_FACE, LV_COLOR_FORMAT_RGB565);
        for (int i = 0; i < WX_CLOUDS_MAX; i++) {
            bool on = i < o->nclouds;
            sprite(s_cl[i], on ? s_buf.scene + o->cloud_at[i] : s_none, on ? o->cloud[i] : (wx_box_t){0, 0, 0, 0});
        }
        wx_drift_start(&s_cd, o->nclouds, o->cloud_back, esp_random());
        face_kind(t);
        if (t->rain.on) rain_attach(&t->rain);
        if (t->cast.on) cast_attach(&t->cast);
        if (t->wind.on) wind_attach(&t->wind);
        if (t->days.on) days_attach(&t->days);
        if (t->sun.on) sun_attach();
        if (t->air.on) air_attach(&t->air);
    } else if (kind & WX_BAKE_RING) {
        wx_ring_blit(s_buf.ring, s_buf.bg);
    }
    if (kind & (WX_BAKE_SCENE | WX_BAKE_RING)) lv_obj_invalidate(s_bg);
    if (kind & WX_BAKE_HERO) {
        s_front ^= 1;
        sprite(s_hero, s_hero_buf[s_front], o->hero);
        s_buf.hero = s_hero_buf[!s_front];
    }
    if (t->cast.on || t->days.on || t->air.on) {
        show_obj(s_lo, false);
        show_obj(s_hi, false);
    } else if (t->sun.on) {
        strlcpy(s_lo_txt, t->sun.rise, sizeof s_lo_txt);
        strlcpy(s_hi_txt, t->sun.set, sizeof s_hi_txt);
        fixed_label(s_lo, s_lo_txt, t->sun.have, WX_SUN_HZN_RGB);
        fixed_label(s_hi, s_hi_txt, t->sun.have, WX_SUN_HZN_RGB);
    } else if (t->wind.on) {
        fixed_label(s_lo, "0", t->wind.have, wx_wind_rgb(0));
        fixed_label(s_hi, "50+", t->wind.have, wx_wind_rgb(WX_WIND_GAUGE_KMH));
    } else if (t->rain.on) {
        pct_label(s_lo, s_lo_txt, sizeof s_lo_txt, t->rain.has_pop, 0, LABEL_GREY_RGB);
        pct_label(s_hi, s_hi_txt, sizeof s_hi_txt, t->rain.has_pop, 100, wx_rain_rgb(t->rain.intensity));
    } else {
        end_label(s_lo, s_lo_txt, sizeof s_lo_txt, t->have && t->has_range, t->lo);
        end_label(s_hi, s_hi_txt, sizeof s_hi_txt, t->have && t->has_range, t->hi);
    }
    s_out = *o;
    s_tick = -1;
    ESP_LOGI(TAG, "baked parts %d (1 ring, 2 hero, 7 scene) face %d %d..%d hero %d clouds %d in %d ms", kind, t->face,
             t->lo, t->hi, (int)t->hero, t->cloud_band, s_io.ms);
}

static void shift_root(void)
{
    int dx, dy;
    wx_shift_xy(&s_shift, &WX_SHIFT_SQUARE, &dx, &dy);
    lv_obj_set_style_translate_x(s_root, dx, 0);
    lv_obj_set_style_translate_y(s_root, dy, 0);
}

static void set_value(const char *txt, uint32_t rgb)
{
    if (strcmp(txt, s_shown.value) != 0) {
        strlcpy(s_shown.value, txt, sizeof s_shown.value);
        lv_label_set_text_static(s_value, s_shown.value);
        lv_point_t sz = {0};
        if (s_unit_on) lv_text_get_size(&sz, s_shown.value, &font_digits_96, VALUE_SPACE, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (s_unit_on && sz.x != s_shown.value_w) {
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

static void place_marker(float x, float y, bool on)
{
    show_obj(s_marker, on);
    if (!on) return;
    int mx = (int)lroundf(x) - WX_MARKER / 2, my = (int)lroundf(y) - WX_MARKER / 2;
    if (mx == s_shown.marker_x && my == s_shown.marker_y) return;
    s_shown.marker_x = mx;
    s_shown.marker_y = my;
    lv_obj_set_pos(s_marker, mx, my);
}

static void set_marker(float frac, bool on)
{
    float x = 0, y = 0;
    if (on) wx_gauge_xy(frac, &x, &y);
    place_marker(x, y, on);
}

static void set_unit_text(const char *u)
{
    if (u == s_shown.unit) return;
    s_shown.unit = u;
    show_obj(s_unit, u != NULL);
    int w = 0;
    if (u) {
        lv_label_set_text_static(s_unit, u);
        lv_point_t sz = {0};
        lv_text_get_size(&sz, u, &lv_font_montserrat_30, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        w = sz.x + UNIT_GAP;
    }
    lv_obj_align(s_value, LV_ALIGN_TOP_MID, -w / 2, VALUE_Y);
    s_shown.value[0] = 0;
    s_shown.value_w = -1;
}

static void set_label(const wx_cast_t *c, int opa)
{
    if (strcmp(c->label, s_shown.label) != 0 || strcmp(c->sub, s_shown.label_sub) != 0) {
        strlcpy(s_shown.label, c->label, sizeof s_shown.label);
        strlcpy(s_shown.label_sub, c->sub, sizeof s_shown.label_sub);
        lv_label_set_text_static(s_clabel, s_shown.label);
        lv_point_t sz = {0}, sub = {0};
        lv_text_get_size(&sz, s_shown.label, &lv_font_montserrat_24, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_text_get_size(&sub, c->sub, &lv_font_montserrat_30, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_RECOLOR);
        int x, y;
        s_shown.label_clear = wx_cast_label_place(c->label_deg, sz.x, sz.y, wx_cast_sub_box(sub.x, sub.y), &x, &y);
        lv_obj_set_pos(s_clabel, x, y);
    }
    bool on = c->has_count && opa > 0 && s_shown.label_clear;
    show_obj(s_clabel, on);
    if (!on) return;
    if (opa != s_shown.label_opa) {
        s_shown.label_opa = opa;
        lv_obj_set_style_text_opa(s_clabel, (lv_opa_t)opa, 0);
    }
}

static void set_drop(const wx_cast_t *c, double ta, int fo)
{
    int opa = c->has_count ? wx_cast_drop_opa(ta) * fo / 255 : 0;
    show_obj(s_drop, opa > 0);
    if (opa <= 0) return;
    int q = (int)lroundf(wx_cast_drop_deg(c, ta) * 4);
    if (q != s_shown.drop_q) {
        s_shown.drop_q = q;
        wx_box_t b = wx_paint_drop(&s_drop_img, (float)q / 4);
        lv_obj_set_pos(s_drop, b.x0, b.y0);
        lv_obj_invalidate(s_drop);
    }
    if (opa != s_shown.drop_opa) {
        s_shown.drop_opa = opa;
        lv_obj_set_style_image_opa(s_drop, (lv_opa_t)opa, 0);
    }
}

static void set_cast(const wx_cast_t *c, int fo, int sprite_opa)
{
    int k = (int)floor(s_t * WX_CAST_FPS);
    int gate = wx_cast_gate_opa((double)k / WX_CAST_FPS);
    bool rain = fo == 255 && gate > 0 && !c->dry;
    for (int s = 0; s < s_clay.sides; s++) {
        const wx_cast_sheet_t *sh = wx_cast_sheet(c->variant, s);
        int dx = wx_cast_breathe(c->variant, s, k), opa = sprite_opa * wx_cast_gloom(c->variant, k) / 255;
        bool cloud = dx != s_shown.cast_dx[s] || opa != s_shown.cast_opa[s];
        if (dx != s_shown.cast_dx[s]) lv_obj_set_x(s_cloud[s], s_clay.cloud[s].x0 + dx);
        if (opa != s_shown.cast_opa[s]) lv_obj_set_style_image_opa(s_cloud[s], (lv_opa_t)opa, 0);
        s_shown.cast_dx[s] = dx;
        s_shown.cast_opa[s] = opa;
        show_obj(s_csheet[s], rain);
        if (rain) {
            int off = wx_cast_sheet_off(sh, c->snow, k);
            if (off != s_shown.cast_off[s]) {
                s_shown.cast_off[s] = off;
                lv_obj_set_y(s_cstrip[s][0], off - 2 * sh->h);
                lv_obj_set_y(s_cstrip[s][1], off);
            }
            if (gate != s_shown.cast_gate) {
                lv_obj_set_style_image_opa(s_cstrip[s][0], (lv_opa_t)gate, 0);
                lv_obj_set_style_image_opa(s_cstrip[s][1], (lv_opa_t)gate, 0);
            }
        }
        if (cloud) lv_obj_invalidate(s_cbox[s]);
    }
    if (rain) s_shown.cast_gate = gate;
}

static void set_wind(const wx_wind_t *w, uint32_t rgb, int fo, int opa)
{
    wx_wind_frame_t f;
    wx_wind_frame(w->level, w->have && fo == 255 ? (int)floor(s_t * WX_WIND_FPS) : -1, &f);
    bool dirty = false;
    if (f.pose != s_shown.wind_pose) {
        wx_box_t b = s_wlay.sock;
        s_shown.wind_pose = f.pose;
        lv_canvas_set_buffer(s_wsock, s_buf.scene + s_wlay.fill[f.pose], b.x1 - b.x0, b.y1 - b.y0, LV_COLOR_FORMAT_A8);
        lv_canvas_set_buffer(s_wdark, s_buf.scene + s_wlay.dark[f.pose], b.x1 - b.x0, b.y1 - b.y0, LV_COLOR_FORMAT_A8);
        dirty = true;
    }
    if (rgb != s_shown.sock_rgb) {
        s_shown.sock_rgb = rgb;
        lv_obj_set_style_image_recolor(s_wsock, lv_color_hex(rgb), 0);
        lv_obj_set_style_image_recolor(s_wdark, lv_color_hex(wx_wind_dark(rgb)), 0);
        dirty = true;
    }
    if (opa != s_shown.sock_opa) {
        s_shown.sock_opa = opa;
        lv_obj_set_style_image_opa(s_wsock, (lv_opa_t)opa, 0);
        lv_obj_set_style_image_opa(s_wdark, (lv_opa_t)opa, 0);
        dirty = true;
    }
    for (int r = 0; r < s_wlay.nrows; r++) {
        int o = f.streak_opa[r] * opa / 255;
        bool on = o > 0;
        dirty |= on != (s_shown.streak_opa[r] > 0);
        show_obj(s_wstreak[r], on);
        if (!on) {
            s_shown.streak_opa[r] = 0;
            continue;
        }
        if (f.streak_dx[r] != s_shown.streak_dx[r]) {
            s_shown.streak_dx[r] = f.streak_dx[r];
            lv_obj_set_x(s_wstreak[r], wx_wind_streak_x(w->level, r) + f.streak_dx[r]);
            dirty = true;
        }
        if (o != s_shown.streak_opa[r]) {
            s_shown.streak_opa[r] = o;
            lv_obj_set_style_image_opa(s_wstreak[r], (lv_opa_t)o, 0);
            dirty = true;
        }
    }
    for (int i = 0; i < s_wlay.nleaves && i < WX_WIND_LEAVES; i++) {
        int o = f.leaf_opa[i] * opa / 255;
        bool on = o > 0;
        dirty |= on != (s_shown.leaf_opa[i] > 0);
        show_obj(s_wleaf[i], on);
        if (!on) {
            s_shown.leaf_opa[i] = 0;
            continue;
        }
        if (f.leaf_rot[i] != s_shown.leaf_rot[i]) {
            s_shown.leaf_rot[i] = f.leaf_rot[i];
            lv_canvas_set_buffer(s_wleaf[i], s_buf.scene + s_wlay.leaf[f.leaf_rot[i]], WX_LEAF_BOX, WX_LEAF_BOX, LV_COLOR_FORMAT_A8);
            dirty = true;
        }
        if (f.leaf_x[i] != s_shown.leaf_x[i] || f.leaf_y[i] != s_shown.leaf_y[i]) {
            s_shown.leaf_x[i] = f.leaf_x[i];
            s_shown.leaf_y[i] = f.leaf_y[i];
            lv_obj_set_pos(s_wleaf[i], f.leaf_x[i], f.leaf_y[i]);
            dirty = true;
        }
        if (o != s_shown.leaf_opa[i]) {
            s_shown.leaf_opa[i] = o;
            lv_obj_set_style_image_opa(s_wleaf[i], (lv_opa_t)o, 0);
            dirty = true;
        }
    }
    if (dirty) lv_obj_invalidate(s_wbox);
}

static void set_days(const wx_days_t *d, double ta, int fo)
{
    bool dirty = false;
    wx_days_frame_t f;
    wx_days_frame(d, ta, &f);
    for (int i = 0; i < s_dlay.n; i++) {
        int opa = f.opa[i] * fo / 255, dy = f.dy[i], lo = f.lo[i], hi = f.hi[i];
        if (opa != s_shown.day_opa[i]) {
            s_shown.day_opa[i] = opa;
            lv_obj_set_style_opa(s_drow[i], (lv_opa_t)opa, 0);
            dirty = true;
        }
        if (dy != s_shown.day_dy[i]) {
            s_shown.day_dy[i] = dy;
            lv_obj_set_y(s_drow[i], wx_days_row_box(&s_dlay, i, dy).y0);
            dirty = true;
        }
        if (lo != s_shown.day_lo[i]) {
            s_shown.day_lo[i] = lo;
            end_label(s_dlo[i], s_dlo_txt[i], sizeof s_dlo_txt[i], true, lo);
            dirty = true;
        }
        if (hi != s_shown.day_hi[i]) {
            s_shown.day_hi[i] = hi;
            end_label(s_dhi[i], s_dhi_txt[i], sizeof s_dhi_txt[i], true, hi);
            dirty = true;
        }
    }
    if (dirty) lv_obj_invalidate(s_dbox);
}

static void set_sun(const wx_sun_t *sn, double ta, int fo)
{
    wx_sun_frame_t f;
    wx_sun_frame(ta, &f);
    bool hero = wx_sun_hero(sn), dirty = false;
    show_obj(s_sbox, hero);
    if (f.dy != s_shown.sun_dy) {
        s_shown.sun_dy = f.dy;
        lv_obj_set_y(s_ssun, f.dy);
        dirty = true;
    }
    if (fo != s_shown.sun_opa) {
        s_shown.sun_opa = fo;
        lv_obj_set_style_image_opa(s_ssun, (lv_opa_t)fo, 0);
        dirty = true;
    }
    int rays = hero ? f.rays_opa * fo / 255 : 0;
    show_obj(s_srays, rays > 0);
    if (rays != s_shown.rays_opa) {
        s_shown.rays_opa = rays;
        lv_obj_set_style_image_opa(s_srays, (lv_opa_t)rays, 0);
    }
    if (dirty) lv_obj_invalidate(s_sbox);
}

static void set_air(const wx_air_t *aq, int fo)
{
    wx_petal_t p;
    wx_air_petal(aq->wilted && fo == 255 ? (int)floor(s_t * WX_AIR_FPS) : -1, &p);
    wx_box_t b = wx_air_petal_box();
    bool on = p.opa > 0, dirty = on != (s_shown.petal_opa > 0);
    show_obj(s_apetal, on);
    if (!on) {
        s_shown.petal_opa = 0;
    } else {
        if (p.rot != s_shown.petal_rot) {
            s_shown.petal_rot = p.rot;
            lv_canvas_set_buffer(s_apetal, s_buf.scene + p.rot * WX_PETAL_BOX * WX_PETAL_BOX * 3, WX_PETAL_BOX, WX_PETAL_BOX,
                                 LV_COLOR_FORMAT_RGB565A8);
            dirty = true;
        }
        if (p.x != s_shown.petal_x || p.y != s_shown.petal_y) {
            s_shown.petal_x = p.x;
            s_shown.petal_y = p.y;
            lv_obj_set_pos(s_apetal, p.x - b.x0, p.y - b.y0);
            dirty = true;
        }
        if (p.opa != s_shown.petal_opa) {
            s_shown.petal_opa = p.opa;
            lv_obj_set_style_image_opa(s_apetal, (lv_opa_t)p.opa, 0);
            dirty = true;
        }
    }
    if (dirty) lv_obj_invalidate(s_abox);
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

static void set_sprites(int opa, bool drift)
{
    if (opa != s_shown.sprite_opa) {
        s_shown.sprite_opa = opa;
        lv_obj_set_style_image_opa(s_hero, (lv_opa_t)opa, 0);
        for (int i = 0; i < WX_CLOUDS_MAX; i++) lv_obj_set_style_image_opa(s_cl[i], (lv_opa_t)opa, 0);
    }
    int i = drift ? wx_drift_step(&s_cd) : -1;
    if (i < 0) return;
    lv_obj_set_pos(s_cl[i], s_out.cloud[i].x0 + s_cd.dx[i], s_out.cloud[i].y0 + s_cd.dy[i]);
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
            lv_canvas_set_buffer(s_drip[i], s_buf.scene + s_lay.drip[op.drip_pose[i]], WX_DRIP_W, WX_DRIP_H, LV_COLOR_FORMAT_A8);
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

static void paint(const wx_temp_t *t, const wx_obs_t *obs, bool offline, int fo)
{
    set_face_opa(fo);
    double ta = floor(s_t * ARRIVE_FPS) / ARRIVE_FPS;
    double a = wx_arrive(ta);
    double tf = floor(s_t * FADE_FPS) / FADE_FPS;
    int sprite_opa = (int)lround(fo * wx_arrive(tf));
    const wx_rain_t *r = wx_rain_same_scene(&t->rain, &s_io.key.rain) ? &t->rain : &s_io.key.rain;
    const wx_cast_t *c = wx_cast_same_scene(&t->cast, &s_io.key.cast) ? &t->cast : &s_io.key.cast;
    const wx_wind_t *w = wx_wind_same_scene(&t->wind, &s_io.key.wind) ? &t->wind : &s_io.key.wind;
    const wx_sun_t *sn = wx_sun_same_scene(&t->sun, &s_io.key.sun) ? &t->sun : &s_io.key.sun;
    const wx_air_t *aq = wx_air_same_scene(&t->air, &s_io.key.air) ? &t->air : &s_io.key.air;
    char txt[20];
    if (s_air) {
        if (aq->have) {
            int v = wx_count_value(aq->aqi, wx_count_step(wx_count_n(aq->aqi), ta));
            set_unit_text("AQI");
            snprintf(txt, sizeof txt, "%d", v);
            set_value(txt, wx_air_rgb(v));
            set_unit(WX_AIR_UNIT_RGB);
        } else {
            set_unit_text(NULL);
            set_value("--", GREY_RGB);
        }
        set_marker(aq->frac * (float)a, aq->have);
        set_air(aq, fo);
    } else if (s_sun) {
        const char *unit = wx_sun_value(sn, wx_count_step(wx_sun_count_n(sn), ta), txt, sizeof txt);
        set_unit_text(unit);
        set_value(txt, unit ? WX_SUN_VALUE_RGB : GREY_RGB);
        set_unit(WX_SUN_UNIT_RGB);
        set_marker(sn->frac * (float)a, sn->have && sn->has_now);
        set_sun(sn, ta, fo);
    } else if (s_days) {
        const wx_days_t *d = &s_io.key.days;
        if (d->have) set_days(d, ta, fo);
        else set_value("--", GREY_RGB);
        place_marker(0, 0, false);
    } else if (s_wind) {
        uint32_t rgb = GREY_RGB;
        if (w->have) {
            int v = wx_count_value(w->kmh, wx_count_step(wx_count_n(w->kmh), ta));
            rgb = wx_wind_rgb(v);
            set_unit_text("km/h");
            snprintf(txt, sizeof txt, "%d", v);
            set_value(txt, rgb);
            set_unit(WX_WIND_UNIT_RGB);
        } else {
            set_unit_text(NULL);
            set_value("--", GREY_RGB);
        }
        set_marker(w->frac * (float)a, w->have);
        set_wind(w, rgb, fo, sprite_opa);
    } else if (s_cast) {
        uint32_t rgb;
        const char *unit = wx_cast_value(c, wx_count_step(wx_cast_count_n(c), ta), txt, sizeof txt, &rgb);
        set_unit_text(unit);
        set_value(txt, rgb);
        set_unit(rgb);
        float mx = 0, my = 0;
        wx_cast_ring_xy(c->now_deg, &mx, &my);
        place_marker(mx, my, c->have);
        set_drop(c, ta, fo);
        double tl = floor(s_t * FADE_FPS) / FADE_FPS;
        set_label(c, wx_cast_label_opa(tl) * fo / 255);
        set_cast(c, fo, sprite_opa);
    } else if (s_rain) {
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
    bool temp = !s_rain && !s_cast && !s_wind && !s_days && !s_sun && !s_air;
    if (temp) set_marker(t->frac * (float)a, t->have);
    set_sprites(sprite_opa, temp && s_t >= WX_ARRIVE_S);
    double sf = (s_t - WX_ARRIVE_S) / SUB_FADE_S;
    int sub_opa = (sf <= 0 ? 0 : sf >= 1 ? 255 : 85 * (1 + (int)(sf * 3))) * fo / 255;
    char age[24];
    if (offline && obs && obs->valid) {
        wx_age_label(obs, age, sizeof age);
        set_sub(age, GREY_RGB, sub_opa);
    } else {
        const char *word = aq->have ? wx_air_word(aq->band) : "";
        set_sub(s_days ? "" : s_air ? word : s_sun ? sn->sub : s_wind ? w->sub : s_cast ? c->sub : s_rain ? r->sub : t->have ? t->sub : "",
                s_air ? wx_air_band_rgb(aq->band) : s_sun ? sn->sub_rgb : s_days || s_wind || s_cast || s_rain ? SUB_RGB : t->sub_rgb,
                sub_opa);
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

void temp_face_set_list(const uint8_t *ids, int n) { wx_face_list_set(&s_faces.list, ids, n); }

void temp_face_turn(int detents)
{
    if (!s_root || !s_job.visible) return;
    if (wx_faces_turn(&s_faces, wx_job_face(&s_job), s_face, detents)) wx_job_cut(&s_job);
}

int temp_face_picked(void) { return s_faces.pick; }

int temp_face_resolved(void) { return s_root && s_job.visible ? s_face : -1; }

void temp_face_pick(int face)
{
    if (!s_root || !s_job.visible || face == s_faces.pick) return;
    s_faces.pick = face;
    if (face != WXF_AUTO && wx_job_face(&s_job) != face) wx_job_cut(&s_job);
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
        wx_temp_listed(obs, s_job.baked ? &s_io.key : NULL, s_faces.pick, &s_faces.list, &t);
        s_face = t.face;
    }
    int a = wx_job_step(&s_job, &t, dt);
    act(a);
    if (!(a & WX_DO_PAINT)) return;
    if (wx_shift_tick(&s_shift, &WX_SHIFT_SQUARE, dt)) shift_root();
    s_t += dt;
    if (s_t >= WX_ARRIVE_S) wx_drift_tick(&s_cd, dt);
    int tick = (int)floor(s_t * ARRIVE_FPS);
    if (s_t < WX_ARRIVE_S + SUB_FADE_S && tick == s_tick && s_job.opa == s_shown.face_opa) return;
    s_tick = tick;
    paint(&t, obs, offline, s_job.opa);
    show_obj(s_root, true);
}

void page_weather_show(bool on) { temp_face_show(on); }

void page_weather_input(const page_input_t *in, double t)
{
    (void)t;
    if (in->detents) temp_face_turn(in->detents);
}
