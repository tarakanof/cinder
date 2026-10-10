#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "diag.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "iot_knob.h"
#include "lvgl.h"
#include "cJSON.h"

#include "bot_behavior.h"
#include "bot_view.h"
#include "chase.h"
#include "glint_chase.h"
#include "bot_shape.h"
#include "orbit_table.h"
#include "panel_check.h"
#include "esp_random.h"
#include "display/lv_display_private.h"
#include "config_store.h"
#include "dim.h"
#include "ember_client.h"
#include "ember_host.h"
#include "http_conn.h"
#include "pomo_client.h"
#include "pomo_view.h"
#include "nowplaying_client.h"
#include "nowplaying_view.h"
#include "ota_client.h"
#include "ota_face.h"
#include "page_ops.h"
#include "pages.h"
#include "press_route.h"
#include "provision_usb.h"
#include "screen_snap.h"
#include "reset_gesture.h"
#include "touch_swipe.h"
#include "weather_client.h"
#include "weather_view.h"
#include "bsp_knob_15_md50et.h"

static const char *TAG = "cinder";

/* The iot_knob "left" event fires on a clockwise turn (docs/llm.md). */
#define KNOB_EVENT_CLOCKWISE KNOB_LEFT
#define DEGREES_PER_DETENT 15.0

static atomic_int s_detents;
static atomic_int s_pushes;
static atomic_int s_touches;
static atomic_int s_long_pushes;
static atomic_int s_page_steps;
static atomic_int s_swipes;
static atomic_int s_np_wakes;
static atomic_bool s_hold_seen;
static atomic_bool s_button_down;
static atomic_bool s_turned_while_down;

static reset_gesture_t s_rg;
static portMUX_TYPE s_rg_mux = portMUX_INITIALIZER_UNLOCKED;
static lv_obj_t *s_reset_layer, *s_reset_arc, *s_reset_label;
static rg_state_t s_reset_shown = RG_IDLE;
static int s_reset_progress_shown = -1;

static bool s_setup;
static lv_obj_t *s_join_label;
static int s_join_shown;
static pages_nav_t s_nav = {.n = 1};
static uint32_t s_settings_gen;
static knob_settings_t *s_ks_lv;
static lv_obj_t *s_bot_page;
static double s_last_frame_t;
static EXT_RAM_BSS_ATTR ts_touch_t s_touch;
#define SCREEN_W 472
#define DOT_PX 8
#define DOT_PITCH 16
#define DOTS_X 442
static lv_obj_t *s_wipe, *s_dots;
static int s_wipe_dir, s_wipe_edge = -TS_WIPE_BAND_PX;
static double s_wipe_t0, s_dots_until;

static bot_t s_bot;
static bot_pose_t s_last_pose;
static bool s_drawn;
static double s_ring_deg;
static int s_demo_index;
static double s_demo_until;
static double s_demo_hold_s = 20.0;
#define LINK_FALLBACK 1
#define LINK_CHECK_WAIT_MS 1000
#define CHASE_LEAD_DEG 15.0
/* Chase gaze reach is tuned against tearing without TE; the eye rate is CINDER1 chase fps (docs/features.md, chase tearing, chase near the ring). */
#define CHASE_GAZE 0.6
#define CHASE_FPS_DEFAULT 60
#define CHASE_OUTER 0.92
static glint_chase_t s_chase;
static atomic_int s_chase_status = 2;
static atomic_int s_chase_req = -1;
static atomic_int s_chase_fps = CHASE_FPS_DEFAULT;
static gc_half_t s_half;
static double s_half_hold_deg;
static bool s_half_snapped;

int app_chase_request(int style, int fps, int laps)
{
    int st = atomic_load(&s_chase_status);
    if (st != 0) return st;
    if (fps > 0) atomic_store(&s_chase_fps, fps);
    atomic_store(&s_chase_req, style | laps << 4);
    return st;
}

#define ORBIT_CHUNK 8
static EXT_RAM_BSS_ATTR bot_orbit_table_t s_orbit;
static TaskHandle_t s_orbit_task;
static atomic_bool s_orbit_done;

static void orbit_task(void *arg)
{
    (void)arg;
    for (int k = 0; k < BOT_ORBIT_KINDS; k++)
        for (int i = 0; i < BOT_ORBIT_TAB_N; vTaskDelay(1)) i = bot_orbit_table_fill_part(&s_orbit, (bot_eyes_t)k, i, ORBIT_CHUNK);
    atomic_store(&s_orbit_done, true);
    for (;;) vTaskSuspend(NULL);
}

static void orbit_reap(void)
{
    if (!s_orbit_task || !atomic_load(&s_orbit_done)) return;
    vTaskDeleteWithCaps(s_orbit_task);
    s_orbit_task = NULL;
}

static double chase_gaze(const bot_pose_t *p, double deg, double orbit)
{
    if (orbit <= 0) return CHASE_GAZE;
    double ring = bot_orbit_table_get(&s_orbit, p, deg);
    return CHASE_GAZE + (ring - CHASE_GAZE) * orbit;
}

static void chase_track(const bot_pose_t *ref, double deg, double orbit, double t)
{
    double m = chase_gaze(ref, deg, orbit), a = deg * M_PI / 180;
    bot_track(&s_bot, cos(a) * m, -sin(a) * m, t);
}

void app_input_inject(int kind, int n)
{
    switch (kind) {
    case 0: atomic_fetch_add(&s_detents, n); break;
    case 1: atomic_fetch_add(&s_pushes, n); break;
    case 2: atomic_fetch_add(&s_long_pushes, n); break;
    case 3: atomic_fetch_add(&s_touches, n); break;
    case 4: atomic_fetch_add(&s_page_steps, n); break;
    case 5: atomic_fetch_add(&s_swipes, n); break;
    case 6: atomic_fetch_add(&s_swipes, -n); break;
    default: break;
    }
}

static double s_pose_drawn_at;
static int s_frames;
static double s_fps_since;
static volatile bot_mood_t s_cur_mood;

static int64_t s_refr_start_us;
static int s_refr_count;
static int64_t s_refr_total_us, s_refr_max_us;
static int s_frame_kind;
static int s_refr_kind;
static int64_t s_refr_px;
typedef struct {
    int n, areas_max;
    int64_t us, us_max;
    int64_t px;
} refr_kind_t;
static refr_kind_t s_kind[4];

static void refr_cb(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    lv_display_t *disp = lv_event_get_current_target(e);
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) {
        s_refr_start_us = now;
        s_refr_kind = s_frame_kind;
        s_frame_kind = 0;
        refr_kind_t *k = &s_kind[s_refr_kind];
        int64_t px = 0;
        for (uint32_t i = 0; i < disp->inv_p; i++) px += lv_area_get_size(&disp->inv_areas[i]);
        s_refr_px = px;
        if (px) {
            k->px += px;
            if ((int)disp->inv_p > k->areas_max) k->areas_max = (int)disp->inv_p;
        }
        return;
    }
    int64_t d = now - s_refr_start_us;
    refr_kind_t *k = &s_kind[s_refr_kind];
    if (s_refr_px) {
        k->n++;
        k->us += d;
        if (d > k->us_max) k->us_max = d;
        ota_client_note_frame();
    }
    diag_note_frame(d);
    s_refr_count++;
    s_refr_total_us += d;
    if (d > s_refr_max_us) s_refr_max_us = d;
}

static void link_reboot(const char *why)
{
    ESP_LOGE(TAG, "display link: %s", why);
    vTaskDelay(pdMS_TO_TICKS(200));
}

static bool link_confirm_fail(int seed)
{
    int fails = 0;
    for (int k = 0; k < 3; k++) {
        uint8_t raw[32];
        int bad = panel_check_run(seed + 1000 + k, raw, LINK_CHECK_WAIT_MS);
        ESP_LOGW(TAG, "link recheck %d: %d (level %02X wrote %02X read %02X)", k, bad, raw[0], raw[1], raw[2]);
        fails += bad > 0;
    }
    return fails >= 2;
}

static ota_reboot_gate_t s_reboot_gate;

static void link_reboot_run(ota_reboot_kind_t k)
{
    if (k == OTA_REBOOT_FALLBACK) bsp_knob_15_md50et_qspi_fallback_reboot();
    else if (k == OTA_REBOOT_RESTART) esp_restart();
}

static void link_reboot_request(ota_reboot_kind_t k, const char *why)
{
    link_reboot(why);
    ota_reboot_kind_t now = ota_reboot_request(&s_reboot_gate, k, ota_client_verifying());
    if (now == OTA_REBOOT_NONE) ESP_LOGW(TAG, "display link: reboot held until the new firmware is marked valid");
    link_reboot_run(now);
}

static void link_task(void *arg)
{
    (void)arg;
    static EXT_RAM_BSS_ATTR knob_settings_t ks;
    vTaskDelay(pdMS_TO_TICKS(3000));
    int fast = bsp_knob_15_md50et_qspi_hz() > 40 * 1000 * 1000;
    if (bsp_knob_15_md50et_qspi_fallback_active()) ESP_LOGW(TAG, "display link: 40 MHz (fallback after a failed check)");
    for (int i = 0; i < 5; i++) {
        uint8_t raw[32];
        int bad = panel_check_run(i, raw, LINK_CHECK_WAIT_MS);
        ESP_LOGW(TAG, "link check %d at %d MHz: %d bad | %02X %02X %02X %02X %02X %02X %02X %02X", i,
                 bsp_knob_15_md50et_qspi_hz() / 1000000, bad, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                 raw[6], raw[7]);
        if (bad < 0) continue;
        bool failed = bad > 0 && link_confirm_fail(i);
        ota_client_note_link(ota_link_result(failed, fast));
        if (failed && fast && LINK_FALLBACK) {
            link_reboot_request(OTA_REBOOT_FALLBACK, "check failed at boot: rebooting at 40 MHz");
        }
    }
    uint32_t runs = 0, fails = 0, errs = 0;
    int streak = 0;
    for (int seed = 100;; seed++) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        link_reboot_run(ota_reboot_release(&s_reboot_gate, ota_client_verifying()));
        config_store_settings(&ks);
        bool want = ks.fast_link && !bsp_knob_15_md50et_qspi_fallback_active();
        if (want != (bool)fast && s_reboot_gate.held == OTA_REBOOT_NONE)
            link_reboot_request(OTA_REBOOT_RESTART,
                                want ? "fast link turned on: rebooting at 80 MHz" : "fast link turned off: rebooting at 40 MHz");
        int bad = panel_check_run(seed, NULL, LINK_CHECK_WAIT_MS);
        runs++;
        if (bad < 0) errs++;
        else if (bad > 0) fails++;
        if (bad > 0 && !link_confirm_fail(seed)) bad = 0;
        streak = bad > 0 ? streak + 1 : 0;
        if (bad >= 0) ota_client_note_link(ota_link_result(streak >= 2, fast));
        if (streak >= 2 && fast && LINK_FALLBACK) {
            link_reboot_request(OTA_REBOOT_FALLBACK, "two checks failed: rebooting at 40 MHz");
            streak = 0;
        }
        if (bad != 0 || runs % 12 == 0)
            ESP_LOGW(TAG, "link soak: %" PRIu32 " runs, %" PRIu32 " mismatches, %" PRIu32 " errors (%d MHz)", runs, fails,
                     errs, bsp_knob_15_md50et_qspi_hz() / 1000000);
    }
}

static const bot_mood_t DEMO_MOODS[] = {BOT_IDLE, BOT_WORKING, BOT_WAITING, BOT_DONE, BOT_ERROR};
static const char *const MOOD_NAMES[] = {"idle", "sleepy", "working", "waiting", "error", "done"};

static double now_s(void) { return esp_timer_get_time() / 1e6; }

static void knob_cb(void *event)
{
    knob_event_t ev = (knob_event_t)(intptr_t)event;
    int dir = ev == KNOB_EVENT_CLOCKWISE ? 1 : -1;
    ota_client_note_input();
    taskENTER_CRITICAL(&s_rg_mux);
    bool reset_turn = rg_turn(&s_rg, dir, now_s());
    taskEXIT_CRITICAL(&s_rg_mux);
    switch (pr_turn(reset_turn, atomic_load(&s_button_down))) {
    case PR_TURN_PAGE:
        atomic_store(&s_turned_while_down, true);
        atomic_fetch_add(&s_page_steps, dir);
        break;
    case PR_TURN_DETENT:
        atomic_fetch_add(&s_detents, dir);
        break;
    default:
        break;
    }
}

static void button_cb(void *event)
{
    ota_client_note_input();
    switch ((button_event_t)(intptr_t)event) {
    case BUTTON_PRESS_DOWN:
        taskENTER_CRITICAL(&s_rg_mux);
        rg_press(&s_rg, now_s());
        taskEXIT_CRITICAL(&s_rg_mux);
        atomic_store(&s_hold_seen, false);
        atomic_store(&s_turned_while_down, false);
        atomic_store(&s_button_down, true);
        break;
    case BUTTON_LONG_PRESS_HOLD:
        atomic_store(&s_hold_seen, true);
        break;
    case BUTTON_PRESS_UP: {
        taskENTER_CRITICAL(&s_rg_mux);
        bool reset_press = rg_release(&s_rg, now_s());
        taskEXIT_CRITICAL(&s_rg_mux);
        atomic_store(&s_button_down, false);
        pr_press_t press = pr_release(reset_press, atomic_load(&s_turned_while_down), atomic_load(&s_hold_seen));
        if (press != PR_PRESS_NONE) atomic_fetch_add(press == PR_PRESS_LONG ? &s_long_pushes : &s_pushes, 1);
        break;
    }
    default:
        break;
    }
}

static void touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p = {0};
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &p);
    double now = now_s(), report = bsp_knob_15_md50et_touch_report_us() / 1e6;
    ts_result_t r = TS_NONE;
    if (code == LV_EVENT_PRESSED) {
        ota_client_note_input();
        atomic_fetch_add(&s_np_wakes, 1);
        r = ts_press(&s_touch, p.x, p.y, report, s_ks_lv->swipe_pages);
    } else if (code == LV_EVENT_PRESSING) {
        ts_move(&s_touch, p.x, p.y, report, now);
    } else if (code == LV_EVENT_RELEASED) {
        r = ts_release(&s_touch, p.x, p.y, report, now);
    } else {
        ts_cancel(&s_touch);
    }
    if (r == TS_TAP) atomic_fetch_add(&s_touches, 1);
    else if (r != TS_NONE) atomic_fetch_add(&s_swipes, r == TS_NEXT ? 1 : -1);
}

static void wipe_rect(lv_layer_t *layer, const lv_draw_rect_dsc_t *d, int a, int b)
{
    int y1, y2;
    if (!ts_wipe_span(a, b, s_wipe_dir, &y1, &y2)) return;
    lv_area_t area = {0, y1, SCREEN_W - 1, y2};
    lv_draw_rect(layer, d, &area);
}

static void wipe_draw_cb(lv_event_t *e)
{
    int edge = s_wipe_edge;
    if (!s_wipe_dir || edge <= -TS_WIPE_BAND_PX) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_black();
    d.bg_opa = LV_OPA_COVER;
    wipe_rect(layer, &d, 0, edge);
    for (int dy = 0; dy < TS_WIPE_BAND_PX; dy += 2) {
        d.bg_opa = (lv_opa_t)ts_wipe_band_opa(dy);
        wipe_rect(layer, &d, edge + dy, edge + dy + 2);
    }
}

static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void page_fx_create(void)
{
    s_wipe = plain_obj(lv_layer_top());
    lv_obj_set_size(s_wipe, SCREEN_W, TS_H);
    lv_obj_add_event_cb(s_wipe, wipe_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

static void wipe_start(int dir, double t)
{
    s_wipe_dir = dir;
    s_wipe_t0 = t;
    s_wipe_edge = TS_H;
    lv_obj_invalidate(s_wipe);
}

static void dots_show(double until)
{
    if (!s_dots) {
        int h = s_nav.n * DOT_PITCH - (DOT_PITCH - DOT_PX);
        s_dots = plain_obj(lv_layer_top());
        lv_obj_set_size(s_dots, DOT_PX, h);
        lv_obj_set_pos(s_dots, DOTS_X, (TS_CY - h / 2) & ~1);
        for (int i = 0; i < s_nav.n; i++) {
            lv_obj_t *d = plain_obj(s_dots);
            lv_obj_set_size(d, DOT_PX, DOT_PX);
            lv_obj_set_pos(d, 0, i * DOT_PITCH);
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        }
    }
    for (int i = 0; i < (int)lv_obj_get_child_count(s_dots); i++)
        lv_obj_set_style_bg_color(lv_obj_get_child(s_dots, i), lv_color_hex(i == s_nav.pos ? 0xFFFFFF : 0x4A4A4A), 0);
    s_dots_until = until;
}

static void dots_refresh(void)
{
    if (!s_dots) return;
    lv_obj_delete(s_dots);
    s_dots = NULL;
    if (s_nav.n > 1) dots_show(s_dots_until);
}

static void page_fx_frame(double t)
{
    if (s_dots && t >= s_dots_until) {
        lv_obj_delete(s_dots);
        s_dots = NULL;
    }
    if (!s_wipe_dir) return;
    int e = ts_wipe_edge(t - s_wipe_t0);
    if (e == s_wipe_edge) return;
    int y1, y2;
    if (ts_wipe_rows(s_wipe_edge, e, s_wipe_dir, &y1, &y2)) {
        lv_area_t a = {0, y1, SCREEN_W - 1, y2};
        lv_obj_invalidate_area(s_wipe, &a);
    }
    s_wipe_edge = e;
    if (e <= -TS_WIPE_BAND_PX) s_wipe_dir = 0;
}

void page_bot_show(bool on)
{
    if (!s_bot_page) return;
    if (on) lv_obj_remove_flag(s_bot_page, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_bot_page, LV_OBJ_FLAG_HIDDEN);
    if (on) s_drawn = false;
}

void page_pomo_show(bool on) { pomo_view_show(on); }
void page_weather_show(bool on) { weather_view_show(on); }
void page_np_show(bool on) { np_view_show(on); }

static bool s_ota_paused;

static void pages_pause(void)
{
    pages_hide_all();
    if (s_wipe) s_wipe_dir = 0;
    if (s_dots) {
        lv_obj_delete(s_dots);
        s_dots = NULL;
    }
    lv_obj_add_flag(s_reset_layer, LV_OBJ_FLAG_HIDDEN);
    s_reset_shown = RG_IDLE;
}

static void drop_inputs(void)
{
    atomic_store(&s_page_steps, 0);
    atomic_store(&s_swipes, 0);
    atomic_store(&s_np_wakes, 0);
    atomic_store(&s_detents, 0);
    atomic_store(&s_pushes, 0);
    atomic_store(&s_touches, 0);
    atomic_store(&s_long_pushes, 0);
}

static bool ota_frame(void)
{
    if (ota_face_frame()) {
        if (!s_ota_paused) {
            s_ota_paused = true;
            pages_pause();
        }
        taskENTER_CRITICAL(&s_rg_mux);
        rg_init(&s_rg);
        taskEXIT_CRITICAL(&s_rg_mux);
        drop_inputs();
        return true;
    }
    if (s_ota_paused) {
        s_ota_paused = false;
        pages_resume(&s_nav);
    }
    return false;
}

static void page_go(int steps, double t)
{
    if (!pages_step(&s_nav, steps)) return;
    wipe_start(steps > 0 ? 1 : -1, t);
    dots_show(t + TS_DOTS_S);
}

struct page_frame {
    double t;
    bool offline;
    bot_pose_t pose;
    gc_out_t chase;
};

void page_pomo_input(const page_input_t *in, double t)
{
    (void)t;
    for (int n = in->pushes; n > 0; n--) pomo_client_action(POMO_INPUT_PUSH);
    for (int n = in->longs; n > 0; n--) pomo_client_action(POMO_INPUT_LONG_PUSH);
}

void page_pomo_frame(const page_frame_t *f)
{
    pomo_snapshot_t snap;
    pomo_client_get(&snap);
    pomo_est_t e = pomo_estimate(&snap.clock, !snap.online, f->t);
    if (snap.disabled) e.has_state = false;
    pomo_view_set_note(pomo_client_note(&snap, f->t));
    pomo_view_update(&e, f->t);
}

void page_np_input(const page_input_t *in, double t)
{
    if (in->detents || in->pushes || in->longs || in->touches || in->wakes) np_view_input(t);
    if (in->detents) np_view_turn(in->detents, t);
    for (int n = in->pushes; n > 0; n--) np_view_push(t);
    for (int n = in->longs; n > 0; n--) np_view_long_push(t);
}

void page_np_frame(const page_frame_t *f) { np_view_update(f->t, f->offline); }

static void reset_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGW(TAG, "factory reset: erasing settings and Wi-Fi, rebooting");
    esp_err_t err = config_store_erase();
    if (err != ESP_OK) ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
    ember_client_forget_wifi();
    esp_restart();
}

static void reset_overlay_create(void)
{
    s_reset_layer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_reset_layer);
    lv_obj_set_size(s_reset_layer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_reset_layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_reset_layer, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_reset_layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_reset_arc = lv_arc_create(s_reset_layer);
    lv_obj_set_size(s_reset_arc, 400, 400);
    lv_obj_center(s_reset_arc);
    lv_arc_set_bg_angles(s_reset_arc, 0, 360);
    lv_arc_set_rotation(s_reset_arc, 270);
    lv_arc_set_range(s_reset_arc, 0, RG_DETENTS);
    lv_arc_set_value(s_reset_arc, 0);
    lv_obj_remove_style(s_reset_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_reset_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_reset_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_reset_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_reset_arc, lv_color_hex(0x262626), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_reset_arc, lv_color_hex(0xE5484D), LV_PART_INDICATOR);

    s_reset_label = lv_label_create(s_reset_layer);
    lv_obj_set_style_text_font(s_reset_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_reset_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_reset_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_reset_label, "Reset knob?\nTurn right to confirm");
    lv_obj_center(s_reset_label);
    lv_obj_add_flag(s_reset_layer, LV_OBJ_FLAG_HIDDEN);
}

static void settings_frame(double t)
{
    uint32_t gen = config_store_settings_gen();
    if (gen == s_settings_gen) return;
    bool first = s_settings_gen == 0;
    s_settings_gen = gen;
    config_store_settings(s_ks_lv);
    pages_settings(&s_nav, s_ks_lv, first);
    dots_refresh();
    bot_set_sleep_after(&s_bot, s_ks_lv->sleepy_after_s);
    s_demo_hold_s = s_ks_lv->demo_hold_s;
    bot_view_set_options(s_ks_lv->source_label, s_ks_lv->working_ring);
    if (bsp_knob_15_md50et_set_rotation(s_ks_lv->rotation) != ESP_OK) ESP_LOGW(TAG, "rotation %d failed", s_ks_lv->rotation);
    if (s_demo_until > t + s_demo_hold_s) s_demo_until = t + s_demo_hold_s;
}

static void join_frame(void)
{
    int show = s_setup                                                              ? 0
               : !ember_client_online() && ember_client_join_failures() >= 3          ? 1
               : ember_client_link() == EMBER_LINK_UNAUTHORIZED                      ? 2
               : config_store_has_ember_url() && !config_store_has_device()         ? 2
               : ember_client_view_compat() == EMBER_VIEW_UPDATE_KNOB                ? 3
               : ember_client_view_compat() == EMBER_VIEW_UPDATE_EMBER               ? 4
                                                                                      : 0;
    if (show == s_join_shown) return;
    s_join_shown = show;
    if (show == 1) {
        char ssid[CFG_SSID_MAX + 1], text[CFG_SSID_MAX + 16];
        config_store_ssid(ssid, sizeof ssid);
        snprintf(text, sizeof text, "Can't join\n%s", ssid);
        lv_label_set_text(s_join_label, text);
    } else if (show == 2) {
        lv_label_set_text(s_join_label, "Not paired");
    } else if (show >= 3) {
        lv_label_set_text(s_join_label, show == 3 ? "Update knob" : "Update Ember");
    }
    if (show) lv_obj_remove_flag(s_join_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_join_label, LV_OBJ_FLAG_HIDDEN);
}

static void join_label_create(void)
{
    s_join_label = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(s_join_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_join_label, lv_color_hex(0xF5A623), 0);
    lv_obj_set_style_text_align(s_join_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_join_label, 300);
    lv_label_set_long_mode(s_join_label, LV_LABEL_LONG_DOT);
    lv_obj_align(s_join_label, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_add_flag(s_join_label, LV_OBJ_FLAG_HIDDEN);
}

static bool reset_frame(double t)
{
    taskENTER_CRITICAL(&s_rg_mux);
    rg_state_t st = rg_tick(&s_rg, t);
    int progress = s_rg.progress;
    taskEXIT_CRITICAL(&s_rg_mux);
    if (st != s_reset_shown) {
        if (st == RG_IDLE) {
            lv_obj_add_flag(s_reset_layer, LV_OBJ_FLAG_HIDDEN);
            s_drawn = false;
        } else {
            lv_obj_remove_flag(s_reset_layer, LV_OBJ_FLAG_HIDDEN);
        }
        if (st == RG_CONFIRMED) {
            lv_label_set_text(s_reset_label, "Resetting");
            xTaskCreatePinnedToCore(reset_task, "reset", 5120, NULL, 4, NULL, 1);
        }
        s_reset_shown = st;
    }
    if (st != RG_IDLE && progress != s_reset_progress_shown) {
        lv_arc_set_value(s_reset_arc, progress);
        s_reset_progress_shown = progress;
    }
    return st != RG_IDLE;
}

static void render_freeze(lv_display_t *disp)
{
    static bool frozen;
    bool want = ota_client_render_frozen();
    if (want == frozen) return;
    frozen = want;
    lv_display_enable_invalidation(disp, !want);
    if (want) ESP_LOGW(TAG, "OTA test fault: screen frozen");
    else lv_obj_invalidate(lv_screen_active());
}

void page_bot_input(const page_input_t *in, double t)
{
    if (in->detents) {
        s_ring_deg += in->detents * DEGREES_PER_DETENT;
        double a = s_ring_deg * M_PI / 180;
        bot_look(&s_bot, sin(a) * 0.85, cos(a) * 0.85, t);
    }
    for (int n = in->pushes; n > 0; n--) bot_push(&s_bot, t);
    for (int n = in->longs; n > 0; n--) {
        s_demo_index = (s_demo_index + 1) % (int)(sizeof DEMO_MOODS / sizeof DEMO_MOODS[0]);
        bot_set_mood_user(&s_bot, DEMO_MOODS[s_demo_index], t);
        s_demo_until = t + s_demo_hold_s;
    }
}

void page_bot_frame(const page_frame_t *f)
{
    double t = f->t;
    const bot_pose_t *p = &f->pose;
    const gc_out_t *chase = &f->chase;
    ember_host_info_t host;
    ember_client_host(&host);
    bot_view_set_host(&host);
    bot_view_set_offline(f->offline);
    bool pose = !s_drawn || !bot_pose_same(p, &s_last_pose, bot_view_radius_px());
    if (pose && s_drawn && chase->chasing && t - s_pose_drawn_at < 0.75 / atomic_load(&s_chase_fps)) pose = false;
    bot_view_chase(chase->chasing, chase->chasing && chase->label_opa > 0);
    if (pose) s_pose_drawn_at = t;
    if (pose) {
        bot_view_update(p);
        s_last_pose = *p;
        s_drawn = true;
        s_frames++;
        s_frame_kind |= 1;
    }
    if (bot_view_tick(t, pose && !chase->chasing)) s_frame_kind |= 2;
    bot_view_label_fx(chase->label_opa, chase->shift_x, chase->shift_y);
}

static __attribute__((noinline)) void page_tick(double t, double dt)
{
    const page_desc_t *pg = pages_current(&s_nav);
    page_input_t in;
    in.detents = atomic_exchange(&s_detents, 0);
    in.pushes = atomic_exchange(&s_pushes, 0);
    in.touches = atomic_exchange(&s_touches, 0);
    in.longs = atomic_exchange(&s_long_pushes, 0);
    in.wakes = atomic_exchange(&s_np_wakes, 0);

    bool offline = ember_client_offline();
    wx_obs_t wx;
    bool have_wx = weather_client_get(&wx);
    weather_view_update(have_wx ? &wx : NULL, offline, dt);

    bool calm = bot_calm(ember_client_quiet(), s_ks_lv->quiet_calm);
    bot_set_calm(&s_bot, calm, t);
    pages_dispatch(&s_nav, in, t);
    bot_mood_t em;
    if (t >= s_demo_until && offline) bot_set_mood(&s_bot, BOT_IDLE, t);
    else if (t >= s_demo_until && ember_client_mood(&em)) bot_set_mood(&s_bot, em, t);

    float gdeg;
    bool glint = bot_chase_on((pg->flags & PAGE_GLINT) && bot_view_glint_deg(t, &gdeg), calm, s_chase.phase != GC_IDLE);
    int req = atomic_exchange(&s_chase_req, -1);
    if (req >= 0 && glint && bot_anim_allowed(BOT_ANIM_GLINT_CHASE, calm) && gc_start_now(&s_chase, req & 15, req >> 4)) {
        gc_half_reset(&s_half);
        s_half_hold_deg = 0;
        s_half_snapped = false;
    }
    page_frame_t f = {.t = t, .offline = offline};
    gc_out_t *chase = &f.chase;
    gc_tick(&s_chase, t, glint, 3.0, chase);
    atomic_store(&s_chase_status, !glint ? 2 : s_chase.phase != GC_IDLE ? 1 : 0);
    double psi = fmod(t / 3.0, 1.0) * 360.0 - 90.0;
    bot_pose_t ref = {.mood = BOT_WORKING, .eyes = s_last_pose.eyes, .scale_x = 1, .scale_y = 1};
    if (chase->chasing && glint && chase->style == GC_STYLE_HALF) {
        gc_half_out_t h;
        gc_half_step(&s_half, psi, CHASE_LEAD_DEG, &h);
        if (h.follow) {
            s_half_snapped = false;
            s_half_hold_deg = h.target_deg;
            chase_track(&ref, h.target_deg, chase->orbit, t);
        } else {
            bot_hold_gaze(&s_bot, t + 0.5);
            if (h.blink) bot_blink(&s_bot, t);
            if (h.snap) {
                if (s_bot.tracking) bot_track_end(&s_bot, t);
                s_half_snapped = true;
                bot_look_still(&s_bot, chase_gaze(&ref, 0, chase->orbit), 0, t);
            } else if (!s_half_snapped) {
                chase_track(&ref, s_half_hold_deg, chase->orbit, t);
            }
        }
    } else if (chase->chasing && glint) {
        chase_track(&ref, psi + CHASE_LEAD_DEG, chase->orbit, t);
    } else if (s_bot.tracking) {
        bot_track_end(&s_bot, t);
    }

    f.pose = bot_pose(&s_bot, t);
    f.pose.orbit = chase->chasing ? chase->orbit : 0;
    s_cur_mood = f.pose.mood;
    pages_frame(&s_nav, &f);
}

static atomic_uint s_loop_ticks;

static void frame_cb(lv_timer_t *timer)
{
    panel_check_frame();
    screen_snap_reap();
    render_freeze(lv_timer_get_user_data(timer));
    if (ota_client_render_frozen()) return;
    ota_client_note_loop();
    atomic_fetch_add_explicit(&s_loop_ticks, 1, memory_order_relaxed);
    double t = now_s();
    double dt = s_last_frame_t > 0 ? t - s_last_frame_t : 0;
    s_last_frame_t = t;
    join_frame();
    screen_snap_frame();
    if (!s_setup && ota_frame()) return;
    if (reset_frame(t) || s_setup) {
        drop_inputs();
        return;
    }

    settings_frame(t);
    int steps = atomic_exchange(&s_page_steps, 0);
    int swipes = atomic_exchange(&s_swipes, 0);
    if (s_ks_lv->swipe_pages) steps += swipes;
    if (steps) page_go(steps, t);
    page_fx_frame(t);
    page_tick(t, dt);
}

static void setup_view_create(lv_obj_t *scr)
{
    lv_obj_t *ring = lv_obj_create(scr);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 300, 300);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 4, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x3A3A3A), 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *msg = lv_label_create(scr);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0xBDBDBD), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(msg, "Connect to a Mac\nwith Ember");
    lv_obj_center(msg);

    char id[7], line[16];
    config_store_short_id(id);
    snprintf(line, sizeof line, "ID %s", id);
    lv_obj_t *idl = lv_label_create(scr);
    lv_obj_set_style_text_font(idl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(idl, lv_color_hex(0x6E6E6E), 0);
    lv_label_set_text(idl, line);
    lv_obj_align(idl, LV_ALIGN_CENTER, 0, 185);
}

#define STATS_PERIOD_MS 30000
#define LVGL_STALL_CHECKS 2

static void lvgl_stall_check(void)
{
    static unsigned last;
    static int same;
    unsigned tick = atomic_load(&s_loop_ticks);
    if (tick != last || ota_client_verifying() || ota_face_requested()) same = 0;
    else same++;
    last = tick;
    if (same > LVGL_STALL_CHECKS) return;
    int s = LVGL_STALL_CHECKS * STATS_PERIOD_MS / 1000;
    switch (diag_stall_check(same == LVGL_STALL_CHECKS)) {
    case DEV_STALL_ABORT:
        ESP_LOGE(TAG, "LVGL loop stalled for %d s: restarting", s);
        abort();
    case DEV_STALL_LOG:
        ESP_LOGE(TAG, "LVGL loop stalled for %d s: not restarting (%d stall resets in a row)", s, DEV_STALL_MAX_RESETS);
        break;
    case DEV_STALL_NONE:
        break;
    }
}

static void stats_task(void *arg)
{
    (void)arg;
    static EXT_RAM_BSS_ATTR http_conn_stats_t prev, ns;
    static EXT_RAM_BSS_ATTR refr_kind_t kc[4];
    static EXT_RAM_BSS_ATTR dev_wifi_t w;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATS_PERIOD_MS));
        diag_track();
        orbit_reap();
        double t = now_s();
        int frames = s_frames, n = s_refr_count;
        int64_t total = s_refr_total_us, mx = s_refr_max_us;
        s_frames = 0;
        s_refr_count = 0;
        s_refr_total_us = s_refr_max_us = 0;
        http_conn_get_stats(&ns);
        ESP_LOGI(TAG, "pose redraws %.1f/s | screen refreshes %d, avg %.1f ms, max %.1f ms | mood %s | page %d",
                 frames / (t - s_fps_since), n, n ? total / 1000.0 / n : 0.0, mx / 1000.0, MOOD_NAMES[s_cur_mood],
                 s_nav.page);
        ESP_LOGI(TAG, "glint chase: phase %d, working %.0f of %.0f s, laps %d, eyes %d fps", (int)s_chase.phase,
                 s_chase.work_s, s_chase.wait_s, s_chase.laps, atomic_load(&s_chase_fps));
        static const char *const KIND[4] = {"other", "pose", "glint", "pose+glint"};
        memcpy(kc, s_kind, sizeof kc);
        memset(s_kind, 0, sizeof s_kind);
        for (int i = 0; i < 4; i++)
            if (kc[i].n)
                ESP_LOGI(TAG, "refresh %s: %d, avg %.1f ms, max %.1f ms, avg %lld px, max %d areas", KIND[i], kc[i].n,
                         kc[i].us / 1000.0 / kc[i].n, kc[i].us_max / 1000.0, kc[i].px / kc[i].n, kc[i].areas_max);
        ESP_LOGI(TAG, "net: %" PRIu32 " req (%" PRIu32 " x 304), %" PRIu32 " B in, %" PRIu32 " failed, %" PRIu32
                 " retried, %" PRIu32 " new connections | internal free %u B (largest %u B) | PSRAM free %u KB",
                 ns.requests - prev.requests, ns.not_modified - prev.not_modified, ns.rx_bytes - prev.rx_bytes,
                 ns.failures - prev.failures, ns.retries - prev.retries,
                 ns.connects - prev.connects, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        int rssi = 0;
        bool assoc = ember_client_rssi(&rssi);
        ember_client_wifi(&w);
        ESP_LOGI(TAG, "wifi: RSSI %d dBm (min %d), channel %d, %" PRIu32 " disconnects (last reason %d), %u beacon timeouts",
                 assoc ? rssi : 0, w.has_rssi_min ? w.rssi_min : 0, w.channel, w.disconnects, w.last_reason,
                 ember_client_beacon_timeouts());
        static EXT_RAM_BSS_ATTR char stacks[DIAG_STACKS_LINE_MAX];
        diag_stacks_line(stacks);
        ESP_LOGI(TAG, "stack free B: %s", stacks);
        lvgl_stall_check();
        prev = ns;
        s_fps_since = t;
    }
}

static void psram_task(TaskFunction_t fn, const char *name, uint32_t stack, UBaseType_t prio, BaseType_t core,
                       TaskHandle_t *out)
{
    if (xTaskCreatePinnedToCoreWithCaps(fn, name, stack, NULL, prio, out, core, MALLOC_CAP_SPIRAM) != pdPASS)
        ESP_LOGE(TAG, "%s task not created", name);
}

static void *json_malloc(size_t n) { return heap_caps_malloc_prefer(n, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT); }

void app_main(void)
{
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = json_malloc, .free_fn = free});
    ESP_LOGI(TAG, "cinder starting");
    http_conn_quiet_idf_logs();
    config_store_init();
    diag_boot();
    ota_client_boot();
    s_setup = !config_store_provisioned();
    s_ks_lv = heap_caps_calloc(1, sizeof *s_ks_lv, MALLOC_CAP_SPIRAM);
    assert(s_ks_lv);
    rg_init(&s_rg);

    /* Wi-Fi starts before the display: its RX buffers need internal DMA RAM (docs/features.md, internal RAM budget). */
    if (!s_setup) {
        char url[CFG_URL_MAX + 1];
        config_store_ember_url(url, sizeof url);
        if (url[0]) {
            pomo_client_init();
            weather_client_init();
            np_client_init();
        }
    }
    ember_client_start();

    ESP_LOGI(TAG, "before display: free internal %u B, largest DMA block %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    config_store_settings(s_ks_lv);
    bsp_knob_15_md50et_set_qspi_fast(s_ks_lv->fast_link);
    bsp_knob_15_md50et_set_rotation(s_ks_lv->rotation);
    bsp_knob_15_md50et_handles_t handles = {0};
    ESP_ERROR_CHECK(bsp_knob_15_md50et_init(&handles));
    bsp_knob_15_md50et_register_knob_cb(knob_cb);
    bsp_knob_15_md50et_register_button_cb(button_cb);
    panel_check_init(handles.panel, handles.panel_io);

    bot_init(&s_bot, 0x454D4252ULL, now_s());
    s_fps_since = now_s();

    ESP_ERROR_CHECK(bsp_knob_15_md50et_lock(-1));
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    if (s_setup) {
        setup_view_create(scr);
    } else {
        s_bot_page = lv_obj_create(scr);
        lv_obj_remove_style_all(s_bot_page);
        lv_obj_set_size(s_bot_page, lv_pct(100), lv_pct(100));
        lv_obj_remove_flag(s_bot_page, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        bot_view_create(s_bot_page);
        pomo_view_create(scr);
        weather_view_create(scr);
        np_view_create(scr);
        pages_show(&s_nav, 0);
        lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESS_LOST, NULL);
        page_fx_create();
    }
    reset_overlay_create();
    join_label_create();
    gc_init(&s_chase, esp_random());
    bot_orbit_table_init(&s_orbit, BOT_VIEW_EYE_SCALE, CHASE_OUTER);
    lv_timer_create(frame_cb, 16, handles.disp);
    lv_display_add_event_cb(handles.disp, refr_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(handles.disp, refr_cb, LV_EVENT_REFR_READY, NULL);
    bsp_knob_15_md50et_unlock();

    config_store_settings(s_ks_lv);
    uint8_t startup = s_ks_lv->startup;
    panel_check_brightness(startup);
    if (!s_setup) ember_client_dim_enable(startup);
    if (!s_setup) psram_task(orbit_task, "orbit", 3328, 1, 1, &s_orbit_task);
    psram_task(stats_task, "stats", 3840, 1, tskNO_AFFINITY, NULL);
    psram_task(link_task, "link", 4096, 1, 1, NULL);
    provision_usb_start();
    ESP_LOGI(TAG, "%s ready; free internal %u KB (largest %u KB)", s_setup ? "setup face" : "bot face",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024));
}
