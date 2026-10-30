#include "bot_view.h"

#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "arc_text.h"
#include "label_wipe.h"
#include "offline_glyph.h"
#include "bot_raster.h"
#include "bot_shape.h"
#include "rim_slots.h"
#include "ring_glint.h"
#include "timing.h"
#include "tool_marks.h"
#include "usage.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Driver frame 472 x 466; the visible circle is 466 across (docs/llm.md). */
#define SCREEN_W 472
#define SCREEN_H 466
#define CX 236.0
#define CY 233.0
#define FILL 0.84
#define HOP_SCALE 0.45
#define RIM_PX 9.0

#define EYE_BUF_W 220
#define EYE_BUF_H 260

typedef struct {
    lv_obj_t *canvas;
    uint16_t *buf;
    int w, h;
    int x, y;
} canvas_t;

static canvas_t s_eye[2];
static SemaphoreHandle_t s_eye_go, s_eye_done;
static void eye_task(void *arg);
static lv_obj_t *s_rim_canvas;
static rim_slots_t s_slots;
static TaskHandle_t s_rim_task;
static int s_rim_want = -1, s_rim_face = -1;
static float s_rim_ratio = 1.0f;
static int s_rim_pose_dy;
static int s_rim_shape = -1;
static bot_mood_t s_rim_mood = (bot_mood_t)-1;
static int s_rim_dy;
static lv_obj_t *s_badge_gap, *s_badge_dot;
#define LABEL_W BOT_VIEW_LABEL_W
#define LABEL_H BOT_VIEW_LABEL_H
#define LABEL_X ((int)CX - LABEL_W / 2)
#define LABEL_Y BOT_VIEW_LABEL_Y
#define LABEL_R 176.0f
#define LABEL_ICON_CELL 4
#define LABEL_ICON_MAX_W TOOL_MARK_MAX_W
#define LABEL_ICON_H TOOL_MARK_H
#define LABEL_ICON_R 146
#define LABEL_ICON_SUB_R 128
#define LABEL_SUB_R 147.0f
static lv_obj_t *s_label;
static uint16_t *s_label_buf;
static uint32_t *s_label_scratch;
static int s_label_cx, s_label_cy;
static int s_label_sx, s_label_sy;
static int s_label_opa = 255;
static uint32_t *s_icon_buf;
static lv_area_t s_icon_box = {0, 0, -1, -1};
static bool s_icon_off;
static ember_host_info_t s_host = {.color = -1};
static bool s_label_dirty = true;
static int s_label_mood = -1;
static bool s_opt_label = true, s_opt_glint = true;
#define LABEL_WIPE_R 150.0
static uint8_t *s_label_alpha0;
static uint16_t *s_label_bin;
static int s_label_w, s_label_h;
typedef struct {
    int16_t x0, y0, x1, y1;
} bin_box_t;
static EXT_RAM_BSS_ATTR bin_box_t s_bin_box[LW_BINS];
static label_wipe_t s_wipe;
static bool s_merge, s_wipe_on;
static double s_r;

#define GLINT_HW 6.0f
#define GLINT_TAIL_DEG 40.0f
#define GLINT_MIX 0.85f
#define GLINT_PERIOD_S 3.0
#define GLINT_FPS 15
#define GLINT_STEP_S (1.0 / GLINT_FPS)
#define GLINT_STEPS 45
#define GLINT_PIECES 1
#define GLINT_MAX_DEFER 4
typedef struct {
    ring_glint_box_t box;
    uint8_t *mask;
} glint_step_t;
static lv_obj_t *s_glint;
static glint_step_t *s_glint_cache;
static lv_image_dsc_t s_glint_img;
static int s_glint_idx = -1;
static int s_glint_dy;
static int s_glint_defer;
static void glint_draw_cb(lv_event_t *e);

static EXT_RAM_BSS_ATTR struct {
    uint8_t *mask;
    lv_image_dsc_t img;
    int x, y, w, h, dy;
    bool want, on;
} s_hint;

#define OFFLINE_RGB 0x5A5A5A
#define AGE_W 140
static lv_obj_t *s_offline;
static EXT_RAM_BSS_ATTR lv_image_dsc_t s_offline_img;
static EXT_RAM_BSS_ATTR uint8_t s_offline_a8[OFFLINE_GLYPH_W * OFFLINE_GLYPH_H];
static bool s_offline_on;
static lv_obj_t *s_age;
static char s_age_txt[16];

static uint32_t mood_rgb(bot_mood_t m)
{
    switch (m) {
    case BOT_WORKING: return 0x2EE85E;
    case BOT_WAITING: return 0xFFC14D;
    case BOT_ERROR:   return 0xFF3A3A;
    case BOT_DONE:    return 0x4FA9FF;
    default:          return 0x888888;
    }
}

static lv_obj_t *circle_create(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, color, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    return c;
}

static lv_obj_t *canvas_create(lv_obj_t *parent)
{
    lv_obj_t *c = lv_canvas_create(parent);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static void to_screen(const bot_pose_t *p, double x, double y, double *sx, double *sy)
{
    double by = p->offset_y * HOP_SCALE - (1 - p->scale_y) + p->scale_y * y;
    *sx = CX + s_r * (p->offset_x + p->scale_x * x);
    *sy = CY - s_r * by;
}

typedef struct {
    double bx[BOT_RING_POINTS], by[BOT_RING_POINTS];
    float rx[BOT_RING_POINTS + 1], ry[BOT_RING_POINTS + 1];
    int segs[BOT_RING_POINTS];
    bot_raster_scratch_t scratch;
} rim_work_t;

static void rim_raster(uint16_t *buf, int shape, int tone, float sx, float sy, rim_work_t *wk)
{
    double *bx = wk->bx, *by = wk->by;
    float *rx = wk->rx, *ry = wk->ry;
    int *segs = wk->segs;
    int64_t t0 = timing_now();
    bot_pose_t pose = {.scale_x = sx, .scale_y = sy};
    bot_body_ring(shape, bx, by);
    for (int i = 0; i < BOT_RING_POINTS; i++) {
        double px, py;
        to_screen(&pose, bx[i], by[i], &px, &py);
        rx[i] = (float)px;
        ry[i] = (float)py;
        segs[i] = i;
    }
    rx[BOT_RING_POINTS] = rx[0];
    ry[BOT_RING_POINTS] = ry[0];
    bot_raster_stroke(&wk->scratch, buf, NULL, SCREEN_W, SCREEN_H, 0, 0, rx, ry, BOT_RING_POINTS, segs,
                      (float)(RIM_PX / 2), mood_rgb(bot_rim_mood(tone)), bot_rim_alpha(tone));
    timing_add(SPAN_RIM, timing_now() - t0);
}

static void rim_raster_cb(void *ctx, void *buf, int shape, int tone, float sx, float sy) { rim_raster(buf, shape, tone, sx, sy, ctx); }

static void rim_yield(void *ctx)
{
    (void)ctx;
    vTaskDelay(1);
}

static void rim_task(void *arg)
{
    (void)arg;
    static EXT_RAM_BSS_ATTR rim_work_t work;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        rim_slots_run(&s_slots, rim_raster_cb, rim_yield, &work);
    }
}

void bot_view_create(lv_obj_t *parent)
{
    s_r = 233.0 * FILL;
    bot_shape_init();
    void *bufs[RIM_SLOTS];
    for (int v = 0; v < RIM_SLOTS; v++) {
        bufs[v] = heap_caps_malloc(SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
        assert(bufs[v]);
    }
    rim_slots_init(&s_slots, bufs);
    s_rim_canvas = canvas_create(parent);
    lv_obj_add_flag(s_rim_canvas, LV_OBJ_FLAG_HIDDEN);
    s_glint_cache = heap_caps_calloc(GLINT_STEPS, sizeof *s_glint_cache, MALLOC_CAP_SPIRAM);
    assert(s_glint_cache);
    s_glint = lv_obj_create(parent);
    lv_obj_remove_style_all(s_glint);
    lv_obj_set_size(s_glint, SCREEN_W, SCREEN_H);
    lv_obj_remove_flag(s_glint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_glint, glint_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    s_badge_gap = circle_create(parent, lv_color_black());
    s_badge_dot = circle_create(parent, lv_color_hex(mood_rgb(BOT_IDLE)));
    for (int e = 0; e < 2; e++) {
        s_eye[e].buf = heap_caps_malloc(EYE_BUF_W * EYE_BUF_H * 3, MALLOC_CAP_SPIRAM);
        assert(s_eye[e].buf);
        s_eye[e].canvas = canvas_create(parent);
    }
    s_label_buf = heap_caps_malloc(LABEL_W * LABEL_H * 3, MALLOC_CAP_SPIRAM);
    s_label_scratch = heap_caps_malloc(LABEL_W * LABEL_H * 4, MALLOC_CAP_SPIRAM);
    s_label_alpha0 = heap_caps_malloc(LABEL_W * LABEL_H, MALLOC_CAP_SPIRAM);
    s_label_bin = heap_caps_malloc(LABEL_W * LABEL_H * sizeof *s_label_bin, MALLOC_CAP_SPIRAM);
    assert(s_label_scratch && s_label_alpha0 && s_label_bin);
    s_icon_buf = heap_caps_malloc(LABEL_ICON_MAX_W * LABEL_ICON_H * 4, MALLOC_CAP_SPIRAM);
    assert(s_label_buf && s_icon_buf);
    s_label = canvas_create(parent);
    lv_obj_add_flag(s_label, LV_OBJ_FLAG_HIDDEN);
    offline_glyph_raster(s_offline_a8);
    s_offline_img = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = OFFLINE_GLYPH_W, .h = OFFLINE_GLYPH_H,
                   .stride = OFFLINE_GLYPH_W},
        .data_size = OFFLINE_GLYPH_W * OFFLINE_GLYPH_H,
        .data = s_offline_a8,
    };
    s_offline = lv_image_create(parent);
    lv_image_set_src(s_offline, &s_offline_img);
    lv_obj_set_style_image_recolor(s_offline, lv_color_hex(OFFLINE_RGB), 0);
    lv_obj_set_pos(s_offline, (int)CX - OFFLINE_GLYPH_W / 2, (int)CY + BOT_VIEW_OFFLINE_DY - OFFLINE_GLYPH_H / 2);
    lv_obj_remove_flag(s_offline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_offline, LV_OBJ_FLAG_HIDDEN);
    s_age = lv_label_create(parent);
    lv_obj_set_style_text_font(s_age, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_age, lv_color_hex(OFFLINE_RGB), 0);
    lv_obj_set_style_text_align(s_age, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(s_age, AGE_W, lv_font_get_line_height(&lv_font_montserrat_14));
    lv_label_set_long_mode(s_age, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_pos(s_age, (int)CX - AGE_W / 2, (int)CY + BOT_VIEW_OFFLINE_DY + OFFLINE_GLYPH_H / 2 + 2);
    lv_obj_remove_flag(s_age, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text_static(s_age, s_age_txt);
    lv_obj_add_flag(s_age, LV_OBJ_FLAG_HIDDEN);
    if (xTaskCreatePinnedToCoreWithCaps(rim_task, "rim", 2560, NULL, 2, &s_rim_task, 1, MALLOC_CAP_SPIRAM) != pdPASS)
        esp_system_abort("bot_view: rim task not created");
    s_eye_go = xSemaphoreCreateBinary();
    s_eye_done = xSemaphoreCreateBinary();
    assert(s_eye_go && s_eye_done);
    if (xTaskCreatePinnedToCore(eye_task, "eye", 2560, NULL, 5, NULL, 1) != pdPASS)
        esp_system_abort("bot_view: eye raster task not created");
}

double bot_view_radius_px(void) { return s_r; }

static uint32_t argb(uint32_t rgb) { return 0xFF000000u | rgb; }

static int s_icon_w, s_icon_h;

static void icon_fill_mark(const uint8_t *mask, int w, int h, uint32_t rgb)
{
    s_icon_w = w;
    s_icon_h = h;
    for (int i = 0; i < w * h; i++) s_icon_buf[i] = ((uint32_t)mask[i] << 24) | rgb;
}

static void icon_fill(const uint8_t body[8], const uint8_t feat[8], uint32_t body_rgb, uint32_t feat_rgb)
{
    s_icon_w = s_icon_h = 8 * LABEL_ICON_CELL;
    for (int y = 0; y < s_icon_h; y++) {
        for (int x = 0; x < s_icon_w; x++) {
            int cy = y / LABEL_ICON_CELL, cx = x / LABEL_ICON_CELL;
            uint8_t bit = (uint8_t)(0x80 >> cx);
            s_icon_buf[y * s_icon_w + x] = (feat[cy] & bit)   ? argb(feat_rgb)
                                           : (body[cy] & bit) ? argb(body_rgb)
                                                              : 0;
        }
    }
}

static void label_place(void)
{
    lv_obj_set_pos(s_label, LABEL_X + s_label_cx + s_label_sx, LABEL_Y + s_label_cy + s_label_sy + s_rim_dy);
}

static void label_bins(uint8_t *alpha, int w, int h)
{
    for (int b = 0; b < LW_BINS; b++) s_bin_box[b] = (bin_box_t){INT16_MAX, INT16_MAX, -1, -1};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            s_label_alpha0[i] = alpha[i];
            if (!alpha[i]) {
                s_label_bin[i] = UINT16_MAX;
                continue;
            }
            int b = lw_bin(LABEL_X + s_label_cx + x + 0.5 - CX, LABEL_Y + s_label_cy + y + 0.5 - CY);
            s_label_bin[i] = (uint16_t)b;
            bin_box_t *bb = &s_bin_box[b];
            if (x < bb->x0) bb->x0 = (int16_t)x;
            if (x > bb->x1) bb->x1 = (int16_t)x;
            if (y < bb->y0) bb->y0 = (int16_t)y;
            if (y > bb->y1) bb->y1 = (int16_t)y;
        }
    if (!s_wipe.count) return;
    for (int i = 0; i < w * h; i++)
        if (s_label_bin[i] != UINT16_MAX && lw_has(&s_wipe, s_label_bin[i])) alpha[i] = 0;
}

static void icon_apply(void)
{
    if (s_icon_box.x2 < s_icon_box.x1 || s_icon_box.y2 < s_icon_box.y1) return;
    uint8_t *alpha = (uint8_t *)(s_label_buf + s_label_w * s_label_h);
    for (int y = s_icon_box.y1; y <= s_icon_box.y2; y++)
        for (int x = s_icon_box.x1; x <= s_icon_box.x2; x++) {
            int i = y * s_label_w + x;
            alpha[i] = s_icon_off ? 0 : s_label_alpha0[i];
        }
}

static void label_unwipe(void)
{
    if (!s_wipe.count) return;
    memcpy((uint8_t *)(s_label_buf + s_label_w * s_label_h), s_label_alpha0, (size_t)(s_label_w * s_label_h));
    lw_clear(&s_wipe);
    lv_obj_invalidate(s_label);
}

static lv_point_t origin(void)
{
    lv_area_t c;
    lv_obj_get_coords(s_glint, &c);
    return (lv_point_t){c.x1, c.y1};
}

static void area_add(lv_area_t *a, bool *any, int x1, int y1, int x2, int y2)
{
    if (x2 < x1 || y2 < y1) return;
    if (!*any) {
        *a = (lv_area_t){x1, y1, x2, y2};
        *any = true;
        return;
    }
    if (x1 < a->x1) a->x1 = x1;
    if (y1 < a->y1) a->y1 = y1;
    if (x2 > a->x2) a->x2 = x2;
    if (y2 > a->y2) a->y2 = y2;
}

static void label_crop(const uint32_t *src)
{
    int x0 = LABEL_W, y0 = LABEL_H, x1 = -1, y1 = -1;
    for (int y = 0; y < LABEL_H; y++)
        for (int x = 0; x < LABEL_W; x++)
            if (src[y * LABEL_W + x] >> 24) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < 0) x0 = y0 = 0, x1 = y1 = 1;
    x0 &= ~1, y0 &= ~1;
    int w = (x1 - x0 + 2) & ~1, h = (y1 - y0 + 2) & ~1;
    if (x0 + w > LABEL_W) w = LABEL_W - x0;
    if (y0 + h > LABEL_H) h = LABEL_H - y0;
    uint8_t *alpha = (uint8_t *)(s_label_buf + w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = src[(y0 + y) * LABEL_W + x0 + x];
            s_label_buf[y * w + x] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
            alpha[y * w + x] = (uint8_t)(c >> 24);
        }
    s_label_cx = x0;
    s_label_cy = y0;
    s_label_w = w;
    s_label_h = h;
    if (s_icon_box.x2 >= s_icon_box.x1) {
        lv_area_t b = {s_icon_box.x1 - x0, s_icon_box.y1 - y0, s_icon_box.x2 - x0, s_icon_box.y2 - y0};
        if (b.x1 < 0) b.x1 = 0;
        if (b.y1 < 0) b.y1 = 0;
        if (b.x2 > w - 1) b.x2 = w - 1;
        if (b.y2 > h - 1) b.y2 = h - 1;
        s_icon_box = b;
    }
    label_bins(alpha, w, h);
    icon_apply();
    lv_canvas_set_buffer(s_label, s_label_buf, w, h, LV_COLOR_FORMAT_RGB565A8);
    label_place();
}

static void arc_line(lv_layer_t *layer, const lv_font_t *font, const char *text, float r, uint32_t rgb)
{
    float w[EMBER_LABEL_MAX];
    arc_item_t it[EMBER_LABEL_MAX];
    int n = 0;
    for (const char *p = text; *p && n < EMBER_LABEL_MAX; p++)
        w[n++] = (float)lv_font_get_glyph_width(font, (uint8_t)p[0], (uint8_t)p[1]);
    if (!n) return;
    arc_text_layout((float)(CX - LABEL_X), (float)(CY - LABEL_Y), r, w, n, it);
    for (int i = 0; i < n; i++) {
        lv_draw_letter_dsc_t d;
        lv_draw_letter_dsc_init(&d);
        d.font = font;
        d.color = lv_color_hex(rgb);
        d.opa = LV_OPA_COVER;
        d.unicode = (uint8_t)text[i];
        d.rotation = (int32_t)lroundf(it[i].rot_deg * 10);
        lv_point_t pt = {(int32_t)lroundf(it[i].x), (int32_t)lroundf(it[i].y)};
        lv_draw_letter(layer, &d, &pt);
    }
}

static void label_draw(void)
{
    uint32_t mood_c = mood_rgb(s_rim_mood);
    uint32_t text_c = s_host.color >= 0 ? (uint32_t)s_host.color : mood_c;
    uint8_t body[8], feat[8];
    uint32_t mark_rgb = 0;
    int mark_w = 0, mark_h = 0;
    bool marked = bot_label_mark(s_rim_mood);
    const uint8_t *mark = marked ? tool_mark(s_host.tool, &mark_rgb, &mark_w, &mark_h) : NULL;
    bool icon = marked && (mark || arc_tool_icon(s_host.tool, body, feat));
    const char *sub = s_rim_mood == BOT_WAITING || s_rim_mood == BOT_DONE ? s_host.sub : "";

    /* LVGL cannot render into RGB565A8: draw ARGB8888, keep the drawn bounds. */
    uint32_t *scratch = s_label_scratch;
    lv_obj_t *tmp = lv_canvas_create(NULL);
    lv_canvas_set_buffer(tmp, scratch, LABEL_W, LABEL_H, LV_COLOR_FORMAT_ARGB8888);
    lv_canvas_fill_bg(tmp, lv_color_black(), LV_OPA_TRANSP);
    lv_layer_t layer;
    lv_canvas_init_layer(tmp, &layer);
    static lv_image_dsc_t img;
    if (icon) {
        if (mark) icon_fill_mark(mark, mark_w, mark_h, mark_rgb);
        else icon_fill(body, feat, text_c, mood_c);
        img = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .w = s_icon_w,
                       .h = s_icon_h, .stride = s_icon_w * 4},
            .data_size = s_icon_w * s_icon_h * 4,
            .data = (const uint8_t *)s_icon_buf,
        };
        /* LV_CACHE_DEF_SIZE is 0: no image cache, so the same source with new pixels draws fresh. */
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = &img;
        int x = (int)(CX - LABEL_X), y = (int)(CY - LABEL_Y) + (sub[0] ? LABEL_ICON_SUB_R : LABEL_ICON_R);
        lv_area_t a = {x - s_icon_w / 2, y - s_icon_h, x - s_icon_w / 2 + s_icon_w - 1, y - 1};
        lv_draw_image(&layer, &d, &a);
        s_icon_box = a;
    } else {
        s_icon_box = (lv_area_t){0, 0, -1, -1};
    }
    arc_line(&layer, &lv_font_montserrat_30, s_host.text, LABEL_R, text_c);
    arc_line(&layer, &lv_font_montserrat_18, sub, LABEL_SUB_R, text_c);
    lv_canvas_finish_layer(tmp, &layer);
    lv_obj_delete(tmp);
    label_crop(scratch);
    s_label_mood = (int)s_rim_mood;
    s_label_dirty = false;
}

static void host_sync(void)
{
    bool show = s_opt_label && s_host.text[0] &&
                (s_rim_mood == BOT_WAITING || s_rim_mood == BOT_ERROR || s_rim_mood == BOT_WORKING || s_rim_mood == BOT_DONE);
    if (show && (s_label_dirty || s_label_mood != (int)s_rim_mood)) label_draw();
    if (show == lv_obj_has_flag(s_label, LV_OBJ_FLAG_HIDDEN)) {
        if (show) lv_obj_remove_flag(s_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void bot_view_set_host(const ember_host_info_t *host)
{
    if (strcmp(host->text, s_host.text) == 0 && strcmp(host->sub, s_host.sub) == 0 && host->color == s_host.color &&
        host->tool == s_host.tool)
        return;
    s_host = *host;
    s_label_dirty = true;
    host_sync();
}

bool bot_view_icon_off(bool off, lv_area_t *a)
{
    *a = (lv_area_t){0, 0, -1, -1};
    if (off != s_icon_off) {
        s_icon_off = off;
        icon_apply();
    }
    if (s_icon_box.x2 < s_icon_box.x1 || lv_obj_has_flag(s_label, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_point_t o = origin();
    int lx = LABEL_X + s_label_cx + s_label_sx + o.x, ly = LABEL_Y + s_label_cy + s_label_sy + s_rim_dy + o.y;
    *a = (lv_area_t){(lx + s_icon_box.x1) & ~1, (ly + s_icon_box.y1) & ~1, (lx + s_icon_box.x2) | 1, (ly + s_icon_box.y2) | 1};
    return true;
}

void bot_view_icon_redraw(const lv_area_t *a)
{
    lv_obj_invalidate_area(s_label, a);
}

void bot_view_set_offline(bool on)
{
    if (on == s_offline_on || !s_offline) return;
    s_offline_on = on;
    if (on) lv_obj_remove_flag(s_offline, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_offline, LV_OBJ_FLAG_HIDDEN);
}

void bot_view_set_age(const char *age)
{
    if (!s_age) return;
    if (!s_offline_on || !age) age = "";
    if (strcmp(age, s_age_txt) != 0) {
        strlcpy(s_age_txt, age, sizeof s_age_txt);
        lv_label_set_text_static(s_age, s_age_txt);
    }
    if (s_age_txt[0]) lv_obj_remove_flag(s_age, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_age, LV_OBJ_FLAG_HIDDEN);
}

void bot_view_set_options(bool source_label, bool working_ring)
{
    if (source_label == s_opt_label && working_ring == s_opt_glint) return;
    s_opt_label = source_label;
    s_opt_glint = working_ring;
    host_sync();
}

static void hint_area(lv_area_t *a)
{
    lv_point_t o = origin();
    int y = s_hint.y + s_hint.dy + o.y;
    *a = (lv_area_t){s_hint.x + o.x, y, s_hint.x + o.x + s_hint.w - 1, y + s_hint.h - 1};
}

static void hint_draw(lv_layer_t *layer)
{
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &s_hint.img;
    d.recolor = lv_color_hex(USAGE_RED);
    lv_area_t a;
    hint_area(&a);
    lv_draw_image(layer, &d, &a);
}

static void glint_draw_cb(lv_event_t *e)
{
    if (s_hint.on) hint_draw(lv_event_get_layer(e));
    if (s_glint_idx < 0) return;
    const glint_step_t *st = &s_glint_cache[s_glint_idx];
    s_glint_img = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = st->box.w, .h = st->box.h,
                   .stride = st->box.w},
        .data_size = (uint32_t)(st->box.w * st->box.h),
        .data = st->mask,
    };
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &s_glint_img;
    d.recolor = lv_color_hex(ring_glint_color(mood_rgb(s_rim_mood), GLINT_MIX));
    lv_point_t o = origin();
    int x = st->box.x + o.x, y = st->box.y + s_glint_dy + o.y;
    lv_area_t a = {x, y, x + st->box.w - 1, y + st->box.h - 1};
    lv_draw_image(lv_event_get_layer(e), &d, &a);
}

static const ring_glint_t *glint_geom(void)
{
    static ring_glint_t g;
    g = (ring_glint_t){.cx = (float)CX, .cy = (float)CY, .r = (float)s_r, .hw = GLINT_HW, .tail_deg = GLINT_TAIL_DEG};
    return &g;
}

static float glint_head(int idx) { return (float)idx * 360.0f / GLINT_STEPS - 90.0f; }

static void glint_invalidate(float from_deg, float to_deg)
{
    float span = (to_deg - from_deg) / GLINT_PIECES;
    lv_point_t o = origin();
    for (int i = 0; i < GLINT_PIECES; i++) {
        ring_glint_box_t b;
        ring_glint_span_box(glint_geom(), to_deg - span * (float)i, span, &b);
        int x = b.x + o.x, y = b.y + s_glint_dy + o.y;
        lv_area_t a = {x, y, x + b.w - 1, y + b.h - 1};
        lv_obj_invalidate_area(s_glint, &a);
    }
}

static void glint_hide(void)
{
    if (s_glint_idx >= 0) {
        float h = glint_head(s_glint_idx);
        glint_invalidate(h - GLINT_TAIL_DEG, h);
    }
    s_glint_idx = -1;   /* the object stays: hiding it would invalidate the whole screen */
}

void bot_view_label_fx(float opa, int shift_x, int shift_y)
{
    int o = (int)(opa * 8 + 0.5f) * 255 / 8;
    if (o != s_label_opa) {
        s_label_opa = o;
        lv_obj_set_style_opa(s_label, (lv_opa_t)o, 0);
    }
    if (shift_x != s_label_sx || shift_y != s_label_sy) {
        s_label_sx = shift_x;
        s_label_sy = shift_y;
        label_place();
    }
}

static bool s_glint_force;
static bool s_lap_on;
static double s_lap_at;

void bot_view_glint_force(bool on) { s_glint_force = on; }

static bool glint_enabled(void)
{
    return (s_glint_force || (s_opt_glint && s_rim_mood == BOT_WORKING)) && s_rim_shape == 0;
}

static int glint_step(double t) { return (int)fmod(floor(t / GLINT_STEP_S), GLINT_STEPS); }

static int lap_step(double t)
{
    if (!s_lap_on || !s_opt_glint || s_rim_mood != BOT_DONE || s_rim_shape != 0) return -1;
    return bot_lap_step(s_lap_at, t, GLINT_STEP_S, GLINT_STEPS);
}

bool bot_view_glint_deg(double t, float *deg)
{
    if (!glint_enabled()) return false;
    *deg = glint_head(glint_step(t));
    return true;
}

static void rim_sync(double t);

void bot_view_set_hint(bool on) { s_hint.want = on; }

static void hint_invalidate(void)
{
    lv_area_t a;
    hint_area(&a);
    lv_obj_invalidate_area(s_glint, &a);
}

static bool hint_mask(void)
{
    if (s_hint.mask) return true;
    float hw = (float)(RIM_PX / 2);
    usage_arc_box((float)CX, (float)CY, (float)s_r, hw, USAGE_HINT_A0, USAGE_HINT_A1, &s_hint.x, &s_hint.y, &s_hint.w, &s_hint.h);
    s_hint.mask = heap_caps_malloc((size_t)(s_hint.w * s_hint.h), MALLOC_CAP_SPIRAM);
    if (!s_hint.mask) return false;
    usage_arc_mask((float)CX, (float)CY, (float)s_r, hw, USAGE_HINT_A0, USAGE_HINT_A1, s_hint.x, s_hint.y, s_hint.w, s_hint.h,
                   s_hint.mask);
    s_hint.img = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = (uint32_t)s_hint.w, .h = (uint32_t)s_hint.h,
                   .stride = (uint32_t)s_hint.w},
        .data_size = (uint32_t)(s_hint.w * s_hint.h),
        .data = s_hint.mask,
    };
    return true;
}

static bool hint_sync(void)
{
    bool on = s_hint.want && s_slots.shown == RIM_SLOT_BASE && s_rim_shape == 0 &&
              !lv_obj_has_flag(s_rim_canvas, LV_OBJ_FLAG_HIDDEN) && hint_mask();
    if (on == s_hint.on && (!on || s_hint.dy == s_rim_dy)) return false;
    if (s_hint.on) hint_invalidate();
    s_hint.on = on;
    s_hint.dy = s_rim_dy;
    if (on) hint_invalidate();
    return true;
}

bool bot_view_tick(double t, bool defer)
{
    rim_sync(t);
    bool hint = hint_sync();
    int lap = glint_enabled() ? -1 : lap_step(t);
    bool on = (glint_enabled() || lap >= 0) && s_slots.shown == RIM_SLOT_BASE;
    if (!on) {
        glint_hide();
        return hint;
    }
    s_glint_dy = s_rim_dy;
    int idx = lap >= 0 ? lap : glint_step(t);
    if (idx == s_glint_idx) return hint;
    if (defer && s_glint_idx >= 0 && s_glint_defer < GLINT_MAX_DEFER) {
        s_glint_defer++;
        return hint;
    }
    s_glint_defer = 0;
    glint_step_t *st = &s_glint_cache[idx];
    if (!st->mask) {
        ring_glint_box(glint_geom(), glint_head(idx), &st->box);
        uint8_t *m = heap_caps_malloc((size_t)(st->box.w * st->box.h), MALLOC_CAP_SPIRAM);
        if (!m) return hint;
        ring_glint_raster(glint_geom(), glint_head(idx), &st->box, m);
        st->mask = m;
    }
    float step = 360.0f / GLINT_STEPS, to = glint_head(idx), from = to - GLINT_TAIL_DEG;
    if (s_glint_idx >= 0) {
        int d = (idx - s_glint_idx + GLINT_STEPS) % GLINT_STEPS;
        float h0 = glint_head(s_glint_idx);
        if (d <= GLINT_MAX_DEFER + 2) from = h0 - GLINT_TAIL_DEG, to = h0 + step * (float)d;
        else glint_invalidate(h0 - GLINT_TAIL_DEG, h0);
    }
    glint_invalidate(from, to);
    s_glint_idx = idx;
    return true;
}

static void rim_canvas_show(void *ctx, void *buf)
{
    (void)ctx;
    lv_canvas_set_buffer(s_rim_canvas, buf, SCREEN_W, SCREEN_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_invalidate(s_rim_canvas);
}

static void rim_wake(void *ctx)
{
    (void)ctx;
    xTaskNotifyGive(s_rim_task);
}

static void rim_swapped(void *ctx, int key)
{
    (void)ctx;
    s_rim_shape = key & BOT_RIM_TRIANGLE ? 1 : 0;
    s_rim_mood = bot_rim_mood(key);
    lv_obj_remove_flag(s_rim_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_badge_dot, lv_color_hex(mood_rgb(s_rim_mood)), 0);
    host_sync();
}

static const rim_slots_ops_t RIM_OPS = {.show = rim_canvas_show, .wake = rim_wake, .swapped = rim_swapped};

static void rim_sync(double t)
{
    if (s_rim_want < 0) return;
    int v = rim_slots_sync(&s_slots, s_rim_want, s_rim_face, s_rim_ratio, t, &RIM_OPS);
    if (v == s_slots.shown && s_rim_pose_dy == s_rim_dy) return;
    if (v != s_slots.shown) rim_slots_show(&s_slots, v, &RIM_OPS);
    if (s_rim_pose_dy != s_rim_dy) {
        s_rim_dy = s_rim_pose_dy;
        lv_obj_set_pos(s_rim_canvas, 0, s_rim_dy);
    }
    label_place();
}

bool bot_view_rim_feed(bot_mood_t m, bool asleep, bool shown)
{
    if (!s_rim_task) return true;
    return rim_slots_feed(&s_slots, bot_rim_key(m, asleep), shown, &RIM_OPS);
}

void bot_view_rim_pose(const bot_pose_t *p, double t)
{
    if (!s_rim_task) return;
    s_rim_want = bot_rim_key(p->mood, p->asleep);
    s_rim_face = bot_rim_key(p->face, p->asleep);
    rim_sync(t);
}

static void eye_box(const double *xs, const double *ys, int n, double width, int *ox, int *oy, int *w, int *h)
{
    float hw = (float)width / 2;
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (int i = 0; i < n; i++) {
        minx = fmin(minx, xs[i]); maxx = fmax(maxx, xs[i]);
        miny = fmin(miny, ys[i]); maxy = fmax(maxy, ys[i]);
    }
    *ox = ((int)floor(minx - hw - 1)) & ~1;
    *oy = ((int)floor(miny - hw - 1)) & ~1;
    *w = (((int)ceil(maxx + hw + 1) - *ox) + 1) & ~1;
    *h = (((int)ceil(maxy + hw + 1) - *oy) + 1) & ~1;
    if (*w > EYE_BUF_W) *w = EYE_BUF_W;
    if (*h > EYE_BUF_H) *h = EYE_BUF_H;
}

typedef struct {
    canvas_t *eye;
    const double *xs, *ys;
    int n;
    double width, lid, keep;
    int ox, oy, w, h;
} eye_job_t;

static eye_job_t s_eye_job[2];

static void eye_raster(bot_raster_scratch_t *scratch, eye_job_t *j)
{
    canvas_t *eye = j->eye;
    float hw = (float)j->width / 2;
    eye_box(j->xs, j->ys, j->n, j->width, &j->ox, &j->oy, &j->w, &j->h);
    float lid_y = (float)bot_lid_line_y(j->ys, j->n, j->width, j->lid, j->keep);
    bot_raster_lid_box(lid_y, &j->oy, &j->h);
    int w = j->w, h = j->h;
    float fx[BOT_STROKE_MAX], fy[BOT_STROKE_MAX];
    int segs[BOT_STROKE_MAX];
    for (int i = 0; i < j->n; i++) { fx[i] = (float)j->xs[i]; fy[i] = (float)j->ys[i]; }
    for (int i = 0; i + 1 < j->n; i++) segs[i] = i;
    if (w != eye->w || h != eye->h) {
        const uint16_t white = (uint16_t)(((0xF4 >> 3) << 11) | ((0xF4 >> 2) << 5) | (0xF2 >> 3));
        for (int i = 0; i < w * h; i++) eye->buf[i] = white;
        eye->w = w;
        eye->h = h;
    }
    bot_raster_stroke_below(scratch, NULL, (uint8_t *)(eye->buf + w * h), lid_y, w, h, j->ox, j->oy, fx, fy, j->n - 1, segs,
                            hw, 0xF4F4F2, 1.0f);
}

static void eye_show(const eye_job_t *j)
{
    canvas_t *eye = j->eye;
    lv_canvas_set_buffer(eye->canvas, eye->buf, j->w, j->h, LV_COLOR_FORMAT_RGB565A8);
    lv_obj_set_pos(eye->canvas, j->ox, j->oy);
    lv_obj_invalidate(eye->canvas);
    eye->x = j->ox;
    eye->y = j->oy;
}

static void eye_task(void *arg)
{
    (void)arg;
    static bot_raster_scratch_t scratch;
    for (;;) {
        xSemaphoreTake(s_eye_go, portMAX_DELAY);
        eye_raster(&scratch, &s_eye_job[1]);
        xSemaphoreGive(s_eye_done);
    }
}

static void eyes_draw(const double ex[2][BOT_STROKE_MAX], const double ey[2][BOT_STROKE_MAX], const bot_stroke_t st[2],
                      const double width[2])
{
    static bot_raster_scratch_t scratch;
    for (int e = 0; e < 2; e++) s_eye_job[e] = (eye_job_t){.eye = &s_eye[e], .xs = ex[e], .ys = ey[e], .n = st[e].n, .width = width[e], .lid = st[e].lid, .keep = st[e].keep};
    xSemaphoreGive(s_eye_go);
    eye_raster(&scratch, &s_eye_job[0]);
    if (xSemaphoreTake(s_eye_done, pdMS_TO_TICKS(1000)) != pdTRUE) esp_system_abort("bot_view: eye raster task stalled 1 s");
    for (int e = 0; e < 2; e++) eye_show(&s_eye_job[e]);
}

void bot_view_chase(bool merge, bool wipe)
{
    s_merge = merge;
    s_wipe_on = wipe;
    if (!merge && !wipe) label_unwipe();
}

static void label_wipe_eyes(const lw_stroke_t ls[2], lv_area_t *a, bool *any)
{
    double from, to, outer;
    if (!lw_span(ls, 2, CX + s_label_sx, CY + s_rim_dy + s_label_sy, &from, &to, &outer) || outer < LABEL_WIPE_R) return;
    static uint16_t added[LW_BINS];
    int n = lw_add(&s_wipe, from, to, added, LW_BINS);
    uint8_t *alpha = (uint8_t *)(s_label_buf + s_label_w * s_label_h);
    int lx = LABEL_X + s_label_cx + s_label_sx, ly = LABEL_Y + s_label_cy + s_label_sy + s_rim_dy;
    for (int k = 0; k < n && k < LW_BINS; k++) {
        const bin_box_t *bb = &s_bin_box[added[k]];
        if (bb->x1 < 0) continue;
        for (int y = bb->y0; y <= bb->y1; y++)
            for (int x = bb->x0; x <= bb->x1; x++)
                if (s_label_bin[y * s_label_w + x] == added[k]) alpha[y * s_label_w + x] = 0;
        if (!lv_obj_has_flag(s_label, LV_OBJ_FLAG_HIDDEN))
            area_add(a, any, lx + bb->x0, ly + bb->y0, lx + bb->x1, ly + bb->y1);
    }
}

static void eyes_merged(const double ex[2][BOT_STROKE_MAX], const double ey[2][BOT_STROKE_MAX], const bot_stroke_t st[2],
                        const double width[2])
{
    lv_area_t a;
    bool any = false;
    for (int e = 0; e < 2; e++) {
        if (s_eye[e].w > 0)
            area_add(&a, &any, s_eye[e].x, s_eye[e].y, s_eye[e].x + s_eye[e].w - 1, s_eye[e].y + s_eye[e].h - 1);
        int ox, oy, w, h;
        eye_box(ex[e], ey[e], st[e].n, width[e], &ox, &oy, &w, &h);
        area_add(&a, &any, ox, oy, ox + w - 1, oy + h - 1);
    }
    if (s_wipe_on) {
        lw_stroke_t ls[2] = {{ex[0], ey[0], st[0].n, width[0] / 2}, {ex[1], ey[1], st[1].n, width[1] / 2}};
        label_wipe_eyes(ls, &a, &any);
    }
    if (!any) return;
    lv_point_t o = origin();
    lv_area_move(&a, o.x, o.y);
    lv_obj_invalidate_area(s_glint, &a);
}

void bot_view_update(const bot_pose_t *p, double t)
{
    /* Static, not stack: large locals overflowed the BSP's 8 KB LVGL task stack. */
    static bot_stroke_t st[2];
    static double ex[2][BOT_STROKE_MAX], ey[2][BOT_STROKE_MAX], width[2];

    s_rim_ratio = (float)(p->scale_y / p->scale_x);
    /* Even offsets only: the panel driver rounds areas to 2 px. */
    /* Outline variants swap as one area in a single sweep (docs/features.md, bot face). */
    int dy = bot_rim_dy(p, s_r, HOP_SCALE);
    s_rim_pose_dy = dy;
    s_lap_on = p->lap;
    s_lap_at = p->lap_at;
    rim_sync(t);

    bot_eye_strokes(p, BOT_VIEW_EYE_SCALE, st);
    double k = s_r * (p->scale_x + p->scale_y) / 2;
    for (int e = 0; e < 2; e++) {
        for (int i = 0; i < st[e].n; i++) to_screen(p, st[e].x[i], st[e].y[i], &ex[e][i], &ey[e][i]);
        width[e] = st[e].width * k;
    }
    if (s_merge) eyes_merged(ex, ey, st, width);
    eyes_draw(ex, ey, st, width);

    if (p->badge > 0.01) {
        double cx, cy;
        bot_pose_t neutral = {.scale_x = 1, .scale_y = 1};
        to_screen(&neutral, cos(M_PI / 4) * 0.98, sin(M_PI / 4) * 0.98, &cx, &cy);
        cy += s_rim_dy;
        int gap = (int)lround(0.3 * p->badge * s_r), dot = (int)lround(0.22 * p->badge * s_r);
        lv_obj_set_size(s_badge_gap, 2 * gap, 2 * gap);
        lv_obj_set_pos(s_badge_gap, (int)lround(cx) - gap, (int)lround(cy) - gap);
        lv_obj_set_size(s_badge_dot, 2 * dot, 2 * dot);
        lv_obj_set_pos(s_badge_dot, (int)lround(cx) - dot, (int)lround(cy) - dot);
        lv_obj_remove_flag(s_badge_gap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_badge_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_badge_gap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_badge_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

void bot_view_rim_hold(bool hold)
{
    if (!s_rim_task) return;
    rim_slots_hold(&s_slots, hold, &RIM_OPS);
}
