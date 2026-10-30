#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
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
#include "controls_view.h"
#include "day_client.h"
#include "day_view.h"
#include "pomo_client.h"
#include "pomo_view.h"
#include "nowplaying_client.h"
#include "nowplaying_view.h"
#include "ota_client.h"
#include "ota_face.h"
#include "overlays.h"
#include "page_canvas.h"
#include "page_frame.h"
#include "page_fx.h"
#include "page_ops.h"
#include "pages.h"
#include "provision_usb.h"
#include "screen_snap.h"
#include "reset_gesture.h"
#include "scene.h"
#include "stats_task.h"
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

static bool s_setup;
static join_label_t s_join_shown;
static int s_join_code = -1;
static pages_nav_t s_nav = {.n = 1};
static uint32_t s_settings_gen;
static knob_settings_t *s_ks_lv;
static lv_obj_t *s_bot_page;
static double s_last_frame_t;

static bot_t s_bot;
static bot_pose_t s_last_pose;
static bool s_drawn;
static EXT_RAM_BSS_ATTR bool s_usage_on;
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

static bool s_ota_paused;

static bool ota_frame(void)
{
    if (ota_face_frame()) {
        if (!s_ota_paused) {
            s_ota_paused = true;
            ember_client_demo_input();
            if (scene_on()) scene_end(now_s());
            scene_resume_cancel();
            pages_hide_all();
            page_fx_pause();
            overlays_pause();
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

static void settings_frame(double t)
{
    uint32_t gen = config_store_settings_gen();
    if (gen == s_settings_gen) return;
    bool first = s_settings_gen == 0;
    s_settings_gen = gen;
    config_store_settings(s_ks_lv);
    pages_settings(&s_nav, s_ks_lv, first);
    dots_refresh(&s_nav);
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
    if (overlays_reset_frame(st, progress)) s_drawn = false;
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

static __attribute__((noinline)) void page_tick(double t, double dt)
{
    const page_desc_t *pg = pages_current(&s_nav);
    page_input_t in;
    overlays_hold_tick(pg, input_route_take(&in));
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
    if (steps) page_go(&s_nav, steps, t);
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
    overlays_create();
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
