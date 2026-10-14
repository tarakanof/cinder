#include "moment_view.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "bot_view.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { MV_IDLE, MV_BAKING, MV_PLAYING };
#define MV_RETRY_S 5.0
#define MV_YIELD_US 25000
enum { BAKE_IDLE, BAKE_QUEUED, BAKE_RUNNING, BAKE_DONE };

static lv_obj_t *s_parent, *s_box, *s_el[WXM_ELEMS], *s_text;
static uint8_t *s_arena;
static TaskHandle_t s_task;
static atomic_int s_bake;
static int s_state;
static bool s_cancel, s_held;
static EXT_RAM_BSS_ATTR wxm_moment_t s_m;
static EXT_RAM_BSS_ATTR wxm_layout_t s_l;
static EXT_RAM_BSS_ATTR wxm_frame_t s_shown;
static EXT_RAM_BSS_ATTR bool s_any;
static EXT_RAM_BSS_ATTR double s_t0, s_play_at;
static EXT_RAM_BSS_ATTR int s_k;
static EXT_RAM_BSS_ATTR int s_count, s_bake_ms, s_failed, s_refused;
static EXT_RAM_BSS_ATTR double s_retry_at;
static EXT_RAM_BSS_ATTR int64_t s_yield_at;

static void bake_yield(void)
{
    if (esp_timer_get_time() - s_yield_at < MV_YIELD_US) return;
    vTaskDelay(1);
    s_yield_at = esp_timer_get_time();
}

static void moment_bake_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int queued = BAKE_QUEUED;
        if (!atomic_compare_exchange_strong(&s_bake, &queued, BAKE_RUNNING)) continue;
        int64_t t0 = esp_timer_get_time();
        s_yield_at = t0;
        s_refused += wxm_paint(&s_m, &s_l, s_arena, bake_yield);
        s_bake_ms = (int)((esp_timer_get_time() - t0) / 1000);
        atomic_store(&s_bake, BAKE_DONE);
    }
}

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

void moment_view_create(lv_obj_t *parent)
{
    s_parent = parent;
    s_box = plain(parent);
    lv_obj_set_pos(s_box, WXM_BOX_X, WXM_BOX_Y);
    lv_obj_set_size(s_box, WXM_BOX_W, WXM_BOX_H);
    for (int i = 0; i < WXM_ELEMS; i++) {
        s_el[i] = lv_canvas_create(s_box);
        lv_obj_remove_flag(s_el[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_el[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_text = lv_label_create(parent);
    lv_obj_set_style_text_font(s_text, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_CLIP);
    int lh = lv_font_get_line_height(&lv_font_montserrat_30);
    lv_obj_set_pos(s_text, WXM_TEXT_X, WXM_TEXT_Y + (WXM_TEXT_H - lh) / 2);
    lv_obj_set_size(s_text, WXM_TEXT_W, lh);
    lv_obj_remove_flag(s_text, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_text, LV_OBJ_FLAG_HIDDEN);
}

static void show(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void hide_all(void)
{
    show(s_box, false);
    show(s_text, false);
    lv_area_t a;
    if (s_any && s_shown.icon_off && bot_view_icon_off(false, &a)) bot_view_icon_redraw(&a);
}

bool moment_view_busy(void) { return s_state != MV_IDLE; }

static bool ensure(void)
{
    if (!s_task && xTaskCreatePinnedToCoreWithCaps(moment_bake_task, "moment", 5120, NULL, 2, &s_task, 1, MALLOC_CAP_SPIRAM) != pdPASS)
        s_task = NULL;
    if (s_task && !s_arena) s_arena = heap_caps_malloc(WXM_ARENA, MALLOC_CAP_SPIRAM);
    return s_task && s_arena;
}

void moment_view_release(void)
{
    if (!s_arena || s_state != MV_IDLE || atomic_load(&s_bake) != BAKE_IDLE) return;
    heap_caps_free(s_arena);
    s_arena = NULL;
}

bool moment_view_start(const wxm_moment_t *m, double t)
{
    if (s_held || s_state != MV_IDLE || atomic_load(&s_bake) != BAKE_IDLE) return false;
    s_m = *m;
    wxm_layout(&s_m, &s_l);
    if (s_l.total > WXM_ARENA || t < s_retry_at) return false;
    if (!ensure()) {
        s_failed++;
        s_retry_at = t + MV_RETRY_S;
        return false;
    }
    s_cancel = false;
    s_play_at = t + (m->quiet ? 0 : WXM_PRE_S);
    s_state = MV_BAKING;
    atomic_store(&s_bake, BAKE_QUEUED);
    xTaskNotifyGive(s_task);
    return true;
}

static void attach(void)
{
    for (int i = 0; i < WXM_ELEMS; i++) {
        show(s_el[i], false);
        if (i >= s_l.ne) continue;
        const wxm_layer_t *a = &s_l.layer[s_l.el[i].layer];
        lv_canvas_set_buffer(s_el[i], s_arena + a->at, a->w, a->h, LV_COLOR_FORMAT_RGB565A8);
    }
    lv_label_set_text(s_text, s_m.text);
    lv_obj_set_style_text_color(s_text, lv_color_hex(s_m.rgb), 0);
    s_any = false;
    s_k = -1;
}

double moment_view_poll(double t)
{
    if (s_state == MV_BAKING && atomic_load(&s_bake) == BAKE_DONE && (s_cancel || t >= s_play_at)) {
        atomic_store(&s_bake, BAKE_IDLE);
        if (s_cancel) {
            s_state = MV_IDLE;
            return -1;
        }
        attach();
        s_state = MV_PLAYING;
        s_t0 = t;
        s_count++;
    }
    return s_state == MV_PLAYING ? t - s_t0 : -1;
}

static void sprite(const wxm_frame_t *f)
{
    lv_area_t box = {WXM_BOX_X, WXM_BOX_Y, WXM_BOX_X + WXM_BOX_W - 1, WXM_BOX_Y + WXM_BOX_H - 1};
    lv_obj_invalidate_area(s_parent, &box);
    bool was = s_any && s_shown.opa > 0;
    if (!was || s_shown.opa != f->opa) lv_obj_set_style_opa(s_box, (lv_opa_t)f->opa, 0);
    show(s_box, f->opa > 0);
    for (int i = 0; i < s_l.ne; i++) {
        const wxm_el_t *e = &f->el[i], *p = &s_shown.el[i];
        bool on = e->opa > 0;
        if (on && (!was || !p->opa || p->x != e->x || p->y != e->y)) lv_obj_set_pos(s_el[i], e->x, e->y);
        if (on && (!was || p->opa != e->opa)) lv_obj_set_style_opa(s_el[i], (lv_opa_t)e->opa, 0);
        show(s_el[i], on);
    }
}

static void text(const wxm_frame_t *f)
{
    lv_area_t icon, u = {WXM_TEXT_X, WXM_TEXT_Y, WXM_TEXT_X + WXM_TEXT_W - 1, WXM_TEXT_Y + WXM_TEXT_H - 1};
    bool turns = s_m.quiet && (s_any && s_shown.icon_off) != f->icon_off;
    bool joined = turns && bot_view_icon_off(f->icon_off, &icon);
    if (joined) {
        if (icon.x1 < u.x1) u.x1 = icon.x1;
        if (icon.y1 < u.y1) u.y1 = icon.y1;
        if (icon.x2 > u.x2) u.x2 = icon.x2;
        if (icon.y2 > u.y2) u.y2 = icon.y2;
    }
    lv_obj_invalidate_area(s_parent, &u);
    if (joined) bot_view_icon_redraw(&icon);
    if (f->text_opa > 0) lv_obj_set_style_opa(s_text, (lv_opa_t)f->text_opa, 0);
    show(s_text, f->text_opa > 0);
}

void moment_view_frame(double t)
{
    if (s_state != MV_PLAYING) return;
    int k = (int)floor((t - s_t0) * WXM_FPS);
    if (k >= WXM_FRAMES) {
        hide_all();
        s_state = MV_IDLE;
        return;
    }
    if (k == s_k || k < 0) return;
    static EXT_RAM_BSS_ATTR wxm_frame_t f;
    wxm_frame(&s_m, &s_l, k, &f);
    int d = wxm_diff(&s_l, s_any ? &s_shown : NULL, &f);
    if (d & WXM_DIRTY_SPRITE) sprite(&f);
    if (d & WXM_DIRTY_TEXT) text(&f);
    s_shown = f;
    s_any = true;
    s_k = k;
}

void moment_view_abort(double t)
{
    if (s_state == MV_BAKING) {
        s_cancel = true;
        return;
    }
    if (s_state != MV_PLAYING) return;
    int k = wxm_abort_frame(s_k);
    if (k != s_k) s_t0 = t - (k + 0.5) / WXM_FPS;
}

void moment_view_stop(void)
{
    if (s_state == MV_BAKING) s_cancel = true;
    if (s_state == MV_PLAYING) {
        hide_all();
        s_state = MV_IDLE;
    }
}

void moment_view_hold(bool hold)
{
    s_held = hold;
    if (hold) moment_view_stop();
}

void moment_view_stats(int *shown, int *bake_ms, int *failed, int *refused)
{
    *shown = s_count;
    *bake_ms = s_bake_ms;
    *failed = s_failed;
    *refused = s_refused;
}
