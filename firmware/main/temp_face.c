#include "temp_face.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
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

static lv_obj_t *s_root, *s_bg, *s_hero, *s_clouds, *s_thermo, *s_marker, *s_value, *s_lo, *s_hi, *s_sub;
static EXT_RAM_BSS_ATTR wx_img_t s_bg_img, s_hero_img, s_cloud_img, s_thermo_img;
static uint8_t *s_cloud_buf;
static uint16_t *s_lut;
static EXT_RAM_BSS_ATTR wx_temp_t s_cur;
static EXT_RAM_BSS_ATTR wx_box_t s_cloud_box;
static bool s_baked, s_visible;
static EXT_RAM_BSS_ATTR double s_t;
static EXT_RAM_BSS_ATTR int s_tick;

typedef struct {
    char value[20], sub[24];
    uint32_t value_rgb, sub_rgb;
    int marker_x, marker_y, merc_q, sprite_opa, sub_opa, drift;
    uint32_t merc_rgb;
} shown_t;

static EXT_RAM_BSS_ATTR shown_t s_shown;
static EXT_RAM_BSS_ATTR char s_lo_txt[20], s_hi_txt[20];

static uint8_t *alloc3(int px)
{
    uint8_t *b = heap_caps_calloc((size_t)px, 3, MALLOC_CAP_SPIRAM);
    assert(b);
    return b;
}

static void sprite_img(wx_img_t *im, uint8_t *buf, int w, int h)
{
    *im = (wx_img_t){.px = (uint16_t *)buf, .a = buf + (size_t)w * h * 2, .w = w, .h = h};
}

static lv_obj_t *canvas(lv_obj_t *parent, void *buf, int w, int h, lv_color_format_t cf)
{
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(c, buf, w, h, cf);
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

static int max_cloud_px(void)
{
    int m = 0;
    for (int b = 1; b <= 4; b++) {
        wx_clouds_t c;
        wx_clouds_for(b, false, &c);
        wx_box_t x = wx_clouds_box(&c);
        int px = (x.x1 - x.x0) * (x.y1 - x.y0);
        if (px > m) m = px;
    }
    return m;
}

void temp_face_create(lv_obj_t *parent)
{
    if (s_root) return;
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, WX_FACE, WX_FACE);
    lv_obj_set_pos(s_root, FACE_X, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_bg_img = (wx_img_t){.px = heap_caps_calloc(WX_FACE * WX_FACE, 2, MALLOC_CAP_SPIRAM), .w = WX_FACE, .h = WX_FACE};
    s_lut = heap_caps_malloc(WX_RING_LUT * sizeof *s_lut, MALLOC_CAP_SPIRAM);
    assert(s_bg_img.px && s_lut);
    uint8_t *hero = alloc3(WX_HERO_BOX * WX_HERO_BOX), *thermo = alloc3(WX_THERMO_W * WX_THERMO_H),
            *marker = alloc3(WX_MARKER * WX_MARKER);
    s_cloud_buf = alloc3(max_cloud_px());
    sprite_img(&s_hero_img, hero, WX_HERO_BOX, WX_HERO_BOX);
    sprite_img(&s_thermo_img, thermo, WX_THERMO_W, WX_THERMO_H);
    wx_img_t mk;
    sprite_img(&mk, marker, WX_MARKER, WX_MARKER);
    wx_paint_marker(&mk);

    s_bg = canvas(s_root, s_bg_img.px, WX_FACE, WX_FACE, LV_COLOR_FORMAT_RGB565);
    s_hero = canvas(s_root, hero, WX_HERO_BOX, WX_HERO_BOX, LV_COLOR_FORMAT_RGB565A8);
    s_clouds = canvas(s_root, s_cloud_buf, 2, 2, LV_COLOR_FORMAT_RGB565A8);
    s_thermo = canvas(s_root, thermo, WX_THERMO_W, WX_THERMO_H, LV_COLOR_FORMAT_RGB565A8);
    lv_obj_set_pos(s_thermo, WX_THERMO_X, WX_THERMO_Y);
    lv_obj_add_flag(s_hero, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_clouds, LV_OBJ_FLAG_HIDDEN);

    int deg = lv_font_get_glyph_width(&font_digits_96, 0x00B0, 0) + VALUE_SPACE;
    s_value = label(s_root, &font_digits_96, LV_ALIGN_TOP_MID, deg / 2, VALUE_Y);
    lv_obj_set_style_text_letter_space(s_value, VALUE_SPACE, 0);
    s_lo = label(s_root, &font_face_36, LV_ALIGN_TOP_LEFT, END_X, END_Y);
    s_hi = label(s_root, &font_face_36, LV_ALIGN_TOP_RIGHT, -END_X, END_Y);
    s_sub = label(s_root, &lv_font_montserrat_30, LV_ALIGN_TOP_MID, 0, SUB_Y);
    s_marker = canvas(s_root, marker, WX_MARKER, WX_MARKER, LV_COLOR_FORMAT_RGB565A8);
    memset(&s_shown, 0xFF, sizeof s_shown);
    s_shown.value[0] = s_shown.sub[0] = 0;
}

static void deg_text(char *buf, size_t n, int v)
{
    if (v < 0) snprintf(buf, n, "\xE2\x88\x92%d\xC2\xB0", -v);
    else snprintf(buf, n, "%d\xC2\xB0", v);
}

static void show_obj(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void end_label(lv_obj_t *l, char *buf, size_t n, bool on, int v)
{
    show_obj(l, on);
    if (!on) return;
    deg_text(buf, n, v);
    lv_label_set_text_static(l, buf);
    lv_obj_set_style_text_color(l, lv_color_hex(wx_temp_rgb((float)v)), 0);
}

static void bake(const wx_temp_t *t)
{
    int64_t t0 = esp_timer_get_time();
    wx_paint_clear(&s_bg_img);
    if (t->hero == WX_HERO_SUN) wx_paint_rays(&s_bg_img);
    if (t->stars) wx_paint_stars(&s_bg_img);
    wx_paint_thermo(&s_bg_img);
    wx_paint_ring(&s_bg_img, t->lo, t->hi, !t->have, s_lut);
    lv_obj_invalidate(s_bg);

    if (t->hero != WX_HERO_NONE) {
        wx_box_t b = wx_paint_hero(&s_hero_img, t->hero, t->moon_phase, t->moon_illum);
        lv_obj_set_pos(s_hero, b.x0, b.y0);
        lv_obj_invalidate(s_hero);
    }
    show_obj(s_hero, t->hero != WX_HERO_NONE);

    wx_clouds_t c;
    wx_clouds_for(t->cloud_band, t->night, &c);
    s_cloud_box = wx_clouds_box(&c);
    int w = s_cloud_box.x1 - s_cloud_box.x0, h = s_cloud_box.y1 - s_cloud_box.y0;
    if (c.n && w > 0 && h > 0) {
        sprite_img(&s_cloud_img, s_cloud_buf, w, h);
        s_cloud_img.x0 = s_cloud_box.x0;
        s_cloud_img.y0 = s_cloud_box.y0;
        wx_paint_clouds(&s_cloud_img, &c);
        lv_canvas_set_buffer(s_clouds, s_cloud_buf, w, h, LV_COLOR_FORMAT_RGB565A8);
        lv_obj_set_pos(s_clouds, s_cloud_box.x0, s_cloud_box.y0);
        lv_obj_invalidate(s_clouds);
    }
    show_obj(s_clouds, c.n > 0);

    end_label(s_lo, s_lo_txt, sizeof s_lo_txt, t->have && t->has_range, t->lo);
    end_label(s_hi, s_hi_txt, sizeof s_hi_txt, t->have && t->has_range, t->hi);
    s_shown.drift = 0x7FFFFFFF;
    ESP_LOGI(TAG, "baked %d..%d hero %d clouds %d in %d ms", t->lo, t->hi, (int)t->hero, t->cloud_band,
             (int)((esp_timer_get_time() - t0) / 1000));
}

static void set_value(const char *txt, uint32_t rgb)
{
    if (strcmp(txt, s_shown.value) != 0) {
        strlcpy(s_shown.value, txt, sizeof s_shown.value);
        lv_label_set_text_static(s_value, s_shown.value);
    }
    if (rgb != s_shown.value_rgb) {
        s_shown.value_rgb = rgb;
        lv_obj_set_style_text_color(s_value, lv_color_hex(rgb), 0);
    }
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

static void set_sprites(int opa, int drift)
{
    if (opa != s_shown.sprite_opa) {
        s_shown.sprite_opa = opa;
        lv_obj_set_style_image_opa(s_hero, (lv_opa_t)opa, 0);
        lv_obj_set_style_image_opa(s_clouds, (lv_opa_t)opa, 0);
    }
    if (drift != s_shown.drift) {
        s_shown.drift = drift;
        lv_obj_set_x(s_clouds, s_cloud_box.x0 + drift);
    }
}

static int drift_px(double t)
{
    double p = fmod(t, DRIFT_PERIOD_S) / DRIFT_PERIOD_S;
    double tri = 1 - 4 * fabs(p - 0.5);
    return 2 * (int)lround(tri * 2);
}

static void paint(const wx_temp_t *t, const wx_obs_t *obs, bool offline)
{
    double ta = floor(s_t * ARRIVE_FPS) / ARRIVE_FPS;
    double a = wx_arrive(ta);
    char txt[20];
    if (t->have) {
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
    set_marker(t->frac * (float)a, t->have);
    double tf = floor(s_t * FADE_FPS) / FADE_FPS;
    set_sprites((int)lround(255 * wx_arrive(tf)), s_t >= WX_ARRIVE_S ? drift_px(s_t - WX_ARRIVE_S) : 0);
    double sf = (s_t - WX_ARRIVE_S) / SUB_FADE_S;
    int sub_opa = sf <= 0 ? 0 : sf >= 1 ? 255 : 85 * (1 + (int)(sf * 3));
    char age[24];
    if (offline && obs && obs->valid) {
        wx_age_label(obs, age, sizeof age);
        set_sub(age, GREY_RGB, sub_opa);
    } else {
        set_sub(t->have ? t->sub : "", t->sub_rgb, sub_opa);
    }
}

void temp_face_show(bool on)
{
    if (!s_root || on == s_visible) return;
    s_visible = on;
    if (!on) show_obj(s_root, false);
    s_t = 0;
    s_tick = -1;
}

void temp_face_update(const wx_obs_t *obs, bool offline, double dt)
{
    if (!s_root || !s_visible) return;
    wx_temp_t t;
    wx_temp_from(obs, &t);
    if (!s_baked || !wx_temp_same_bake(&t, &s_cur)) {
        bake(&t);
        s_baked = true;
    }
    s_cur = t;
    s_t += dt;
    int tick = (int)floor(s_t * ARRIVE_FPS);
    if (s_t < WX_ARRIVE_S + SUB_FADE_S && tick == s_tick) return;
    s_tick = tick;
    paint(&t, obs, offline);
    show_obj(s_root, true);
}
