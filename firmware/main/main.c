#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "diag.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "cJSON.h"

#include "bot_behavior.h"
#include "bot_view.h"
#include "chase_ctl.h"
#include "bot_shape.h"
#include "panel_check.h"
#include "esp_random.h"
#include "config_store.h"
#include "dim.h"
#include "dim_ctl.h"
#include "ember_client.h"
#include "ember_host.h"
#include "http_conn.h"
#include "input_route.h"
#include "join_view.h"
#include "link_task.h"
#include "claim_client.h"
#include "controls_client.h"
#include "controls_view.h"
#include "day_client.h"
#include "day_view.h"
#include "pomo_client.h"
#include "pomo_view.h"
#include "nowplaying_client.h"
#include "nowplaying_view.h"
#include "ota_client.h"
#include "ota_face.h"
#include "page_canvas.h"
#include "page_ops.h"
#include "pages.h"
#include "press_route.h"
#include "provision_usb.h"
#include "screen_snap.h"
#include "reset_gesture.h"
#include "scene.h"
#include "stats_task.h"
#include "touch_swipe.h"
#include "weather_client.h"
#include "temp_face.h"
#include "usage_client.h"
#include "usage_view.h"
#include "moment_view.h"
#include "moments.h"
#include "music_view.h"
#include "shown.h"
#include "wx_shift.h"
#include "bsp_knob_15_md50et.h"

static const char *TAG = "cinder";

#define DEGREES_PER_DETENT 15.0

static lv_obj_t *s_reset_layer, *s_reset_arc, *s_reset_label;
static rg_state_t s_reset_shown = RG_IDLE;
static int s_reset_progress_shown = -1;

static bool s_setup;
static join_label_t s_join_shown;
static int s_join_code = -1;
static pages_nav_t s_nav = {.n = 1};
static uint32_t s_settings_gen;
static knob_settings_t *s_ks_lv;
static lv_obj_t *s_bot_page;
static double s_last_frame_t;
#define SCREEN_W 472
#define DOT_PX 8
#define DOT_PITCH 16
#define DOTS_X 442
static lv_obj_t *s_wipe, *s_dots;
static int s_wipe_dir, s_wipe_edge = -TS_WIPE_BAND_PX;
static EXT_RAM_BSS_ATTR double s_wipe_t0, s_dots_until;

static bot_t s_bot;
static bot_pose_t s_last_pose;
static bool s_drawn;
static EXT_RAM_BSS_ATTR bool s_usage_on;
static EXT_RAM_BSS_ATTR day_ui_t s_day_ui;
static EXT_RAM_BSS_ATTR ctl_batch_t s_ctl_batch;
static EXT_RAM_BSS_ATTR wx_shift_t s_bot_shift;
static double s_ring_deg;
static int s_demo_index;
static double s_demo_until;
static double s_demo_hold_s = 20.0;

static bool s_bot_shown;
static ember_host_info_t s_wait_host;
static double s_pose_drawn_at;
static atomic_int s_frames;
static atomic_int s_cur_mood;

static const bot_mood_t DEMO_MOODS[] = {BOT_IDLE, BOT_WORKING, BOT_WAITING, BOT_DONE, BOT_ERROR};

static double now_s(void) { return esp_timer_get_time() / 1e6; }

static EXT_RAM_BSS_ATTR lv_obj_t *s_hold_layer, *s_hold_arc, *s_hold_label;
static EXT_RAM_BSS_ATTR const char *s_hold_text;
static EXT_RAM_BSS_ATTR int s_hold_pct = -1;

#define HOLD_R 204

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

void wipe_start(int dir, double t)
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
    if (!on) {
        usage_view_close();
        s_usage_on = false;
    }
    if (on && s_usage_on) return;
    if (on && lv_obj_has_flag(s_bot_page, LV_OBJ_FLAG_HIDDEN)) s_bot_shown = true;
    if (on) lv_obj_remove_flag(s_bot_page, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_bot_page, LV_OBJ_FLAG_HIDDEN);
    if (on) s_drawn = false;
}

void page_pomo_show(bool on) { pomo_view_show(on); }
void page_day_show(bool on)
{
    day_view_show(on);
    day_act_t a[DAY_ACTS];
    if (!on)
        for (int i = 0, n = day_ui_leave(&s_day_ui, now_s(), a); i < n; i++) day_client_act(&a[i]);
}
void page_weather_show(bool on) { temp_face_show(on); }

void page_weather_input(const page_input_t *in, double t)
{
    (void)t;
    if (in->detents) temp_face_turn(in->detents);
}

void page_np_show(bool on) { np_view_show(on); }

void page_controls_show(bool on)
{
    controls_view_show(on);
    ctl_batch_init(&s_ctl_batch);
}

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

static bool ota_frame(void)
{
    if (ota_face_frame()) {
        if (!s_ota_paused) {
            s_ota_paused = true;
            ember_client_demo_input();
            if (scene_on()) scene_end(now_s());
            scene_resume_cancel();
            pages_pause();
            bot_view_rim_hold(true);
            moment_view_hold(true);
            moment_unact(now_s());
            music_view_hold(true);
            music_unact(now_s());
        }
        temp_face_hold(true);
        input_route_reset_clear();
        input_route_drop();
        return true;
    }
    if (s_ota_paused) {
        s_ota_paused = false;
        bot_view_rim_hold(false);
        moment_view_hold(false);
        music_view_hold(false);
        temp_face_hold(false);
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
    double t, dt;
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

static day_in_t day_now(day_snapshot_t *snap)
{
    day_client_get(snap);
    double now = pomo_client_now();
    bool exact;
    int now_min = day_client_now_min(snap, now, &exact);
    return (day_in_t){.b = snap->has ? &snap->b : NULL, .fed = snap->fed, .age_s = now - snap->fed_at,
                      .now_min = now_min, .now_exact = exact, .busy = day_client_busy(snap, now)};
}

static void day_send(const day_act_t *a, int n)
{
    for (int i = 0; i < n; i++) day_client_act(&a[i]);
}

void page_day_input(const page_input_t *in, double t)
{
    day_snapshot_t snap;
    day_in_t di = day_now(&snap);
    day_act_t a[DAY_ACTS];
    day_ui_sync(&s_day_ui, &di, t);
    if (in->detents) day_send(a, day_ui_turn(&s_day_ui, &di, in->detents, t, a));
    if (in->pushes) day_send(a, day_ui_push(&s_day_ui, &di, t, a));
    else if (in->longs) day_send(a, day_ui_long(&s_day_ui, &di, t, a));
}

void page_day_frame(const page_frame_t *f)
{
    static EXT_RAM_BSS_ATTR day_snapshot_t snap;
    static EXT_RAM_BSS_ATTR day_model_t m;
    day_in_t di = day_now(&snap);
    day_act_t a[DAY_ACTS];
    day_ui_sync(&s_day_ui, &di, f->t);
    day_send(a, day_ui_tick(&s_day_ui, &di, f->t, a));
    day_model(&s_day_ui, &di, f->t, day_client_note(&snap, pomo_client_now()), &m);
    day_view_update(&m, f->t);
}

void page_controls_input(const page_input_t *in, double t)
{
    if (in->detents) ctl_batch_turn(&s_ctl_batch, in->detents);
    if (in->pushes || in->longs) {
        int steps = ctl_batch_flush(&s_ctl_batch, t);
        if (steps) controls_client_send(CTL_TURN, steps);
    }
    for (int n = in->pushes; n > 0; n--) controls_client_send(CTL_PUSH, 0);
    for (int n = in->longs; n > 0; n--) controls_client_send(CTL_LONG_PUSH, 0);
}

void page_controls_frame(const page_frame_t *f)
{
    int steps = ctl_batch_take(&s_ctl_batch, f->t);
    if (steps) controls_client_send(CTL_TURN, steps);
    static EXT_RAM_BSS_ATTR ctl_state_t st;
    controls_client_get(&st);
    controls_view_update(st.target, ctl_note(st.res, pomo_client_now() - st.at, f->offline));
}

void page_np_input(const page_input_t *in, double t)
{
    if (in->detents || in->pushes || in->longs || in->touches || in->wakes || in->hswipes) np_view_input(t);
    if (in->detents) np_view_turn(in->detents, t);
    for (int n = in->pushes; n > 0; n--) np_view_push(t);
    for (int z = 0; z < PAGE_TAP_ZONES; z++)
        for (int n = in->taps[z]; n > 0; n--) np_view_tap(z, t);
    if (in->hswipes) np_view_skip(in->hswipes, t);
}

void page_np_frame(const page_frame_t *f)
{
    char age[24] = "";
    if (f->offline) ember_client_age_text(age, sizeof age);
    np_view_update(f->t, f->offline, age);
}

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

static void hold_overlay_create(void)
{
    s_hold_layer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_hold_layer);
    lv_obj_set_size(s_hold_layer, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_hold_layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_hold_arc = lv_arc_create(s_hold_layer);
    lv_obj_set_size(s_hold_arc, 2 * HOLD_R, 2 * HOLD_R);
    lv_obj_center(s_hold_arc);
    lv_arc_set_bg_angles(s_hold_arc, 0, 360);
    lv_arc_set_rotation(s_hold_arc, 270);
    lv_arc_set_range(s_hold_arc, 0, 100);
    lv_arc_set_value(s_hold_arc, 0);
    lv_obj_remove_style(s_hold_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_hold_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(s_hold_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_hold_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_hold_arc, lv_color_hex(0xF4F4F2), LV_PART_INDICATOR);
    s_hold_label = lv_label_create(s_hold_layer);
    lv_obj_set_style_text_font(s_hold_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_hold_label, lv_color_hex(0xF4F4F2), 0);
    lv_obj_set_style_bg_color(s_hold_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_hold_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_hold_label, 12, 0);
    lv_obj_set_style_pad_ver(s_hold_label, 4, 0);
    lv_label_set_text_static(s_hold_label, "");
    lv_obj_align(s_hold_label, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_add_flag(s_hold_layer, LV_OBJ_FLAG_HIDDEN);
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
    bot_set_ring(&s_bot, s_ks_lv->working_ring);
    bsp_knob_15_md50et_set_rotation(s_ks_lv->rotation);
    temp_face_set_list(s_ks_lv->wx_faces, s_ks_lv->n_wx_faces);
    pomo_view_set_colors(s_ks_lv->pomo_focus_rgb, s_ks_lv->pomo_break_rgb);
    np_view_set_dim_after(s_ks_lv->np_dim_after_s);
    if (s_demo_until > t + s_demo_hold_s) s_demo_until = t + s_demo_hold_s;
}

static void join_frame(void)
{
    join_in_t in = {
        .setup = s_setup,
        .online = ember_client_online(),
        .join_failures = ember_client_join_failures(),
        .unauthorized = ember_client_link() == EMBER_LINK_UNAUTHORIZED,
        .has_url = config_store_has_ember_url(),
        .has_device = config_store_has_device(),
        .update_knob = ember_client_view_compat() == EMBER_VIEW_UPDATE_KNOB,
        .update_ember = ember_client_view_compat() == EMBER_VIEW_UPDATE_EMBER,
    };
    join_label_t show = join_label_pick(&in);
    int code = show == JOIN_NOT_PAIRED ? claim_client_code() : -1;
    if (show == s_join_shown && code == s_join_code) return;
    s_join_shown = show;
    s_join_code = code;
    char detail[CFG_SSID_MAX + 1] = "";
    if (show == JOIN_CANT_JOIN) config_store_ssid(detail, sizeof detail);
    else if (code >= 0) snprintf(detail, sizeof detail, "%06d", code);
    join_view_show(show, detail);
}

static bool reset_frame(double t)
{
    int progress;
    rg_state_t st = input_route_reset_tick(t, &progress);
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
    usage_in_t u = {.taps = in->touches, .pushes = in->pushes, .longs = in->longs, .detents = in->detents,
                    .hswipes = in->hswipes, .wakes = in->wakes};
    if (usage_view_input(&u, t)) return;
    if (in->detents) {
        s_ring_deg += in->detents * DEGREES_PER_DETENT;
        double a = s_ring_deg * M_PI / 180;
        bot_look(&s_bot, sin(a) * 0.85, cos(a) * 0.85, t);
    }
    for (int n = in->pushes + in->touches; n > 0; n--) bot_push(&s_bot, t);
    for (int n = in->longs; n > 0; n--) {
        s_demo_index = (s_demo_index + 1) % (int)(sizeof DEMO_MOODS / sizeof DEMO_MOODS[0]);
        bot_set_mood_user(&s_bot, DEMO_MOODS[s_demo_index], t);
        s_demo_until = t + s_demo_hold_s;
    }
}

static void bot_under_usage(bool under)
{
    if (under == s_usage_on) return;
    s_usage_on = under;
    if (under) {
        lv_obj_add_flag(s_bot_page, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_bot_page, LV_OBJ_FLAG_HIDDEN);
    s_drawn = false;
    s_bot_shown = true;
}

static void bot_shift_root(void)
{
    int dx, dy;
    wx_shift_xy(&s_bot_shift, &WX_SHIFT_ORBIT, &dx, &dy);
    lv_obj_set_style_translate_x(s_bot_page, dx, 0);
    lv_obj_set_style_translate_y(s_bot_page, dy, 0);
    join_view_shift(dx, dy);
}

void page_bot_frame(const page_frame_t *f)
{
    double t = f->t;
    bot_under_usage(usage_view_frame(t));
    if (s_usage_on) return;
    if (wx_shift_tick(&s_bot_shift, &WX_SHIFT_ORBIT, f->dt)) bot_shift_root();
    const bot_pose_t *p = &f->pose;
    const gc_out_t *chase = &f->chase;
    ember_host_info_t host;
    ember_client_host(&host);
    if (ember_host_lead_new(&s_wait_host, &host)) bot_wait_restart(&s_bot, t, true);
    else if (s_bot_shown) bot_wait_restart(&s_bot, t, false);
    s_bot_shown = false;
    s_wait_host = host;
    bot_view_set_host(&host);
    bot_view_set_offline(join_glyph_shown(f->offline, s_join_shown));
    char age[24] = "";
    if (join_glyph_shown(f->offline, s_join_shown) && s_join_shown == JOIN_NONE) ember_client_age_text(age, sizeof age);
    bot_view_set_age(age);
    bool pose = !s_drawn || !bot_pose_same(p, &s_last_pose, bot_view_radius_px());
    if (pose && s_drawn && chase->chasing && p->face == s_last_pose.face && t - s_pose_drawn_at < 0.75 / chase_ctl_fps())
        pose = false;
    bot_view_chase(chase->chasing || moment_view_busy() || music_view_busy(), chase->chasing && chase->label_opa > 0);
    if (pose) s_pose_drawn_at = t;
    if (pose) {
        bot_view_update(p, t);
        s_last_pose = *p;
        s_drawn = true;
        atomic_fetch_add(&s_frames, 1);
        stats_note_frame_kind(1);
    }
    bot_view_set_hint(usage_view_hint());
    if (bot_view_tick(t, pose && !chase->chasing)) stats_note_frame_kind(2);
    bot_view_label_fx(chase->label_opa, chase->shift_x, chase->shift_y);
    music_view_frame(t);
    moment_view_frame(t);
}

static __attribute__((noinline)) const char *page_day_hold_label(void)
{
    static EXT_RAM_BSS_ATTR day_snapshot_t snap;
    day_in_t di = day_now(&snap);
    return day_hold_label(day_ui_face(&s_day_ui, &di, now_s()));
}

static const char *hold_label(const page_desc_t *pg)
{
    if (pg->frame == page_day_frame) return page_day_hold_label();
    if (pg->frame == page_controls_frame) return "Mac action";
    if (pg->frame != page_pomo_frame) return NULL;
    pomo_snapshot_t snap;
    if (!pomo_client_get(&snap)) return NULL;
    pomo_state_t st = pomo_clock_at(&snap.clock, pomo_client_now());
    return pomo_action_for(&st, POMO_INPUT_LONG_PUSH) == POMO_ACT_NONE ? NULL : "stop";
}

static __attribute__((noinline)) void hold_tick(const page_desc_t *pg, long long held)
{
    const char *label = held >= 0 ? hold_label(pg) : NULL;
    if (label) input_route_hold_counting();
    int pct = label ? (int)((held - PR_COUNTDOWN_MS) * 100 / (PR_HOLD_MS - PR_COUNTDOWN_MS)) : -1;
    if (pct > 100) pct = 100;
    if (!s_hold_layer || pct == s_hold_pct) return;
    if (pct < 0) {
        lv_obj_add_flag(s_hold_layer, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s_hold_pct < 0) lv_obj_remove_flag(s_hold_layer, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_value(s_hold_arc, pct);
        if (label != s_hold_text) lv_label_set_text_static(s_hold_label, label);
    }
    s_hold_text = label;
    s_hold_pct = pct;
}

static __attribute__((noinline)) void page_tick(double t, double dt)
{
    const page_desc_t *pg = pages_current(&s_nav);
    page_input_t in;
    hold_tick(pg, input_route_take(&in));
    usage_view_poll();

    bool offline = ember_client_offline();
    usage_view_set_offline(offline);
    wx_obs_t wx;
    bool have_wx = weather_client_get(&wx);
    temp_face_update(have_wx ? &wx : NULL, offline, dt);
    ember_client_shown(shown_page(pg->id, usage_view_open()), temp_face_resolved());

    ember_demo_status_t ds;
    ember_client_demo_status(&ds);
    bool calm = demo_calm(ds.playing, ds.view_on, ds.quiet, ember_client_quiet(), s_ks_lv->quiet_calm);
    bot_set_calm(&s_bot, calm, t);
    if (scene_gate(t, in.detents || in.pushes || in.touches || in.longs || in.wakes || in.hswipes)) in = (page_input_t){0};
    pages_dispatch(&s_nav, in, t);
    bool bot_page = pg->frame == page_bot_frame && !usage_view_open();
    bot_mood_t em;
    bot_wait_scene(&s_bot, scene_on() ? scene_n() : 0, t);
    if (scene_on() && scene_act_kind() == DEMO_ACT_ASLEEP) bot_sleep(&s_bot, t);
    else if (scene_on() && scene_act_kind() == DEMO_ACT_SLEEPY) bot_set_mood(&s_bot, BOT_SLEEPY, t);
    else if (t >= s_demo_until && offline) bot_set_mood(&s_bot, BOT_IDLE, t);
    else if (t >= s_demo_until && ember_client_mood(&em)) bot_set_mood(&s_bot, em, t);
    if (scene_on()) scene_act(t, have_wx ? &wx : NULL, bot_page);
    else if (!ds.view_on && !scene_resuming()) moments_tick(t, have_wx ? &wx : NULL, calm, bot_page);
    bot_read_moment(&s_bot, moment_view_busy(), t);
    moments_music_tick(t, calm, offline, bot_page, !scene_on() && !ds.view_on && !scene_resuming());

    float gdeg;
    bool glint = (pg->flags & PAGE_GLINT) && !usage_view_open() && bot_view_glint_deg(t, &gdeg);
    chase_ctl_in_t ci = {.t = t, .glint = glint, .calm = calm, .scene = scene_on(), .scene_req = scene_chase_take(), .eyes = s_last_pose.eyes};
    page_frame_t f = {.t = t, .dt = dt, .offline = offline};
    f.chase = chase_ctl_frame(&s_bot, &ci);
    const gc_out_t *chase = &f.chase;

    bot_rim_ready(&s_bot, bot_view_rim_feed(s_bot.mood, s_bot.asleep, bot_page));
    f.pose = bot_pose(&s_bot, t);
    bot_view_rim_pose(&f.pose, t);
    f.pose.eye_dy -= music_view_eye_px(t) / bot_view_radius_px();
    f.pose.orbit = chase->chasing ? chase->orbit : 0;
    atomic_store(&s_cur_mood, f.pose.mood);
    pages_frame(&s_nav, &f);
}

static bool idle_frame(lv_display_t *disp)
{
    static bool held;
    ember_demo_status_t ds;
    ember_client_demo_status(&ds);
    if (ota_face_requested()) dim_ctl_wake(DIM_WAKE_OTA_FACE);
    else if (ds.playing || ds.view_on || scene_on()) dim_ctl_wake(DIM_WAKE_DEMO);
    bool off = dim_ctl_off();
    if (off != held) {
        held = off;
        lv_display_enable_invalidation(disp, !off);
        if (!off) lv_obj_invalidate(lv_screen_active());
        ESP_LOGI(TAG, "screen %s", off ? "off: rendering paused" : "on: rendering resumed");
    }
    if (off) s_last_frame_t = 0;
    return off;
}

static void frame_cb(lv_timer_t *timer)
{
    dim_ctl_frame();
    panel_check_frame();
    bsp_knob_15_md50et_rotation_frame();
    screen_snap_reap();
    render_freeze(lv_timer_get_user_data(timer));
    if (ota_client_render_frozen()) return;
    ota_client_note_loop();
    stats_note_loop();
    if (idle_frame(lv_timer_get_user_data(timer))) return;
    double t = now_s();
    double dt = s_last_frame_t > 0 ? t - s_last_frame_t : 0;
    s_last_frame_t = t;
    join_frame();
    screen_snap_frame();
    if (!s_setup && ota_frame()) return;
    if (reset_frame(t) || s_setup) {
        input_route_drop();
        return;
    }

    settings_frame(t);
    scene_frame(t);
    int steps, swipes;
    input_route_take_steps(&steps, &swipes);
    if (scene_gate(t, steps || swipes)) steps = swipes = 0;
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

bool input_route_classify_touch(void) { return s_ks_lv->swipe_pages || (pages_current(&s_nav)->flags & PAGE_TOUCH_RAW); }

stats_sample_t stats_app_sample(void)
{
    chase_ctl_sample_t c = chase_ctl_sample();
    return (stats_sample_t){.frames = atomic_exchange(&s_frames, 0),
                            .mood = atomic_load(&s_cur_mood),
                            .page = s_nav.page,
                            .chase_phase = c.phase,
                            .chase_laps = c.laps,
                            .chase_fps = c.fps,
                            .chase_work_s = c.work_s,
                            .chase_wait_s = c.wait_s};
}

static void *json_malloc(size_t n) { return heap_caps_malloc_prefer(n, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT); }

void app_main(void)
{
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = json_malloc, .free_fn = free});
    ESP_LOGI(TAG, "cinder starting");
    http_conn_quiet_idf_logs();
    config_store_init();
    diag_boot();
    stats_refr_init();
    ota_client_boot();
    s_setup = !config_store_provisioned();
    s_ks_lv = heap_caps_calloc(1, sizeof *s_ks_lv, MALLOC_CAP_SPIRAM);
    assert(s_ks_lv);
    input_route_init();

    /* Wi-Fi starts before the display: its RX buffers need internal DMA RAM (docs/features.md, internal RAM budget). */
    if (!s_setup) {
        char url[CFG_URL_MAX + 1];
        config_store_ember_url(url, sizeof url);
        if (url[0]) {
            pomo_client_init();
            day_client_init();
            weather_client_init();
            np_client_init();
            usage_client_init();
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
    bsp_knob_15_md50et_register_knob_cb(input_route_knob_cb);
    bsp_knob_15_md50et_register_button_cb(input_route_button_cb);
    panel_check_init(handles.panel, handles.panel_io);

    bot_init(&s_bot, 0x454D4252ULL, now_s());
    stats_fps_start();

    ESP_ERROR_CHECK(bsp_knob_15_md50et_lock(-1));
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    if (s_setup) {
        setup_view_create(scr);
    } else {
        void *page_canvas = heap_caps_malloc(PAGE_CANVAS_BYTES, MALLOC_CAP_SPIRAM);
        assert(page_canvas);
        page_canvas_init(page_canvas, PAGE_CANVAS_BYTES);
        s_bot_page = lv_obj_create(scr);
        lv_obj_remove_style_all(s_bot_page);
        lv_obj_set_size(s_bot_page, lv_pct(100), lv_pct(100));
        lv_obj_remove_flag(s_bot_page, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        bot_view_create(s_bot_page);
        moment_view_create(s_bot_page);
        music_view_create(s_bot_page);
        pomo_view_create(scr);
        day_ui_init(&s_day_ui);
        day_view_create(scr);
        temp_face_create(scr);
        np_view_create(scr);
        controls_view_create(scr);
        usage_view_create(scr);
        pages_show(&s_nav, 0);
        lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(scr, input_route_touch_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(scr, input_route_touch_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(scr, input_route_touch_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(scr, input_route_touch_cb, LV_EVENT_PRESS_LOST, NULL);
        page_fx_create();
    }
    reset_overlay_create();
    hold_overlay_create();
    join_view_create();
    chase_ctl_init(esp_random());
    moments_init(&(moments_ctx_t){.bot = &s_bot, .ks = s_ks_lv});
    scene_init(&(scene_ctx_t){.bot = &s_bot, .nav = &s_nav});
    lv_timer_create(frame_cb, 16, handles.disp);
    lv_display_add_event_cb(handles.disp, stats_refr_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(handles.disp, stats_refr_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(handles.disp, stats_refr_cb, LV_EVENT_REFR_READY, NULL);
    bsp_knob_15_md50et_unlock();

    config_store_settings(s_ks_lv);
    uint8_t startup = s_ks_lv->startup;
    panel_check_brightness(startup);
    if (!s_setup) dim_ctl_enable(startup);
    if (!s_setup) chase_ctl_orbit_start();
    stats_task_start();
    link_task_start();
    provision_usb_start();
    ESP_LOGI(TAG, "%s ready; free internal %u KB (largest %u KB); main task stack headroom %u B",
             s_setup ? "setup face" : "bot face", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
