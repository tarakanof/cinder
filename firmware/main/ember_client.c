#include "ember_client.h"

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bsp_knob_15_md50et.h"
#include "cJSON.h"
#include "config_store.h"
#include "coredump_up.h"
#include "diag.h"
#include "dim.h"
#include "ember_host.h"
#include "ember_legacy.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "http_conn.h"
#include "panel_check.h"
#include "wifi_backoff.h"
#include "esp_log.h"
#include "esp_random.h"
#include <limits.h>
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_vfs_eventfd.h"
#include <unistd.h>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "device_api.h"
#include "esp_app_desc.h"
#include "freertos/task.h"
#include "improv.h"
#include "knob_view.h"
#include "nowplaying_client.h"
#include "nvs.h"
#include "ota_client.h"
#include "pomo_client.h"
#include "view_policy.h"
#include "view_wait.h"
#include "weather_client.h"

static const char *TAG = "ember";

#define RESP_MAX (16 * 1024)

static atomic_bool s_online;
static atomic_bool s_join;
static atomic_int s_join_failures;
static improv_join_t s_join_count;
static atomic_int s_link = EMBER_LINK_OFF;
static atomic_uint s_ip;
static atomic_int s_mood = -1;
static ember_host_info_t s_host = {.color = -1};
static portMUX_TYPE s_host_mux = portMUX_INITIALIZER_UNLOCKED;

static esp_timer_handle_t s_reconnect_timer;
static atomic_uint s_reconnects;

#define RSSI_LOW_DBM (-80)
static atomic_uint s_disconnects;
static atomic_int s_last_reason;
static atomic_int s_channel;
static atomic_int s_rssi_min = INT_MAX;
static atomic_uint s_beacon_timeouts;
static uint8_t s_bssid[6];
static bool s_has_bssid;
static portMUX_TYPE s_wifi_mux = portMUX_INITIALIZER_UNLOCKED;

static void note_rssi(int rssi)
{
    if (rssi >= 0 || rssi < -127) return;
    int cur = atomic_load(&s_rssi_min);
    while (rssi < cur && !atomic_compare_exchange_weak(&s_rssi_min, &cur, rssi)) {
    }
}

static void reconnect_arm(int reason)
{
    uint32_t ms = wifi_reconnect_ms(atomic_fetch_add(&s_reconnects, 1), reason, esp_random());
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)ms * 1000);
}

static int s_channel_hint;

static void set_scan(unsigned retry)
{
    wifi_config_t wc;
    if (esp_wifi_get_config(WIFI_IF_STA, &wc) != ESP_OK) return;
    int ch = atomic_load(&s_channel);
    if (ch <= 0) ch = s_channel_hint;
    bool all = wifi_scan_all_channels(ch, retry);
    wifi_scan_method_t m = all ? WIFI_ALL_CHANNEL_SCAN : WIFI_FAST_SCAN;
    if (wc.sta.scan_method != m || wc.sta.channel != (uint8_t)ch) {
        wc.sta.scan_method = m;
        wc.sta.channel = (uint8_t)ch;
        esp_wifi_set_config(WIFI_IF_STA, &wc);
    }
    memset(&wc, 0, sizeof wc);
}

static void reconnect_cb(void *arg)
{
    (void)arg;
    if (!atomic_load(&s_join) || atomic_load(&s_online)) return;
    unsigned n = atomic_load(&s_reconnects);
    set_scan(n ? n - 1 : 0);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi reconnect failed to start: %s", esp_err_to_name(err));
        reconnect_arm(0);
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (atomic_load(&s_join)) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *c = data;
        wifi_ap_record_t ap;
        int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
        note_rssi(rssi);
        atomic_store(&s_channel, c->channel);
        taskENTER_CRITICAL(&s_wifi_mux);
        memcpy(s_bssid, c->bssid, 6);
        s_has_bssid = true;
        taskEXIT_CRITICAL(&s_wifi_mux);
        ESP_LOGI(TAG, "Wi-Fi associated: channel %d, BSSID %02x:%02x:%02x:%02x:%02x:%02x, RSSI %d dBm", c->channel,
                 c->bssid[0], c->bssid[1], c->bssid[2], c->bssid[3], c->bssid[4], c->bssid[5], rssi);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_BEACON_TIMEOUT) {
        atomic_fetch_add(&s_beacon_timeouts, 1);
        ESP_LOGW(TAG, "Wi-Fi beacon timeout");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_BSS_RSSI_LOW) {
        const wifi_event_bss_rssi_low_t *l = data;
        note_rssi(l->rssi);
        esp_wifi_set_rssi_threshold(l->rssi - 3 > -127 ? l->rssi - 3 : -127);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&s_online, false);
        atomic_store(&s_ip, 0);
        http_conn_link_down();
        if (!atomic_load(&s_join)) return;
        const wifi_event_sta_disconnected_t *d = data;
        int reason = d ? d->reason : -1, rssi = d ? d->rssi : 0;
        atomic_fetch_add(&s_disconnects, 1);
        atomic_store(&s_last_reason, reason > 0 ? reason : 0);
        note_rssi(rssi);
        improv_join_note(&s_join_count, reason, rssi);
        atomic_store(&s_join_failures, improv_join_failures(&s_join_count));
        /* Never block in the default event loop: IP events queue behind it (docs/features.md, #21). */
        unsigned attempt = atomic_load(&s_reconnects);
        reconnect_arm(reason);
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d %s, RSSI %d), reconnect attempt %u", reason,
                 wifi_disc_name(wifi_disc_class(reason)), rssi, attempt + 1);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        atomic_store(&s_reconnects, 0);
        esp_timer_stop(s_reconnect_timer);
        esp_wifi_set_rssi_threshold(RSSI_LOW_DBM);
        ESP_LOGI(TAG, "Wi-Fi up, IP " IPSTR, IP2STR(&e->ip_info.ip));
        atomic_store(&s_ip, e->ip_info.ip.addr);
        improv_join_reset(&s_join_count);
        atomic_store(&s_join_failures, 0);
        atomic_store(&s_online, true);
    }
}

static atomic_bool s_wifi_started;

static void wifi_start(const cfg_t *cfg)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    const esp_timer_create_args_t targs = {.callback = reconnect_cb, .name = "wifi_rc"};
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_reconnect_timer));
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));

    bool join = cfg_provisioned(cfg);
    atomic_store(&s_join, join);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    /* HT20 on purpose at weak RSSI (docs/features.md, Wi-Fi link). */
    esp_err_t bw = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    if (bw != ESP_OK) ESP_LOGW(TAG, "Wi-Fi HT20: %s", esp_err_to_name(bw));
    if (join) {
        wifi_config_t wc = {0};
        /* Full-length SSID/PSK go in without a NUL, as the driver expects. */
        cfg_copy_raw(wc.sta.ssid, sizeof wc.sta.ssid, cfg->wifi_ssid);
        cfg_copy_raw(wc.sta.password, sizeof wc.sta.password, cfg->wifi_pass);
        wc.sta.threshold.authmode = cfg->wifi_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        s_channel_hint = config_store_wifi_channel();
        wc.sta.channel = (uint8_t)s_channel_hint;
        wc.sta.scan_method = wifi_scan_all_channels(s_channel_hint, 0) ? WIFI_ALL_CHANNEL_SCAN : WIFI_FAST_SCAN;
        wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        wc.sta.failure_retry_cnt = 2;
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
        memset(&wc, 0, sizeof wc);
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    atomic_store(&s_wifi_started, true);
    if (join) ESP_LOGI(TAG, "Wi-Fi connecting to \"%.32s\"", cfg->wifi_ssid);
    else ESP_LOGI(TAG, "Wi-Fi radio up for scans only (not provisioned)");
}

#define MAX_SESSIONS 32
#define OFFLINE_AFTER_FAILS 3

static int parse_state(const char *body, ember_host_info_t *host)
{
    static EXT_RAM_BSS_ATTR ember_host_session_t sess[MAX_SESSIONS];
    return ember_legacy_parse(body, sess, MAX_SESSIONS, host);
}

typedef struct {
    char epoch[24];
    char etag[VIEW_ETAG_MAX];
    char now[24];
    char wait[8];
} resp_hdrs_t;

static void on_header(const char *key, const char *value, void *ctx)
{
    resp_hdrs_t *h = ctx;
    if (strcasecmp(key, "X-Ember-Devices-Epoch") == 0) strlcpy(h->epoch, value, sizeof h->epoch);
    else if (strcasecmp(key, "ETag") == 0) strlcpy(h->etag, value, sizeof h->etag);
    else if (strcasecmp(key, "X-Ember-Now") == 0) strlcpy(h->now, value, sizeof h->now);
    else if (strcasecmp(key, VIEW_WAIT_HEADER) == 0) strlcpy(h->wait, value, sizeof h->wait);
}

static http_conn_t *s_conn;

static int http_req(const char *url, const char *body, char *buf, int cap, resp_hdrs_t *hdrs)
{
    return http_conn_req(s_conn, url, body, true, buf, cap, hdrs ? on_header : NULL, hdrs);
}

static int http_get(const char *url, char *buf, int cap) { return http_req(url, NULL, buf, cap, NULL); }

/* Poll-task state stays in PSRAM: more .bss breaks the display draw buffer (docs/features.md, internal RAM). */
typedef struct {
    knob_settings_t ks;
    dev_sched_t sched;
    resp_hdrs_t hdrs;
    http_conn_t conn;
    view_policy_t vp;
    dev_stats_t stats;
    view_etag_t etag;
    knob_view_t view;
    bool have_view;
    int wait_cap;
    int rearm_ms;
    bool waited;
    bool view_failing;
    uint32_t token_gen;
    bool has_pending;
    pomo_input_t pending;
    char base[CFG_URL_MAX + 1];
    char view_url[CFG_URL_MAX + 32];
    char wait_url[CFG_URL_MAX + 48];
    char state_url[CFG_URL_MAX + 16];
    char bright_url[CFG_URL_MAX + 32];
    char checkin_url[CFG_URL_MAX + 40];
    char body[1536];
    dev_diag_t diag;
    cd_state_t cd;
    char cd_url[CFG_URL_MAX + 48];
    ember_host_info_t ota_host;
} poll_ctx_t;
static poll_ctx_t *P;
#define s_ks (P->ks)
static uint32_t s_ks_gen;

static atomic_int s_dim_start = -1;
static dim_fade_t s_fade;
static bool s_fade_on;
static int s_bright_target = -1;

static void set_brightness_target(int level)
{
    if (level != s_bright_target) ESP_LOGI(TAG, "brightness -> %d", level);
    s_bright_target = level;
    if (s_fade_on) dim_fade_set_target(&s_fade, level);
}

static void brightness_poll(char *buf, const char *url)
{
    static int last_status;
    int status = http_get(url, buf, RESP_MAX);
    int level = -1;
    if (status == 200) {
        cJSON *root = cJSON_Parse(buf);
        const cJSON *l = root ? cJSON_GetObjectItemCaseSensitive(root, "level") : NULL;
        if (dim_level_valid(cJSON_IsNumber(l), cJSON_IsNumber(l) ? l->valuedouble : -1)) level = (int)l->valuedouble;
        cJSON_Delete(root);
    }
    if (status != -1 && status != last_status) {
        if (status != 200) ESP_LOGW(TAG, "brightness: HTTP %d, keeping the current level", status);
        last_status = status;
    }
    if (level >= 0) set_brightness_target(level < s_ks.floor ? s_ks.floor : level);
}

static void fade_start(void)
{
    if (!s_fade_on && atomic_load(&s_dim_start) >= 0) {
        dim_fade_init(&s_fade, atomic_load(&s_dim_start));
        dim_fade_set_floor(&s_fade, s_ks.floor);
        s_fade_on = true;
        if (s_bright_target >= 0) dim_fade_set_target(&s_fade, s_bright_target);
    }
}

static int fade_step(void)
{
    uint8_t v;
    if (!s_fade_on || !dim_fade_tick(&s_fade, &v)) return -1;
    panel_check_brightness(v);
    return DIM_STEP_MS;
}

#define EMBER_HANG_MS 90000
static _Atomic int64_t s_alive_us;
static esp_task_wdt_user_handle_t s_wdt;

static void alive(void) { atomic_store(&s_alive_us, esp_timer_get_time()); }

/* The task WDT is fed here, not by the ember task: one request may block ~20 s, past the 5 s timeout (docs/features.md, #22). */
static void wdt_feed_cb(void *arg)
{
    (void)arg;
    if (esp_timer_get_time() - atomic_load(&s_alive_us) < EMBER_HANG_MS * 1000LL) esp_task_wdt_reset_user(s_wdt);
}

static void watchdog_start(void)
{
    alive();
    const esp_timer_create_args_t ta = {.callback = wdt_feed_cb, .name = "ember_wdt"};
    esp_timer_handle_t t = NULL;
    esp_err_t err = esp_task_wdt_add_user("ember", &s_wdt);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ember watchdog not armed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_task_wdt_reset_user(s_wdt);
    if (err == ESP_OK) err = esp_timer_create(&ta, &t);
    if (err == ESP_OK) err = esp_timer_start_periodic(t, 1000000);
    if (err == ESP_OK) return;
    if (t) esp_timer_delete(t);
    esp_task_wdt_delete_user(s_wdt);
    ESP_LOGE(TAG, "ember watchdog not armed: %s", esp_err_to_name(err));
}

static void wait_and_fade(int ms, char *buf)
{
    fade_start();
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    for (;;) {
        alive();
        int32_t left = (int32_t)(end - xTaskGetTickCount());
        if (left <= 0) return;
        if (ota_client_mark_pending()) return;
        TickType_t wait = (TickType_t)left;
        if (wait > pdMS_TO_TICKS(1000)) wait = pdMS_TO_TICKS(1000);
        int next = fade_step();
        if (next >= 0 && wait > pdMS_TO_TICKS(next)) wait = pdMS_TO_TICKS(next);
        if (!buf) {
            vTaskDelay(wait);
            continue;
        }
        if (np_client_visible()) {
            if (atomic_load(&s_online) && np_client_cmds_pending()) np_client_run_controls(s_conn, P->base);
            if (wait > pdMS_TO_TICKS(50)) wait = pdMS_TO_TICKS(50);
        }
        pomo_input_t in;
        if (pomo_client_next_action(pdTICKS_TO_MS(wait), &in)) {
            pomo_client_run_action(in, s_conn, buf, RESP_MAX);
            pomo_client_log_state();
            return;
        }
    }
}

static void view_brightness(void)
{
    if (!s_ks.follow_ember || P->vp.legacy || !P->have_view || !P->view.has_brightness) return;
    int level = P->view.level;
    set_brightness_target(level < s_ks.floor ? s_ks.floor : level);
}

static void settings_changed(int64_t *next_bright_us)
{
    bool followed = s_ks.follow_ember;
    config_store_settings(&s_ks);
    diag_set_level((ks_diag_t)s_ks.diagnostics);
    if (s_fade_on) dim_fade_set_floor(&s_fade, s_ks.floor);
    if (!s_ks.follow_ember) set_brightness_target(s_ks.level);
    else if (!P->vp.legacy) view_brightness();
    else if (!followed) *next_bright_us = 0;
    else if (s_bright_target >= 0) set_brightness_target(s_bright_target < s_ks.floor ? s_ks.floor : s_bright_target);
}

static void poll_settings(int64_t *next_bright_us)
{
    uint32_t gen = config_store_settings_gen();
    if (gen == s_ks_gen) return;
    s_ks_gen = gen;
    settings_changed(next_bright_us);
}

static atomic_bool s_unauthorized;
static atomic_bool s_checked_in;
static bool s_rotated;
static bool s_deep_warned;
static uint32_t s_deep_version;
static _Atomic int64_t s_last_checkin_us = -1;
#define s_sched (P->sched)

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static atomic_int s_ovr_stats_s, s_ovr_live_s;

void ember_client_diag_override(int stats_s, int live_s)
{
    if (stats_s >= 0) atomic_store(&s_ovr_stats_s, stats_s);
    if (live_s >= 0) atomic_store(&s_ovr_live_s, live_s);
    ESP_LOGW(TAG, "diag override: stats %d s, live %d s (0 = settings)", atomic_load(&s_ovr_stats_s),
             atomic_load(&s_ovr_live_s));
}

static int stats_interval_s(void)
{
    int o = atomic_load(&s_ovr_stats_s);
    return o > 0 ? o : s_ks.stats_interval_s;
}

static int live_interval_s(void)
{
    int o = atomic_load(&s_ovr_live_s);
    return o > 0 ? o : s_ks.live_interval_s;
}

void ember_client_diag_intervals(int *stats_s, int *live_s, bool *override, int *diagnostics, int *checkin_s)
{
    int os = atomic_load(&s_ovr_stats_s), ol = atomic_load(&s_ovr_live_s);
    *stats_s = os > 0 ? os : P ? P->ks.stats_interval_s : KS_STATS_INTERVAL_S_DEFAULT;
    *live_s = ol > 0 ? ol : P ? P->ks.live_interval_s : KS_LIVE_INTERVAL_S_DEFAULT;
    *override = os > 0 || ol > 0;
    *diagnostics = P ? P->ks.diagnostics : KS_DIAG_OFF;
    *checkin_s = dev_checkin_period_ms(*stats_s * 1000, *diagnostics != KS_DIAG_OFF) / 1000;
}

static void apply_intervals(void)
{
    int period = dev_checkin_period_ms(stats_interval_s() * 1000, s_ks.diagnostics != KS_DIAG_OFF);
    dev_sched_intervals(&s_sched, period, live_interval_s() * 1000, now_ms());
}

static void live_mode(long long live_until, const char *now_hdr)
{
    long long srv_now = 0;
    view_parse_now(now_hdr, &srv_now);
    bool was = s_sched.live_until_ms > now_ms();
    dev_sched_live(&s_sched, dev_live_deadline_ms(live_until, srv_now, now_ms()), now_ms());
    bool is = s_sched.live_until_ms > now_ms();
    if (was != is) {
        if (is) ESP_LOGI(TAG, "diagnostics live mode on (checkin every %d s)", s_sched.live_ms / 1000);
        else ESP_LOGI(TAG, "diagnostics live mode off");
    }
}

static void coredump_reply(const dev_checkin_result_t *r)
{
    bool was_wanted = P->cd.wanted, was_acked = P->cd.acked;
    cd_reply(&P->cd, r->has_coredump_wanted, r->coredump_wanted, r->has_coredump_ack, r->coredump_ack);
    if (P->cd.wanted && !was_wanted) ESP_LOGI(TAG, "core dump %08" PRIx32 " wanted by Ember", P->cd.id);
    if (P->cd.acked && !was_acked) ESP_LOGI(TAG, "core dump %08" PRIx32 " stored by Ember", P->cd.id);
}

static void coredump_erase(void)
{
    alive();
    esp_err_t err = diag_crash_erase();
    alive();
    if (err != ESP_OK) {
        if (cd_erase_failed(&P->cd, now_ms()))
            ESP_LOGE(TAG, "core dump %08" PRIx32 " erase failed: %s; retrying with backoff", P->cd.id,
                     esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "core dump %08" PRIx32 " erased", P->cd.id);
    cd_erased(&P->cd);
    dev_sched_now(&s_sched, now_ms());
}

typedef struct {
    const uint8_t *map;
    uint8_t *chunk;
    uint32_t size;
    cd_check_t chk;
} cd_upload_t;

static int coredump_read(void *ctx, int off, const char **chunk)
{
    cd_upload_t *u = ctx;
    alive();
    if (off == 0) cd_check_init(&u->chk, P->cd.id, u->size);
    int n = (int)u->size - off < CD_CHUNK ? (int)u->size - off : CD_CHUNK;
    memcpy(u->chunk, u->map + off, (size_t)n);
    cd_check_feed(&u->chk, u->chunk, (size_t)n);
    if (off + n == (int)u->size && !cd_check_ok(&u->chk)) {
        ESP_LOGE(TAG, "core dump %08" PRIx32 ": CRC self-check failed (%08" PRIx32 "/%08" PRIx32 "), upload aborted",
                 P->cd.id, u->chk.crc, u->chk.tail);
        return -1;
    }
    *chunk = (const char *)u->chunk;
    return n;
}

static bool pomo_running(void)
{
    pomo_snapshot_t snap;
    if (!pomo_client_get(&snap)) return false;
    pomo_state_t now = pomo_clock_at(&snap.clock, pomo_client_now());
    return pomo_mode(&now) == POMO_MODE_RUNNING;
}

static bool pomo_active(void)
{
    pomo_snapshot_t snap;
    if (!pomo_client_get(&snap)) return false;
    pomo_state_t now = pomo_clock_at(&snap.clock, pomo_client_now());
    pomo_mode_t m = pomo_mode(&now);
    return m == POMO_MODE_RUNNING || m == POMO_MODE_PAUSED;
}

static void coredump_upload(char *buf)
{
    uint32_t id = 0, size = 0;
    cd_upload_t u = {0};
    esp_partition_mmap_handle_t mh = 0;
    int status = -1;
    if (diag_crash_id(&id, &size) && id == P->cd.id) {
        u.size = size;
        u.chunk = heap_caps_malloc(CD_CHUNK, MALLOC_CAP_SPIRAM);
        u.map = u.chunk ? diag_crash_map(&mh) : NULL;
    }
    if (u.map) {
        char ids[CD_ID_LEN + 1];
        cd_id_format(id, ids);
        snprintf(P->cd_url, sizeof P->cd_url, "%s/v1/devices/self/coredump?id=%s", P->base, ids);
        const http_req_opts_t o = {
            .idempotent = true,
            .auth = true,
            .stream_len = (int)size,
            .read = coredump_read,
            .read_ctx = &u,
            .content_type = "application/octet-stream",
        };
        int64_t t0 = now_ms();
        status = http_conn_req_opts(s_conn, P->cd_url, &o, buf, RESP_MAX);
        if (status == 204 || status == 200)
            ESP_LOGI(TAG, "core dump %s uploaded: %" PRIu32 " B in %lld ms", ids, size, (long long)(now_ms() - t0));
        else
            ESP_LOGW(TAG, "core dump %s upload -> %d", ids, status);
        esp_partition_munmap(mh);
    } else {
        ESP_LOGW(TAG, "core dump upload: dump not readable");
    }
    free(u.chunk);
    alive();
    bool ok = status == 204 || status == 200;
    cd_upload_done(&P->cd, ok, now_ms());
    if (ok) dev_sched_now(&s_sched, now_ms());
    else ESP_LOGW(TAG, "core dump upload retry in %d min", cd_backoff_ms(P->cd.attempts) / 60000);
}

static void ota_ctx(ota_ctx_t *c);

static void checkin(char *buf)
{
    char *url = P->checkin_url, *body = P->body;
    char ip[16];
    ember_client_ip(ip, sizeof ip);
    int rssi = 0;
    ember_client_rssi(&rssi);
    dev_wifi_t wifi;
    ember_client_wifi(&wifi);
    uint32_t applied = config_store_settings_version();
    dev_checkin_t c = {
        .link_mhz = bsp_knob_15_md50et_qspi_hz() / 1000000,
        .link_fallback = bsp_knob_15_md50et_qspi_fallback_active(),
        .fw = esp_app_get_description()->version,
        .ip = ip,
        .rssi = rssi,
        .heap_internal_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        .heap_internal_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        .uptime_s = esp_timer_get_time() / 1000000,
        .config_version = applied,
        .wifi = &wifi,
        .diag = &P->diag,
        .fw_build = ota_client_build(),
    };
    ota_report_t orep;
    ota_client_report(&orep);
    c.ota = &orep;
    diag_fill(&P->diag);
    dev_stats_t *st = &P->stats;
    bool live = s_sched.live_until_ms > now_ms();
    bool with_stats = diag_due(stats_interval_s() * 1000, s_sched.period_ms, live) && diag_sample(st);
    if (!dev_checkin_body(&c, with_stats ? st : NULL, body, sizeof P->body)) return;
    P->hdrs.now[0] = 0;
    double sent = pomo_client_now();
    int status = http_req(url, body, buf, RESP_MAX, &P->hdrs);
    double received = pomo_client_now();
    long long srv_now = 0;
    if (status > 0 && !P->vp.legacy && view_parse_now(P->hdrs.now, &srv_now))
        pomo_client_note_clock(srv_now, sent, received);
    static int last_status;
    if (status != -1 && status != last_status) {
        if (status == 200) ESP_LOGI(TAG, "checkin ok");
        else ESP_LOGW(TAG, "checkin -> HTTP %d%s", status, status == 401 ? ": not paired (token rejected)" : "");
        last_status = status;
    }
    ota_ctx_t octx;
    ota_ctx(&octx);
    if (status == 401) {
        atomic_store(&s_checked_in, false);
        atomic_store(&s_unauthorized, true);
        dev_sched_done(&s_sched, DEV_CHECKIN_UNAUTHORIZED, now_ms());
        ota_client_checkin_done(status, NULL, &octx);
        return;
    }
    if (status != 200) {
        dev_sched_done(&s_sched, DEV_CHECKIN_FAILED, now_ms());
        ota_client_checkin_done(status, NULL, &octx);
        return;
    }
    atomic_store(&s_unauthorized, false);
    atomic_store(&s_checked_in, true);
    if (wifi.has_rssi_min) {
        int cur = wifi.rssi_min;
        atomic_compare_exchange_strong(&s_rssi_min, &cur, INT_MAX);
        if (atomic_load(&s_online)) esp_wifi_set_rssi_threshold(RSSI_LOW_DBM);
    }
    if (with_stats) diag_commit();
    if (atomic_load(&s_last_checkin_us) < 0)
        ESP_LOGI(TAG, "first checkin; task stack headroom %u B", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    atomic_store(&s_last_checkin_us, esp_timer_get_time());
    dev_sched_done(&s_sched, DEV_CHECKIN_OK, now_ms());
    dev_checkin_result_t r;
    dev_checkin_parse(buf, &r);
    live_mode(r.diag_live_until, P->hdrs.now);
    coredump_reply(&r);
    ota_client_checkin_done(status, &r, &octx);
    if (r.has_new_token) {
        esp_err_t err = config_store_set_token(r.new_token);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "token rotated (stored)");
            s_rotated = true;
            dev_sched_now(&s_sched, now_ms());
        } else {
            ESP_LOGE(TAG, "storing the rotated token failed: %s", esp_err_to_name(err));
        }
    }
    if (r.ok && r.config_too_deep && r.config_version != applied && (!s_deep_warned || s_deep_version != r.config_version)) {
        ESP_LOGW(TAG, "knob settings v%" PRIu32 " ignored: config nested deeper than %d levels", r.config_version,
                 DEV_CONFIG_MAX_DEPTH);
        s_deep_warned = true;
        s_deep_version = r.config_version;
    }
    if (r.ok && r.config && r.config_version != applied) {
        esp_err_t err = config_store_apply_settings(r.config, r.config_version);
        if (err == ESP_OK) ESP_LOGI(TAG, "knob settings v%" PRIu32 " applied", r.config_version);
        else ESP_LOGE(TAG, "knob settings v%" PRIu32 " rejected: %s", r.config_version, esp_err_to_name(err));
    }
    dev_checkin_result_free(&r);
}

static void apply_mode(void)
{
    bool legacy = P->vp.legacy;
    if (legacy) {
        view_etag_clear(&P->etag);
        P->have_view = false;
    }
    dev_sched_epoch_reset(&s_sched);
    pomo_client_legacy(legacy);
    weather_client_legacy(legacy);
    if (legacy) ESP_LOGW(TAG, "reading Ember per endpoint (%s)", view_why_name(P->vp.why));
    else ESP_LOGI(TAG, "reading Ember's knob view");
}

static int apply_view(ember_host_info_t *host, long long srv_now, double sent, double received)
{
    const knob_view_t *v = &P->view;
    ember_host_lead_label(v->source, v->lead, v->hosts, host->text, sizeof host->text);
    host->color = ember_host_color(v->lead_color);
    host->tool = (uint8_t)ember_host_tool(v->tool);
    char key[48];
    knob_view_epoch_key(v, key, sizeof key);
    dev_sched_epoch(&s_sched, key, now_ms());
    live_mode(v->diag_live_until, P->hdrs.now);
    pomo_client_feed(v->has_pomo ? &v->pomo : NULL, v->pomo_counting, v->ends_at, srv_now, sent, received);
    weather_client_feed(v->has_weather ? &v->weather : NULL);
    np_client_feed(v->has_np ? &v->np : NULL);
    view_brightness();
    return knob_view_mood(v);
}

static bool longpoll_idle(void *ctx, int *next_slice_ms)
{
    (void)ctx;
    alive();
    int next = fade_step();
    *next_slice_ms = next >= 0 && next < 1000 ? next : 1000;
    if (ota_client_mark_pending()) return true;
    if (!atomic_load(&s_online) || config_store_token_gen() != P->token_gen) return true;
    if (config_store_settings_gen() != s_ks_gen) return true;
    if (np_client_pending()) return true;
    if (!P->has_pending && pomo_client_next_action(0, &P->pending)) P->has_pending = true;
    return P->has_pending;
}

static atomic_int s_wake_fd = -1;

void ember_client_wake(void)
{
    int fd = atomic_load(&s_wake_fd);
    if (fd >= 0) {
        uint64_t one = 1;
        (void)write(fd, &one, sizeof one);
    }
}

static bool s_plain_poll;

static int view_poll(char *buf, int *mood, ember_host_info_t *host)
{
    resp_hdrs_t *h = &P->hdrs;
    h->etag[0] = h->now[0] = h->wait[0] = 0;
    pomo_input_t in;
    if (pomo_client_next_action(0, &in)) {
        pomo_client_run_action(in, s_conn, buf, RESP_MAX);
        pomo_client_log_state();
    }
    const char *etag = P->have_view ? view_etag_get(&P->etag) : NULL;
    int wait = s_plain_poll ? 0 : view_wait_s(P->wait_cap, etag != NULL, s_sched.next_ms - now_ms(), P->view_failing);
    const char *url = P->view_url;
    if (wait > 0) {
        snprintf(P->wait_url, sizeof P->wait_url, "%s?wait=%d", P->view_url, wait);
        url = P->wait_url;
        fade_start();
    }
    const http_req_opts_t o = {
        .idempotent = true,
        .auth = true,
        .if_none_match = etag,
        .on_header = on_header,
        .hdr_ctx = h,
        .read_budget_ms = wait > 0 ? view_wait_budget_ms(wait) : 0,
        .wake_fd = atomic_load(&s_wake_fd),
        .idle = longpoll_idle,
    };
    double sent = pomo_client_now();
    int64_t t0 = now_ms();
    int status = http_conn_req_opts(s_conn, url, &o, buf, RESP_MAX);
    double received = pomo_client_now();
    int64_t elapsed = now_ms() - t0;
    P->waited = wait > 0;
    P->rearm_ms = view_rearm_ms(status, wait, elapsed, s_ks.poll_ms);
    *mood = -1;
    if (status == HTTP_CONN_ABORTED) {
        P->rearm_ms = 0;
        return status;
    }
    P->view_failing = !(status == 200 || status == 304);
    if (!P->view_failing) ota_client_note_view_ok();
    if (status > 0) {
        int cap = view_wait_cap(h->wait);
        if (cap != P->wait_cap) {
            if (cap) ESP_LOGI(TAG, "view: the server long-polls (wait up to %d s)", cap);
            else ESP_LOGI(TAG, "view: the server does not long-poll; polling every poll_ms");
        }
        P->wait_cap = cap;
    }
    long long srv_now = 0;
    if (view_wait_clock_sample(wait, elapsed)) view_parse_now(h->now, &srv_now);
    switch (view_answer(status, P->have_view)) {
    case VIEW_ANS_NEW:
        if (!knob_view_parse(buf, &P->view)) {
            static bool logged;
            if (!logged) ESP_LOGW(TAG, "view: answer is not a knob view");
            logged = true;
            P->have_view = false;
            view_etag_clear(&P->etag);
            return 500;
        }
        P->have_view = true;
        view_etag_set(&P->etag, h->etag);
        *mood = apply_view(host, srv_now, sent, received);
        return status;
    case VIEW_ANS_SAME:
        *mood = apply_view(host, srv_now, sent, received);
        return status;
    case VIEW_ANS_REFETCH:
        view_etag_clear(&P->etag);
        return status;
    case VIEW_ANS_FAILED:
    default:
        if (status == -1) pomo_client_feed_failed();
        return status;
    }
}

static char *s_buf;

static bool ota_fresh_view(void)
{
    if (P->vp.legacy) return true;
    int mood;
    s_plain_poll = true;
    int status = view_poll(s_buf, &mood, &P->ota_host);
    s_plain_poll = false;
    alive();
    return status == 200 || status == 304;
}

static void ota_checkin(void) { checkin(s_buf); }

static void ota_checkin_soon(void) { dev_sched_now(&s_sched, now_ms()); }

static void ota_ctx(ota_ctx_t *c)
{
    *c = (ota_ctx_t){
        .conn = s_conn,
        .base = P->base,
        .online = atomic_load(&s_online),
        .paired = config_store_has_device(),
        .alive = alive,
        .pomo_active = pomo_active,
        .fresh_view = ota_fresh_view,
        .checkin = ota_checkin,
        .checkin_soon = ota_checkin_soon,
    };
}

static void ota_service(void)
{
    ota_ctx_t c;
    ota_ctx(&c);
    ota_client_service(&c);
}

static void poll_task(void *arg)
{
    (void)arg;
    char *buf = heap_caps_malloc(RESP_MAX, MALLOC_CAP_SPIRAM);
    P = heap_caps_calloc(1, sizeof *P, MALLOC_CAP_SPIRAM);
    assert(buf && P);
    s_buf = buf;
    http_conn_init(&P->conn, "ember");
    P->conn.on_request = alive;
    s_conn = &P->conn;
    ember_host_info_t host = {.color = -1};
    const char *url = P->state_url, *bright_url = P->bright_url;
    config_store_ember_url(P->base, sizeof P->base);
    snprintf(P->view_url, sizeof P->view_url, "%s/v1/devices/self/view", P->base);
    snprintf(P->state_url, sizeof P->state_url, "%s/state", P->base);
    snprintf(P->bright_url, sizeof P->bright_url, "%s/v1/display/brightness", P->base);
    snprintf(P->checkin_url, sizeof P->checkin_url, "%s/v1/devices/self/checkin", P->base);
    int last = -2, last_status = 200, mood = -1;
    int64_t next_bright_us = 0;
    P->token_gen = config_store_token_gen();
    dev_sched_init(&s_sched, now_ms());
    view_policy_init(&P->vp, config_store_has_device());
    if (P->vp.legacy) apply_mode();
    {
        uint32_t id = 0, size = 0;
        bool has = diag_crash_id(&id, &size);
        cd_init(&P->cd, has, id);
    }
    int saved_channel = config_store_wifi_channel();
    for (;;) {
        alive();
        int ch = atomic_load(&s_channel);
        if (atomic_load(&s_online) && ch > 0 && ch != saved_channel) {
            config_store_set_wifi_channel(ch);
            saved_channel = ch;
        }
        poll_settings(&next_bright_us);
        if (config_store_token_gen() != P->token_gen) {
            P->token_gen = config_store_token_gen();
            if (!s_rotated) {
                atomic_store(&s_unauthorized, false);
                atomic_store(&s_checked_in, false);
            }
            s_rotated = false;
            dev_sched_now(&s_sched, now_ms());
            bool was_legacy = P->vp.legacy;
            view_policy_device_changed(&P->vp, config_store_has_device(), now_ms());
            view_etag_clear(&P->etag);
            P->have_view = false;
            if (P->vp.legacy != was_legacy) apply_mode();
        }
        bool online = atomic_load(&s_online);
        bool fresh = false;
        P->rearm_ms = -1;
        if (online) {
            int status = -1;
            bool viewed = false;
            bool aborted = false;
            if (view_policy_try_view(&P->vp, now_ms())) {
                int vmood;
                status = view_poll(buf, &vmood, &host);
                alive();
                aborted = status == HTTP_CONN_ABORTED;
                if (aborted) {
                    viewed = true;
                    if (P->has_pending) {
                        P->has_pending = false;
                        pomo_client_run_action(P->pending, s_conn, buf, RESP_MAX);
                        pomo_client_log_state();
                    }
                } else if (view_policy_result(&P->vp, status, now_ms())) apply_mode();
                if (!aborted && !P->vp.legacy) {
                    viewed = true;
                    mood = vmood >= 0 ? vmood : mood;
                    fresh = vmood >= 0;
                }
            }
            if (!viewed) {
                P->hdrs.epoch[0] = 0;
                status = http_req(url, NULL, buf, RESP_MAX, &P->hdrs);
                if (status == 200) {
                    ota_client_note_view_ok();
                    mood = parse_state(buf, &host);
                    fresh = mood >= 0;
                    dev_sched_epoch(&s_sched, P->hdrs.epoch, now_ms());
                }
            }
            bool ok = status == 200 || (viewed && status == 304);
            bool down = (status != -1 && status != 429) || fs_fails(&P->conn.streak) >= OFFLINE_AFTER_FAILS;
            if (aborted) {
            } else if (ok) {
                atomic_store(&s_link, EMBER_LINK_OK);
            } else if (down) {
                atomic_store(&s_link, EMBER_LINK_UNREACHABLE);
            }
            if (!aborted && !ok && down) mood = -1;
            int shown = ok ? 200 : status;
            if (!aborted && shown != -1 && shown != last_status) {
                if (shown != 200) ESP_LOGW(TAG, "GET %s -> HTTP %d", viewed ? P->view_url : url, status);
                last_status = shown;
            }
        } else {
            atomic_store(&s_link, EMBER_LINK_CONNECTING);
            mood = -1;
            if (!P->vp.legacy) pomo_client_feed_failed();
        }
        if (online && !P->vp.legacy && P->have_view) np_client_service(s_conn, P->base);
        if (fresh) {
            taskENTER_CRITICAL(&s_host_mux);
            s_host = host;
            taskEXIT_CRITICAL(&s_host_mux);
        }
        atomic_store(&s_mood, mood);
        if (mood != last) {
            ESP_LOGI(TAG, "Ember mood -> %d%s", mood, mood < 0 ? " (offline / no data)" : "");
            last = mood;
        }
        if (!P->vp.legacy) pomo_client_log_state();
        apply_intervals();
        if (online && config_store_has_device() && dev_sched_due(&s_sched, now_ms()) && !ota_client_checkin_blocked()) {
            checkin(buf);
            alive();
            poll_settings(&next_bright_us);
        }
        if (P->cd.wanted || P->cd.acked) {
            bool pomo = pomo_running();
            if (cd_erase_due(&P->cd, now_ms(), pomo)) coredump_erase();
            else if (online && config_store_has_device() && cd_upload_due(&P->cd, now_ms(), pomo)) coredump_upload(buf);
        }
        ota_service();
        if (P->vp.legacy && online && s_ks.follow_ember && esp_timer_get_time() >= next_bright_us) {
            brightness_poll(buf, bright_url);
            next_bright_us = esp_timer_get_time() + (int64_t)DIM_POLL_MS * 1000;
        }
        pomo_client_legacy(P->vp.legacy);
        weather_client_legacy(P->vp.legacy);
        if (P->vp.legacy) {
            wait_and_fade(s_ks.poll_ms, NULL);
        } else if (P->rearm_ms >= 0 && P->waited) {
            if (P->rearm_ms > 0) wait_and_fade(P->rearm_ms, buf);
        } else {
            wait_and_fade(pomo_client_view_poll_ms(s_ks.poll_ms), buf);
        }
    }
}

void ember_client_start(void)
{
    cfg_t *cfg = heap_caps_calloc(1, sizeof *cfg, MALLOC_CAP_SPIRAM);
    assert(cfg);
    config_store_get(cfg);
    bool have_url = cfg->ember_url[0] != 0;
    wifi_start(cfg);
    if (!cfg_provisioned(cfg)) ESP_LOGW(TAG, "no Wi-Fi configured; Ember disabled");
    bool started = cfg_provisioned(cfg);
    memset(cfg, 0, sizeof *cfg);
    free(cfg);
    if (!started) return;
    if (!have_url) {
        ESP_LOGW(TAG, "no Ember URL configured; Wi-Fi only");
        return;
    }
    atomic_store(&s_link, EMBER_LINK_CONNECTING);
    const esp_vfs_eventfd_config_t efd = ESP_VFS_EVENTD_CONFIG_DEFAULT();
    if (esp_vfs_eventfd_register(&efd) == ESP_OK) {
        int fd = eventfd(0, 0);
        if (fd >= 0) atomic_store(&s_wake_fd, fd);
    }
    if (atomic_load(&s_wake_fd) < 0) ESP_LOGW(TAG, "no wake fd: a push during a long-poll waits up to 1 s");
    if (xTaskCreatePinnedToCore(poll_task, "ember", 6912, NULL, 3, NULL, 1) == pdPASS) watchdog_start();
}

void ember_client_forget_wifi(void)
{
    if (atomic_load(&s_wifi_started)) {
        esp_wifi_restore();
        return;
    }
    nvs_handle_t h;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool ember_client_online(void) { return atomic_load(&s_online); }

ember_link_t ember_client_link(void)
{
    _Static_assert((int)EMBER_LINK_UNAUTHORIZED == (int)DEV_LINK_UNAUTHORIZED, "same order");
    return (ember_link_t)dev_link_combine((dev_link_t)atomic_load(&s_link), config_store_has_device(),
                                          atomic_load(&s_checked_in), atomic_load(&s_unauthorized));
}

void ember_client_report_unauthorized(void)
{
    if (!config_store_has_device()) return;
    atomic_store(&s_unauthorized, true);
}

long ember_client_last_checkin_s(void)
{
    int64_t t = atomic_load(&s_last_checkin_us);
    return t < 0 ? -1 : (long)((esp_timer_get_time() - t) / 1000000);
}

const char *ember_link_name(ember_link_t l)
{
    static const char *const N[] = {"off", "connecting", "ok", "unreachable", "unauthorized"};
    return (unsigned)l < sizeof N / sizeof N[0] ? N[l] : "off";
}

void ember_client_ip(char *out, size_t cap)
{
    uint32_t a = atomic_load(&s_ip);
    if (!a) {
        if (cap) out[0] = 0;
        return;
    }
    snprintf(out, cap, "%u.%u.%u.%u", (unsigned)(a & 0xff), (unsigned)((a >> 8) & 0xff), (unsigned)((a >> 16) & 0xff),
             (unsigned)(a >> 24));
}

bool ember_client_rssi(int *out)
{
    wifi_ap_record_t ap;
    if (!atomic_load(&s_online) || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    *out = ap.rssi;
    note_rssi(ap.rssi);
    return true;
}

void ember_client_wifi(dev_wifi_t *out)
{
    memset(out, 0, sizeof *out);
    out->channel = atomic_load(&s_channel);
    taskENTER_CRITICAL(&s_wifi_mux);
    out->has_bssid = s_has_bssid;
    memcpy(out->bssid, s_bssid, 6);
    taskEXIT_CRITICAL(&s_wifi_mux);
    out->disconnects = atomic_load(&s_disconnects);
    out->last_reason = atomic_load(&s_last_reason);
    int m = atomic_load(&s_rssi_min);
    out->has_rssi_min = m != INT_MAX;
    out->rssi_min = out->has_rssi_min ? m : 0;
}

unsigned ember_client_beacon_timeouts(void) { return atomic_load(&s_beacon_timeouts); }

bool ember_client_wifi_started(void) { return atomic_load(&s_wifi_started); }

int ember_client_join_failures(void) { return atomic_load(&s_join_failures); }

bool ember_client_mood(bot_mood_t *out)
{
    int m = atomic_load(&s_mood);
    if (m < 0) return false;
    *out = (bot_mood_t)m;
    return true;
}

void ember_client_host(ember_host_info_t *out)
{
    taskENTER_CRITICAL(&s_host_mux);
    *out = s_host;
    taskEXIT_CRITICAL(&s_host_mux);
}

void ember_client_dim_enable(uint8_t current_level) { atomic_store(&s_dim_start, current_level); }
