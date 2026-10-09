#include "weather_view.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bot_raster.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "weather_scene.h"

#define SKY_W WX_SKY_W
#define SKY_H WX_SKY_H
#define TEMP_H 62
#define ROOT_H (SKY_H + TEMP_H)   /* even: the panel rounds areas to 2 px */
#define PAGE_Y 96
#define OVERLAY_Y 48
#define TEMP_RGB 0x8C8C8C
#define TEMP_STILL_RGB 0x5A5A5A
#define SHIFT_PERIOD_S 120.0
#define AGE_RGB 0x5A5A5A
#define AGE_GAP 4

static lv_obj_t *s_root, *s_sky, *s_temp, *s_age;
static EXT_RAM_BSS_ATTR char s_age_txt[16];
static uint8_t *s_buf;
static uint8_t *s_mask[WX_SPR_COUNT];
static wx_scene_t s_scene;
static bool s_overlay, s_visible, s_temp_on = true;
static int s_temp_shown = 0x7FFFFFFF;
static int s_temp_still = -1;
static double s_shift_t;
static int s_shift_i;

typedef struct {
    wx_stroke_t st[WX_STROKES_MAX];
    bot_raster_scratch_t scratch;
    int segs[WX_STROKE_PTS];
    uint8_t tmp[128 * 64];
} prerender_t;

static void sprites_create(void)
{
    prerender_t *w = heap_caps_malloc(sizeof *w, MALLOC_CAP_SPIRAM);
    assert(w);
    for (int i = 0; i < WX_STROKE_PTS; i++) w->segs[i] = i;
    for (int id = 0; id < WX_SPR_COUNT; id++) {
        wx_sprite_size_t z = wx_sprite_size(id);
        int fsz = z.w * z.h;
        assert(fsz <= (int)sizeof w->tmp);
        s_mask[id] = heap_caps_calloc((size_t)fsz * z.frames, 1, MALLOC_CAP_SPIRAM);
        assert(s_mask[id]);
        for (int f = 0; f < z.frames; f++) {
            uint8_t *m = s_mask[id] + f * fsz;
            int n = wx_sprite_strokes(id, f, w->st);
            for (int k = 0; k < n; k++) {
                const wx_stroke_t *s = &w->st[k];
                bot_raster_stroke(&w->scratch, NULL, w->tmp, z.w, z.h, 0, 0, s->x, s->y, s->n - 1, w->segs, s->hw,
                                  0xFFFFFF, 1.0f);
                for (int q = 0; q < fsz; q++) if (w->tmp[q] > m[q]) m[q] = w->tmp[q];
            }
            wx_sprite_fill(id, f, m);
        }
    }
    heap_caps_free(w);
}

static inline uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xFF) >> 3));
}

static inline uint16_t mix565(uint16_t d, uint16_t s, uint32_t a)
{
    uint32_t r = ((d >> 11) * (256 - a) + (s >> 11) * a) >> 8;
    uint32_t g = (((d >> 5) & 63) * (256 - a) + ((s >> 5) & 63) * a) >> 8;
    uint32_t b = ((d & 31) * (256 - a) + (s & 31) * a) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void blit(const wx_draw_t *d)
{
    wx_sprite_size_t z = wx_sprite_size(d->sprite);
    if (!z.w || d->frame >= z.frames) return;
    int x0 = d->x < 0 ? -d->x : 0, y0 = d->y < 0 ? -d->y : 0;
    int x1 = d->x + z.w > SKY_W ? SKY_W - d->x : z.w, y1 = d->y + z.h > SKY_H ? SKY_H - d->y : z.h;
    const uint8_t *m = s_mask[d->sprite] + d->frame * z.w * z.h;
    uint16_t *cp = (uint16_t *)s_buf;
    uint8_t *ap = s_buf + SKY_W * SKY_H * 2;
    const uint16_t col = rgb565(d->rgb);
    const uint32_t ga = d->alpha;
    for (int y = y0; y < y1; y++) {
        const uint8_t *mr = m + y * z.w;
        int o = (d->y + y) * SKY_W + d->x;
        for (int x = x0; x < x1; x++) {
            uint32_t a = (mr[x] * ga + 255) >> 8;
            if (!a) continue;
            uint32_t a256 = a + (a >> 7);
            if (!s_overlay) {
                cp[o + x] = a >= 255 ? col : mix565(cp[o + x], col, a256);
            } else {
                uint32_t da = ap[o + x];
                if (!da) {
                    cp[o + x] = col;
                    ap[o + x] = (uint8_t)a;
                } else {
                    cp[o + x] = mix565(cp[o + x], col, a256);
                    ap[o + x] = (uint8_t)(a + ((da * (255 - a) + 127) / 255));
                }
            }
        }
    }
}

static void render(void)
{
    static wx_draw_t list[WX_MAX_DRAW];
    int n = wx_scene_draw(&s_scene, list, WX_MAX_DRAW);
    if (s_overlay) memset(s_buf + SKY_W * SKY_H * 2, 0, SKY_W * SKY_H);
    else memset(s_buf, 0, SKY_W * SKY_H * 2);
    for (int i = 0; i < n; i++) blit(&list[i]);
    lv_canvas_set_buffer(s_sky, s_buf, SKY_W, SKY_H, s_overlay ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565);
    lv_obj_invalidate(s_sky);
}

lv_obj_t *weather_view_create(lv_obj_t *parent)
{
    if (s_root) return s_root;
    sprites_create();
    s_buf = heap_caps_calloc(SKY_W * SKY_H * 3, 1, MALLOC_CAP_SPIRAM);
    assert(s_buf);
    wx_scene_init(&s_scene, 0x57454154u );

    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SKY_W, ROOT_H);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_sky = lv_canvas_create(s_root);
    lv_obj_remove_flag(s_sky, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(s_sky, s_buf, SKY_W, SKY_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_sky, 0, 0);

    s_temp = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_temp, lv_color_hex(TEMP_RGB), 0);
    lv_obj_set_style_text_align(s_temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_temp, SKY_W);
    lv_obj_set_pos(s_temp, 0, SKY_H);
    lv_label_set_text(s_temp, "--\xC2\xB0");

    s_age = lv_label_create(parent);
    lv_obj_set_style_text_font(s_age, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_age, lv_color_hex(AGE_RGB), 0);
    lv_obj_set_style_text_align(s_age, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_age, SKY_W);
    lv_obj_align(s_age, LV_ALIGN_TOP_MID, 0, PAGE_Y + ROOT_H + AGE_GAP);
    lv_obj_remove_flag(s_age, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text_static(s_age, s_age_txt);
    lv_obj_add_flag(s_age, LV_OBJ_FLAG_HIDDEN);

    weather_view_set_mode(WEATHER_VIEW_PAGE);
    return s_root;
}

void weather_view_set_mode(weather_view_mode_t mode)
{
    if (!s_root) return;
    s_overlay = mode == WEATHER_VIEW_OVERLAY;
    lv_obj_align(s_root, LV_ALIGN_TOP_MID, 0, s_overlay ? OVERLAY_Y : PAGE_Y);
    weather_view_set_temp_visible(!s_overlay);
    if (s_overlay) memset(s_buf + SKY_W * SKY_H * 2, 0, SKY_W * SKY_H);
    wx_scene_invalidate(&s_scene);
}

void weather_view_set_temp_visible(bool on)
{
    s_temp_on = on;
    if (!s_temp) return;
    if (on) lv_obj_remove_flag(s_temp, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_temp, LV_OBJ_FLAG_HIDDEN);
}

void weather_view_show(bool on)
{
    if (!s_root || on == s_visible) return;
    s_visible = on;
    if (on) {
        lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        wx_scene_invalidate(&s_scene);
    } else {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_age, LV_OBJ_FLAG_HIDDEN);
    }
}

bool weather_view_visible(void) { return s_visible; }

wx_look_t weather_view_look(void) { return s_scene.look; }

bool weather_view_flash(void) { return wx_scene_flash(&s_scene); }

static void temp_update(const wx_obs_t *obs, bool still)
{
    const int none = -0x7FFF;
    int deg = obs && obs->valid && obs->has_temp ? (int)lroundf(obs->temp_c) : none;
    if (deg != s_temp_shown) {
        char txt[16];
        if (deg == none) snprintf(txt, sizeof txt, "--\xC2\xB0");
        else snprintf(txt, sizeof txt, "%d\xC2\xB0", deg);
        lv_label_set_text(s_temp, txt);
        s_temp_shown = deg;
    }
    if ((int)still != s_temp_still) {
        lv_obj_set_style_text_color(s_temp, lv_color_hex(still ? TEMP_STILL_RGB : TEMP_RGB), 0);
        s_temp_still = still;
    }
}

static void shift_update(double dt)
{
    static const int8_t DX[4] = {0, 2, 2, 0}, DY[4] = {0, 0, 2, 2};
    s_shift_t += dt;
    if (s_shift_t < SHIFT_PERIOD_S) return;
    s_shift_t = 0;
    s_shift_i = (s_shift_i + 1) & 3;
    lv_obj_set_style_translate_x(s_root, DX[s_shift_i], 0);
    lv_obj_set_style_translate_y(s_root, DY[s_shift_i], 0);
}

static void age_update(const wx_obs_t *obs, bool offline)
{
    bool show = offline && !s_overlay && obs && obs->valid;
    if (show) {
        char txt[sizeof s_age_txt];
        wx_age_text(obs->age_s, txt, sizeof txt);
        if (strcmp(txt, s_age_txt) != 0) {
            strlcpy(s_age_txt, txt, sizeof s_age_txt);
            lv_label_set_text_static(s_age, s_age_txt);
        }
    }
    if (show == lv_obj_has_flag(s_age, LV_OBJ_FLAG_HIDDEN)) {
        if (show) lv_obj_remove_flag(s_age, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_age, LV_OBJ_FLAG_HIDDEN);
    }
}

void weather_view_update(const wx_obs_t *obs, bool offline, double dt)
{
    if (!s_root || !s_visible) return;
    wx_look_t look = wx_look_from_obs(obs);
    wx_scene_set_look(&s_scene, &look);
    if (s_temp_on) temp_update(obs, look.still);
    age_update(obs, offline);
    shift_update(dt);
    if (wx_scene_tick(&s_scene, dt)) render();
}
