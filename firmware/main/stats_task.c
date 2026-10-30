#include "stats_task.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "diag.h"
#include "diag_refr.h"
#include "display/lv_display_private.h"
#include "ember_client.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_conn.h"
#include "moment_view.h"
#include "music_view.h"
#include "ota_client.h"
#include "ota_face.h"
#include "refr_stats.h"

static const char *TAG = "cinder";

#define STATS_PERIOD_MS 30000
#define LVGL_STALL_CHECKS 2

static const char *const MOOD_NAMES[] = {"idle", "sleepy", "working", "waiting", "error", "done"};

static double s_fps_since;

static int64_t s_refr_start_us;
static int s_refr_count;
static int64_t s_refr_total_us, s_refr_max_us;
static int s_frame_kind;
static refr_stats_t s_refr;
static portMUX_TYPE s_refr_mux = portMUX_INITIALIZER_UNLOCKED;
static EXT_RAM_BSS_ATTR refr_area_t s_refr_areas[LV_INV_BUF_SIZE];
static atomic_uint s_loop_ticks;

static double now_s(void) { return esp_timer_get_time() / 1e6; }

void stats_refr_init(void)
{
    refr_stats_init(&s_refr, esp_timer_get_time());
    diag_attach_refr(&s_refr, &s_refr_mux);
}

void stats_refr_cb(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    lv_display_t *disp = lv_event_get_current_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_START) {
        s_refr_start_us = now;
        refr_stats_begin(&s_refr, s_frame_kind);
        s_frame_kind = 0;
        return;
    }
    if (code == LV_EVENT_RENDER_START) {
        int n = disp->inv_p > LV_INV_BUF_SIZE ? LV_INV_BUF_SIZE : (int)disp->inv_p;
        for (int i = 0; i < n; i++)
            s_refr_areas[i] = (refr_area_t){disp->inv_areas[i].x1, disp->inv_areas[i].y1, disp->inv_areas[i].x2,
                                            disp->inv_areas[i].y2};
        refr_stats_render(&s_refr, s_refr_areas, disp->inv_area_joined, n);
        return;
    }
    int64_t d = now - s_refr_start_us;
    taskENTER_CRITICAL(&s_refr_mux);
    bool drew = refr_stats_end(&s_refr, d);
    s_refr_count++;
    s_refr_total_us += d;
    if (d > s_refr_max_us) s_refr_max_us = d;
    taskEXIT_CRITICAL(&s_refr_mux);
    if (drew) ota_client_note_frame();
    diag_note_frame(d);
}

void stats_note_loop(void) { atomic_fetch_add_explicit(&s_loop_ticks, 1, memory_order_relaxed); }

void stats_note_frame_kind(int kind) { s_frame_kind |= kind; }

void stats_fps_start(void) { s_fps_since = now_s(); }

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
    static EXT_RAM_BSS_ATTR refr_kind_t kc[REFR_KINDS], kt;
    static EXT_RAM_BSS_ATTR dev_wifi_t w;
    int failed_logged = 0, refused_logged = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATS_PERIOD_MS));
        diag_track();
        orbit_reap();
        double t = now_s();
        stats_sample_t app = stats_app_sample();
        taskENTER_CRITICAL(&s_refr_mux);
        int n = s_refr_count;
        int64_t total = s_refr_total_us, mx = s_refr_max_us;
        s_refr_count = 0;
        s_refr_total_us = s_refr_max_us = 0;
        int64_t win_us = refr_stats_take(&s_refr, esp_timer_get_time(), kc, &kt);
        taskEXIT_CRITICAL(&s_refr_mux);
        http_conn_get_stats(&ns);
        ESP_LOGI(TAG, "pose redraws %.1f/s | screen refreshes %d, avg %.1f ms, max %.1f ms | mood %s | page %d",
                 app.frames / (t - s_fps_since), n, n ? total / 1000.0 / n : 0.0, mx / 1000.0, MOOD_NAMES[app.mood],
                 app.page);
        ESP_LOGI(TAG, "glint chase: phase %d, working %.0f of %.0f s, laps %d, eyes %d fps", app.chase_phase,
                 app.chase_work_s, app.chase_wait_s, app.chase_laps, app.chase_fps);
        int moments, bake_ms, failed, refused;
        moment_view_stats(&moments, &bake_ms, &failed, &refused);
        ESP_LOGI(TAG, "weather moments: %d shown, last bake %d ms", moments, bake_ms);
        if (failed > failed_logged) ESP_LOGE(TAG, "weather moments: %d starts since boot without the PSRAM arena or the bake task", failed);
        failed_logged = failed;
        if (refused > refused_logged) ESP_LOGE(TAG, "weather moments: %d clouds since boot refused by wx_cloud_shape, drawn blank", refused);
        refused_logged = refused;
        int music, paint_ms;
        music_view_stats(&music, &paint_ms);
        ESP_LOGI(TAG, "music moments: %d shown, sprites painted at boot in %d ms", music, paint_ms);
        static const char *const KIND[4] = {"other", "pose", "glint", "pose+glint"};
        for (int i = 0; i < REFR_KINDS; i++)
            if (kc[i].n)
                ESP_LOGI(TAG, "refresh %s: %d, avg %.1f ms, max %.1f ms, avg %lld px, max %d areas", KIND[i], kc[i].n,
                         kc[i].us / 1000.0 / kc[i].n, kc[i].us_max / 1000.0, kc[i].px / kc[i].n, kc[i].areas_max);
        double win = win_us > 0 ? win_us / 1e6 : 1;
        ESP_LOGI(TAG, "invalidated: %lld areas (%.1f/s), %lld px (%.0f px/s) in %d refreshes over %.0f s", kt.areas,
                 kt.areas / win, kt.px, kt.px / win, kt.n, win);
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

void stats_task_start(void)
{
    if (xTaskCreatePinnedToCoreWithCaps(stats_task, "stats", 4352, NULL, 1, NULL, tskNO_AFFINITY, MALLOC_CAP_SPIRAM) != pdPASS)
        ESP_LOGE(TAG, "stats task not created");
}
