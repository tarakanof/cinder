#include "bot_view.h"

#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "arc_text.h"
#include "label_wipe.h"
#include "bot_raster.h"
#include "bot_shape.h"
#include "ring_glint.h"
#include "tool_marks.h"
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
#define RIM_PX 6.0

#define EYE_BUF_W 220
#define EYE_BUF_H 260

#define RIM_STEPS 5
#define RIM_VARIANTS (2 * RIM_STEPS + 1)
#define RIM_BASE RIM_STEPS
static float VAR_SX[RIM_VARIANTS], VAR_SY[RIM_VARIANTS];
static int VAR_ORDER[RIM_VARIANTS - 1];

static void variants_init(void)
{
    for (int k = 0; k < RIM_VARIANTS; k++) {
        float t = (float)(k - RIM_BASE) / RIM_STEPS;
        float q = (float)BOT_HOP_SQUASH;
        VAR_SX[k] = t < 0 ? 1 - 0.10f * q * t : 1 - 0.07f * q * t;
        VAR_SY[k] = t < 0 ? 1 + 0.14f * q * t : 1 + 0.10f * q * t;
    }
    for (int d = 1, n = 0; d <= RIM_STEPS; d++) {
        VAR_ORDER[n++] = RIM_BASE - d;
        VAR_ORDER[n++] = RIM_BASE + d;
    }
}

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
static uint16_t *s_rim_buf[RIM_VARIANTS];
static atomic_uint s_rim_gen;
static atomic_uint s_var_ready_gen[RIM_VARIANTS];
static atomic_int s_job_shape, s_job_mood;
static TaskHandle_t s_rim_task;
static int s_rim_shown = RIM_BASE;
static int s_rim_shape = -1;
static bot_mood_t s_rim_mood = (bot_mood_t)-1;
static int s_rim_dy;
static lv_obj_t *s_badge_gap, *s_badge_dot;
#define LABEL_W 340
#define LABEL_H 110
#define LABEL_X ((int)CX - LABEL_W / 2)
#define LABEL_Y 318
#define LABEL_R 176.0f
#define LABEL_ICON_CELL 4
#define LABEL_ICON_MAX_W TOOL_MARK_MAX_W
#define LABEL_ICON_H TOOL_MARK_H
#define LABEL_ICON_R 146
static lv_obj_t *s_label;
static uint16_t *s_label_buf;
static int s_label_cx, s_label_cy;
static int s_label_sx, s_label_sy;
static int s_label_opa = 255;
static uint32_t *s_icon_buf;
static ember_host_info_t s_host = {.color = -1};
static bool s_label_dirty = true;
static uint32_t s_label_mood_rgb;
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

static void rim_raster(uint16_t *buf, int shape, bot_mood_t mood, float sx, float sy, rim_work_t *wk)
{
    double *bx = wk->bx, *by = wk->by;
    float *rx = wk->rx, *ry = wk->ry;
    int *segs = wk->segs;
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
    bool dim = mood == BOT_IDLE || mood == BOT_SLEEPY;
    bot_raster_stroke(&wk->scratch, buf, NULL, SCREEN_W, SCREEN_H, 0, 0, rx, ry, BOT_RING_POINTS, segs,
                      (float)(RIM_PX / 2), mood_rgb(mood), dim ? 0.55f : 1.0f);
}

static void rim_task(void *arg)
{
    (void)arg;
    static EXT_RAM_BSS_ATTR rim_work_t work;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        unsigned gen = atomic_load(&s_rim_gen);
        int shape = atomic_load(&s_job_shape);
        bot_mood_t mood = (bot_mood_t)atomic_load(&s_job_mood);
        for (int k = 0; k < RIM_VARIANTS - 1; k++) {
            if (atomic_load(&s_rim_gen) != gen) break;
            int v = VAR_ORDER[k];
            rim_raster(s_rim_buf[v], shape, mood, VAR_SX[v], VAR_SY[v], &work);
            atomic_store(&s_var_ready_gen[v], gen);
            vTaskDelay(1);
        }
    }
}

void bot_view_create(lv_obj_t *parent)
{
    s_r = 233.0 * FILL;
    variants_init();
    for (int v = 0; v < RIM_VARIANTS; v++) {
        s_rim_buf[v] = heap_caps_malloc(SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
        assert(s_rim_buf[v]);
        atomic_store(&s_var_ready_gen[v], 0);
    }
    s_rim_canvas = canvas_create(parent);
    lv_obj_add_flag(s_rim_canvas, LV_OBJ_FLAG_HIDDEN);
    s_glint_cache = heap_caps_calloc(GLINT_STEPS, sizeof *s_glint_cache, MALLOC_CAP_SPIRAM);
    assert(s_glint_cache);
    s_glint = lv_obj_create(parent);
    lv_obj_remove_style_all(s_glint);
    lv_obj_set_size(s_glint, SCREEN_W, SCREEN_H);
    lv_obj_remove_flag(s_glint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_glint, glint_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    for (int e = 0; e < 2; e++) {
        s_eye[e].buf = heap_caps_malloc(EYE_BUF_W * EYE_BUF_H * 3, MALLOC_CAP_SPIRAM);
        assert(s_eye[e].buf);
        s_eye[e].canvas = canvas_create(parent);
    }
    s_badge_gap = circle_create(parent, lv_color_black());
    s_badge_dot = circle_create(parent, lv_color_hex(mood_rgb(BOT_IDLE)));
    s_label_buf = heap_caps_malloc(LABEL_W * LABEL_H * 3, MALLOC_CAP_SPIRAM);
    s_label_alpha0 = heap_caps_malloc(LABEL_W * LABEL_H, MALLOC_CAP_SPIRAM);
    s_label_bin = heap_caps_malloc(LABEL_W * LABEL_H * sizeof *s_label_bin, MALLOC_CAP_SPIRAM);
    assert(s_label_alpha0 && s_label_bin);
    s_icon_buf = heap_caps_malloc(LABEL_ICON_MAX_W * LABEL_ICON_H * 4, MALLOC_CAP_SPIRAM);
    assert(s_label_buf && s_icon_buf);
    s_label = canvas_create(parent);
    lv_obj_add_flag(s_label, LV_OBJ_FLAG_HIDDEN);
    xTaskCreatePinnedToCore(rim_task, "rim", 6144, NULL, 2, &s_rim_task, 1);
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

static void label_unwipe(void)
{
    if (!s_wipe.count) return;
    memcpy((uint8_t *)(s_label_buf + s_label_w * s_label_h), s_label_alpha0, (size_t)(s_label_w * s_label_h));
    lw_clear(&s_wipe);
    lv_obj_invalidate(s_label);
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
    label_bins(alpha, w, h);
    lv_canvas_set_buffer(s_label, s_label_buf, w, h, LV_COLOR_FORMAT_RGB565A8);
    label_place();
}

static void label_draw(void)
{
    static const lv_font_t *const font = &lv_font_montserrat_30;
    uint32_t mood_c = mood_rgb(s_rim_mood);
    uint32_t text_c = s_host.color >= 0 ? (uint32_t)s_host.color : mood_c;
    float w[EMBER_HOST_MAX];
    arc_item_t it[EMBER_HOST_MAX];
    uint8_t body[8], feat[8];
    int n = 0;
    uint32_t mark_rgb = 0;
    int mark_w = 0, mark_h = 0;
    const uint8_t *mark = tool_mark(s_host.tool, &mark_rgb, &mark_w, &mark_h);
    bool icon = mark || arc_tool_icon(s_host.tool, body, feat);
    int first = n;
    for (const char *p = s_host.text; *p && n < EMBER_HOST_MAX; p++)
        w[n++] = (float)lv_font_get_glyph_width(font, (uint8_t)p[0], (uint8_t)p[1]);
    arc_text_layout((float)(CX - LABEL_X), (float)(CY - LABEL_Y), LABEL_R, w, n, it);

    /* LVGL cannot render into RGB565A8: draw ARGB8888, keep the drawn bounds. */
    uint32_t *scratch = heap_caps_malloc(LABEL_W * LABEL_H * 4, MALLOC_CAP_SPIRAM);
    if (!scratch) return;
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
        int x = (int)(CX - LABEL_X), y = (int)(CY - LABEL_Y) + LABEL_ICON_R;
        lv_area_t a = {x - s_icon_w / 2, y - s_icon_h, x - s_icon_w / 2 + s_icon_w - 1, y - 1};
        lv_draw_image(&layer, &d, &a);
    }
    for (int i = first; i < n; i++) {
        lv_draw_letter_dsc_t d;
        lv_draw_letter_dsc_init(&d);
        d.font = font;
        d.color = lv_color_hex(text_c);
        d.opa = LV_OPA_COVER;
        d.unicode = (uint8_t)s_host.text[i - first];
        d.rotation = (int32_t)lroundf(it[i - first].rot_deg * 10);
        lv_point_t pt = {(int32_t)lroundf(it[i - first].x), (int32_t)lroundf(it[i - first].y)};
        lv_draw_letter(&layer, &d, &pt);
    }
    lv_canvas_finish_layer(tmp, &layer);
    lv_obj_delete(tmp);
    label_crop(scratch);
    heap_caps_free(scratch);
    s_label_mood_rgb = mood_c;
    s_label_dirty = false;
}

static void host_sync(void)
{
    bool show = s_opt_label && s_host.text[0] &&
                (s_rim_mood == BOT_WAITING || s_rim_mood == BOT_ERROR || s_rim_mood == BOT_WORKING);
    if (show && (s_label_dirty || s_label_mood_rgb != mood_rgb(s_rim_mood))) label_draw();
    if (show == lv_obj_has_flag(s_label, LV_OBJ_FLAG_HIDDEN)) {
        if (show) lv_obj_remove_flag(s_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void bot_view_set_host(const ember_host_info_t *host)
{
    if (strcmp(host->text, s_host.text) == 0 && host->color == s_host.color && host->tool == s_host.tool) return;
    s_host = *host;
    s_label_dirty = true;
    host_sync();
}

void bot_view_set_options(bool source_label, bool working_ring)
{
    if (source_label == s_opt_label && working_ring == s_opt_glint) return;
    s_opt_label = source_label;
    s_opt_glint = working_ring;
    host_sync();
}

static void glint_draw_cb(lv_event_t *e)
{
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
    d.recolor = lv_color_hex(ring_glint_color(mood_rgb(BOT_WORKING), GLINT_MIX));
    int y = st->box.y + s_glint_dy;
    lv_area_t a = {st->box.x, y, st->box.x + st->box.w - 1, y + st->box.h - 1};
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
    for (int i = 0; i < GLINT_PIECES; i++) {
        ring_glint_box_t b;
        ring_glint_span_box(glint_geom(), to_deg - span * (float)i, span, &b);
        lv_area_t a = {b.x, b.y + s_glint_dy, b.x + b.w - 1, b.y + s_glint_dy + b.h - 1};
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

static bool glint_enabled(void) { return s_opt_glint && s_rim_mood == BOT_WORKING && s_rim_shape == 0; }

static int glint_step(double t) { return (int)fmod(floor(t / GLINT_STEP_S), GLINT_STEPS); }

bool bot_view_glint_deg(double t, float *deg)
{
    if (!glint_enabled()) return false;
    *deg = glint_head(glint_step(t));
    return true;
}

bool bot_view_tick(double t, bool defer)
{
    bool on = glint_enabled() && s_rim_shown == RIM_BASE;
    if (!on) {
        glint_hide();
        return false;
    }
    s_glint_dy = s_rim_dy;
    int idx = glint_step(t);
    if (idx == s_glint_idx) return false;
    if (defer && s_glint_idx >= 0 && s_glint_defer < GLINT_MAX_DEFER) {
        s_glint_defer++;
        return false;
    }
    s_glint_defer = 0;
    glint_step_t *st = &s_glint_cache[idx];
    if (!st->mask) {
        ring_glint_box(glint_geom(), glint_head(idx), &st->box);
        uint8_t *m = heap_caps_malloc((size_t)(st->box.w * st->box.h), MALLOC_CAP_SPIRAM);
        if (!m) return false;
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

static void rim_show(int v)
{
    s_rim_shown = v;
    lv_canvas_set_buffer(s_rim_canvas, s_rim_buf[v], SCREEN_W, SCREEN_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_invalidate(s_rim_canvas);
}

static void rim_move(int v, int dy)
{
    if (v != s_rim_shown) rim_show(v);
    if (dy != s_rim_dy) {
        s_rim_dy = dy;
        lv_obj_set_pos(s_rim_canvas, 0, dy);
    }
}

static int rim_variant_for(const bot_pose_t *p)
{
    float ratio = (float)(p->scale_y / p->scale_x);
    if (fabsf(ratio - 1.0f) < 0.015f) return RIM_BASE;
    unsigned gen = atomic_load(&s_rim_gen);
    int best = RIM_BASE;
    float best_d = fabsf(ratio - 1.0f);
    for (int v = 0; v < RIM_VARIANTS; v++) {
        if (v != RIM_BASE && atomic_load(&s_var_ready_gen[v]) != gen) continue;
        float d = fabsf(ratio - VAR_SY[v] / VAR_SX[v]);
        if (d < best_d) { best_d = d; best = v; }
    }
    return best;
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
    double width;
    int ox, oy, w, h;
} eye_job_t;

static eye_job_t s_eye_job[2];

static void eye_raster(bot_raster_scratch_t *scratch, eye_job_t *j)
{
    canvas_t *eye = j->eye;
    float hw = (float)j->width / 2;
    eye_box(j->xs, j->ys, j->n, j->width, &j->ox, &j->oy, &j->w, &j->h);
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
    bot_raster_stroke(scratch, NULL, (uint8_t *)(eye->buf + w * h), w, h, j->ox, j->oy, fx, fy, j->n - 1, segs, hw,
                      0xF4F4F2, 1.0f);
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
    for (int e = 0; e < 2; e++) s_eye_job[e] = (eye_job_t){.eye = &s_eye[e], .xs = ex[e], .ys = ey[e], .n = st[e].n, .width = width[e]};
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
    if (any) lv_obj_invalidate_area(s_glint, &a);
}

void bot_view_update(const bot_pose_t *p)
{
    /* Static, not stack: large locals overflowed the BSP's 8 KB LVGL task stack. */
    static bot_stroke_t st[2];
    static double ex[2][BOT_STROKE_MAX], ey[2][BOT_STROKE_MAX], width[2];
    static EXT_RAM_BSS_ATTR rim_work_t work;

    int shape = p->triangle >= 0.5 ? 1 : 0;
    if (shape != s_rim_shape || p->mood != s_rim_mood) {
        s_rim_shape = shape;
        s_rim_mood = p->mood;
        atomic_fetch_add(&s_rim_gen, 1);
        rim_raster(s_rim_buf[RIM_BASE], shape, p->mood, 1.0f, 1.0f, &work);
        rim_show(RIM_BASE);
        lv_obj_set_pos(s_rim_canvas, 0, s_rim_dy);
        lv_obj_remove_flag(s_rim_canvas, LV_OBJ_FLAG_HIDDEN);
        atomic_store(&s_job_shape, shape);
        atomic_store(&s_job_mood, p->mood);
        xTaskNotifyGive(s_rim_task);
        lv_obj_set_style_bg_color(s_badge_dot, lv_color_hex(mood_rgb(p->mood)), 0);
        host_sync();
    }

    /* Even offsets only: the panel driver rounds areas to 2 px. */
    /* Outline variants swap as one area in a single sweep (docs/features.md, bot face). */
    int v = rim_variant_for(p);
    int dy = ((int)lround(-s_r * p->offset_y * HOP_SCALE)) & ~1;
    if (v != s_rim_shown || dy != s_rim_dy) {
        rim_move(v, dy);
        label_place();
    }

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
