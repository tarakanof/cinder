#include "provision_usb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cinder_line.h"
#include "config_store.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "ember_client.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "http_conn.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "improv.h"
#include "ota_client.h"
#include "chase.h"
#include "screen_snap.h"

static const char *TAG = "prov";

#define RX_RING 512
#define TX_RING 512
#define OUT_CHUNK 256
#define OUT_TIMEOUT_MS 50
#define QUIET_US (30 * 1000000LL)
#define JOIN_FAILS_REPORTED 3
#define SCAN_MAX 20
#define CHIP "ESP32-S3"

#define IMPROV_PENDING_MAGIC 0x494D5056u
static RTC_NOINIT_ATTR uint32_t s_improv_pending;

static prov_rx_t s_rx;
static char *s_out;
static cfg_t *s_cfg;
static cl_req_t *s_req;
static bool s_pending;
static bool s_join_error_sent;
static bool s_quiet;
static int64_t s_quiet_until;

/* Whole frame under the stdout lock; dropped when no host or a chunk times out (SOF-based connect check, docs/features.md, USB provisioning). */
static bool out_write_wait(const void *data, size_t n, uint32_t chunk_ms)
{
    if (!n) return true;
    if (!usb_serial_jtag_is_connected()) return false;
    flockfile(stdout);
    fflush(stdout);
    const uint8_t *p = data;
    while (n) {
        size_t c = n > OUT_CHUNK ? OUT_CHUNK : n;
        if (usb_serial_jtag_write_bytes(p, c, pdMS_TO_TICKS(chunk_ms)) != (int)c) break;
        p += c;
        n -= c;
    }
    funlockfile(stdout);
    return n == 0;
}

static void out_write(const void *data, size_t n) { (void)out_write_wait(data, n, OUT_TIMEOUT_MS); }

static void out_improv_state(void)
{
    uint8_t st = !config_store_provisioned()               ? IMPROV_STATE_READY
                 : s_pending && !ember_client_online()     ? IMPROV_STATE_PROVISIONING
                                                           : IMPROV_STATE_PROVISIONED;
    out_write(s_out, improv_state(st, (uint8_t *)s_out, CL_LINE_MAX));
}

static void out_improv_error(uint8_t e) { out_write(s_out, improv_error(e, (uint8_t *)s_out, CL_LINE_MAX)); }

static void out_result(uint8_t cmd, const char *const *strs, size_t n)
{
    size_t len = improv_result(cmd, strs, n, (uint8_t *)s_out, CL_LINE_MAX);
    if (len) out_write(s_out, len);
    else out_improv_error(IMPROV_ERR_UNKNOWN);
}

static void redirect_url(char *out, size_t cap)
{
    char ip[16];
    ember_client_ip(ip, sizeof ip);
    if (ip[0]) snprintf(out, cap, "http://%s/", ip);
    else if (cap) out[0] = 0;
}

static void restart_soon(void)
{
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(200));
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static void enter_session(void)
{
    s_quiet_until = esp_timer_get_time() + QUIET_US;
    if (s_quiet) return;
    s_quiet = true;
    ESP_LOGW(TAG, "host session: logs at WARN until 30 s after the last message");
    esp_log_level_set("*", ESP_LOG_WARN);
    http_conn_quiet_idf_logs();
}

static void session_tick(void)
{
    if (s_quiet && esp_timer_get_time() >= s_quiet_until) {
        s_quiet = false;
        esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL);
        http_conn_quiet_idf_logs();
        ESP_LOGI(TAG, "host session over: logs back on (listener stack headroom %u B)",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

static void improv_wifi(const improv_rpc_t *rpc)
{
    static char ssid[CFG_SSID_MAX + 1], pass[CFG_PASS_MAX + 1];
    bool ok = improv_rpc_string(rpc, 0, ssid, sizeof ssid) && ssid[0] &&
              improv_rpc_string(rpc, 1, pass, sizeof pass);
    config_store_get(s_cfg);
    ok = ok && cfg_set(s_cfg, "wifi_ssid", ssid) == CFG_OK && cfg_set(s_cfg, "wifi_pass", pass) == CFG_OK;
    memset(pass, 0, sizeof pass);
    if (!ok) {
        memset(s_cfg, 0, sizeof *s_cfg);
        ESP_LOGW(TAG, "Improv Wi-Fi: invalid SSID or password");
        out_improv_error(IMPROV_ERR_INVALID_RPC);
        out_write(s_out, cl_event_wifi("invalid", s_out, CL_LINE_MAX));
        return;
    }
    esp_err_t err = config_store_save(s_cfg);
    memset(s_cfg, 0, sizeof *s_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Improv Wi-Fi: saving failed: %s", esp_err_to_name(err));
        out_improv_error(IMPROV_ERR_UNKNOWN);
        return;
    }
    out_write(s_out, improv_state(IMPROV_STATE_PROVISIONING, (uint8_t *)s_out, CL_LINE_MAX));
    ESP_LOGW(TAG, "Improv Wi-Fi: saved \"%s\"; rebooting to join (Wi-Fi starts before the display)", ssid);
    s_improv_pending = IMPROV_PENDING_MAGIC;
    restart_soon();
}

static void improv_scan(void)
{
    if (!ember_client_wifi_started()) {
        out_improv_error(IMPROV_ERR_UNKNOWN);
        return;
    }
    /* A provisioned knob that is still joining refuses scans for a moment (ESP_ERR_WIFI_STATE). */
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 6 && err != ESP_OK; i++) {
        err = esp_wifi_scan_start(NULL, true);
        if (err != ESP_OK) vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        out_improv_error(IMPROV_ERR_UNKNOWN);
        return;
    }
    uint16_t n = SCAN_MAX;
    wifi_ap_record_t *ap = heap_caps_calloc(SCAN_MAX, sizeof *ap, MALLOC_CAP_SPIRAM);
    if (!ap || esp_wifi_scan_get_ap_records(&n, ap) != ESP_OK) n = 0;
    if (!ap) esp_wifi_clear_ap_list();
    for (uint16_t i = 0; i < n; i++) {
        if (!ap[i].ssid[0]) continue;
        char ssid[33], rssi[8];
        memcpy(ssid, ap[i].ssid, 32);
        ssid[32] = 0;
        snprintf(rssi, sizeof rssi, "%d", ap[i].rssi);
        const char *s[] = {ssid, rssi, ap[i].authmode == WIFI_AUTH_OPEN ? "NO" : "YES"};
        out_result(IMPROV_CMD_SCAN, s, 3);
    }
    out_result(IMPROV_CMD_SCAN, NULL, 0);
    free(ap);
}

static void handle_improv(const uint8_t *frame, size_t len)
{
    improv_pkt_t pkt;
    improv_rpc_t rpc;
    improv_err_t e = improv_parse(frame, len, &pkt);
    if (e != IMPROV_OK || pkt.type != IMPROV_TYPE_RPC || improv_parse_rpc(&pkt, &rpc) != IMPROV_OK) {
        if (e == IMPROV_OK && pkt.type != IMPROV_TYPE_RPC) return;
        out_improv_error(IMPROV_ERR_INVALID_RPC);
        return;
    }
    enter_session();
    switch (rpc.cmd) {
    case IMPROV_CMD_WIFI:
        improv_wifi(&rpc);
        break;
    case IMPROV_CMD_STATE: {
        out_improv_state();
        char url[40];
        redirect_url(url, sizeof url);
        if (config_store_provisioned() && url[0]) {
            const char *s[] = {url};
            out_result(IMPROV_CMD_STATE, s, 1);
        }
        break;
    }
    case IMPROV_CMD_INFO: {
        char name[CFG_NAME_MAX + 1], id[7];
        config_store_get(s_cfg);
        strlcpy(name, s_cfg->name, sizeof name);
        memset(s_cfg, 0, sizeof *s_cfg);
        if (!name[0]) {
            config_store_short_id(id);
            snprintf(name, sizeof name, "Knob %s", id);
        }
        const char *s[] = {"cinder", esp_app_get_description()->version, CHIP, name};
        out_result(IMPROV_CMD_INFO, s, 4);
        break;
    }
    case IMPROV_CMD_SCAN:
        improv_scan();
        break;
    case IMPROV_CMD_NAME: {
        char name[CFG_NAME_MAX + 1];
        config_store_get(s_cfg);
        bool ok = improv_rpc_string(&rpc, 0, name, sizeof name) && cfg_set(s_cfg, "name", name) == CFG_OK &&
                  config_store_save(s_cfg) == ESP_OK;
        memset(s_cfg, 0, sizeof *s_cfg);
        if (ok) out_result(IMPROV_CMD_NAME, NULL, 0);
        else out_improv_error(IMPROV_ERR_INVALID_RPC);
        break;
    }
    default:
        out_improv_error(IMPROV_ERR_UNKNOWN_CMD);
    }
}

static void reply_err(const cl_req_t *r, cl_err_t e)
{
    out_write(s_out, cl_reply_error(r->has_id, r->id, e, s_out, CL_LINE_MAX));
}

static void reply_ok(const cl_req_t *r) { out_write(s_out, cl_reply_ok(r->has_id, r->id, s_out, CL_LINE_MAX)); }

static void op_info(const cl_req_t *r)
{
    char hw[13], dev[CFG_DEV_ID_MAX + 1];
    config_store_hw_id(hw);
    config_store_get(s_cfg);
    strlcpy(dev, s_cfg->dev_id, sizeof dev);
    cl_info_t in = {
        .fw = esp_app_get_description()->version,
        .hw_id = hw,
        .device_id = dev,
        .wifi_configured = cfg_provisioned(s_cfg),
        .ember_configured = s_cfg->ember_url[0] != 0,
    };
    memset(s_cfg, 0, sizeof *s_cfg);
    out_write(s_out, cl_reply_info(r->id, &in, s_out, CL_LINE_MAX));
}

static void op_set_ember(const cl_req_t *r)
{
    config_store_get(s_cfg);
    bool url_changed = strcmp(s_cfg->ember_url, r->url) != 0;
    bool provisioned = cfg_provisioned(s_cfg);
    bool ok = cfg_set(s_cfg, "ember_url", r->url) == CFG_OK && cfg_set(s_cfg, "dev_id", r->device_id) == CFG_OK &&
              cfg_set(s_cfg, "dev_tok", r->token) == CFG_OK && (!r->name[0] || cfg_set(s_cfg, "name", r->name) == CFG_OK);
    esp_err_t err = ok ? config_store_save(s_cfg) : ESP_OK;
    memset(s_cfg, 0, sizeof *s_cfg);
    if (!ok) {
        reply_err(r, CL_E_BAD_VALUE);
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_ember: saving failed: %s", esp_err_to_name(err));
        reply_err(r, CL_E_BAD_VALUE);
    } else {
        ESP_LOGW(TAG, "set_ember: saved url %s, device %s", r->url, r->device_id);
        reply_ok(r);
        if (url_changed && provisioned) {
            ESP_LOGW(TAG, "set_ember: Ember URL changed; rebooting to apply it");
            restart_soon();
        }
    }
}

static void op_status(const cl_req_t *r)
{
    char ip[16], ssid[CFG_SSID_MAX + 1];
    config_store_get(s_cfg);
    bool prov = cfg_provisioned(s_cfg);
    strlcpy(ssid, s_cfg->wifi_ssid, sizeof ssid);
    memset(s_cfg, 0, sizeof *s_cfg);
    ember_client_ip(ip, sizeof ip);
    int rssi = 0;
    bool online = ember_client_online();
    cl_status_t st = {
        .wifi_state = !prov ? "off" : online ? "connected" : "connecting",
        .ssid = prov ? ssid : NULL,
        .ip = ip[0] ? ip : NULL,
        .has_rssi = online && ember_client_rssi(&rssi),
        .rssi = rssi,
        .ember_state = ember_link_name(ember_client_link()),
        .last_checkin_s = ember_client_last_checkin_s(),
        .config_version = config_store_settings_version() ? (long)config_store_settings_version() : -1,
        .internal_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        .internal_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        .psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
    };
    int diag, checkin_s;
    ember_client_diag_intervals(&st.diag_stats_s, &st.diag_live_s, &st.diag_override, &diag, &checkin_s);
    out_write(s_out, cl_reply_status(r->id, &st, s_out, CL_LINE_MAX));
}

static void op_diag_override(const cl_req_t *r)
{
    ember_client_diag_override(r->stats_s, r->live_s);
    http_conn_stats_t ns;
    http_conn_get_stats(&ns);
    cl_diag_t d = {.requests = ns.requests, .rx_bytes = ns.rx_bytes, .tx_bytes = ns.tx_bytes,
                   .uptime_ms = esp_timer_get_time() / 1000};
    int diag;
    ember_client_diag_intervals(&d.stats_s, &d.live_s, &d.override, &diag, &d.checkin_s);
    static const char *const LEVELS[] = {"off", "basic", "full"};
    d.diagnostics = diag >= 0 && diag <= 2 ? LEVELS[diag] : "off";
    out_write(s_out, cl_reply_diag(r->id, &d, s_out, CL_LINE_MAX));
}

#define SNAP_RAW 3072
static void op_snapshot(const cl_req_t *r)
{
    const uint8_t *px;
    int w, h, stride;
    char *line = heap_caps_malloc(SNAP_RAW / 3 * 4 + 8, MALLOC_CAP_SPIRAM);
    snap_result_t sr = line ? screen_snap_take(3000, &px, &w, &h, &stride) : SNAP_FAILED;
    if (sr != SNAP_OK) {
        free(line);
        reply_err(r, sr == SNAP_BUSY ? CL_E_BUSY : CL_E_FAILED);
        return;
    }
    size_t total = (size_t)stride * h;
    snprintf(s_out, CL_LINE_MAX, CL_PREFIX "{\"bytes\":%u,\"cf\":\"rgb565le\",\"ev\":\"snapshot\",\"h\":%d,\"id\":%ld,"
             "\"stride\":%d,\"w\":%d}\n", (unsigned)total, h, r->id, stride, w);
    out_write(s_out, strlen(s_out));
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t off = 0; off < total; off += SNAP_RAW) {
        size_t n = total - off < SNAP_RAW ? total - off : SNAP_RAW, o = 0;
        const uint8_t *p = px + off;
        memcpy(line, "SNAP ", 5);
        o = 5;
        for (size_t i = 0; i < n; i += 3) {
            uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
            line[o++] = B64[v >> 18 & 63];
            line[o++] = B64[v >> 12 & 63];
            line[o++] = i + 1 < n ? B64[v >> 6 & 63] : '=';
            line[o++] = i + 2 < n ? B64[v & 63] : '=';
        }
        line[o++] = '\n';
        if (!out_write_wait(line, o, 500)) break;
    }
    screen_snap_release();
    free(line);
    snprintf(s_out, CL_LINE_MAX, CL_PREFIX "{\"ev\":\"snapshot_end\",\"id\":%ld}\n", r->id);
    out_write(s_out, strlen(s_out));
}

static void op_chase(const cl_req_t *r)
{
    int st = app_chase_request(r->chase_style, r->chase_fps, r->chase_laps);
    for (int i = 0; st == 2 && i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        st = app_chase_request(r->chase_style, r->chase_fps, r->chase_laps);
    }
    if (st == 0) reply_ok(r);
    else reply_err(r, st == 1 ? CL_E_BUSY : st == 2 ? CL_E_NOT_WORKING : CL_E_FAILED);
}

static void op_input(const cl_req_t *r)
{
#if CONFIG_CINDER_DEV_INPUT
    ESP_LOGW(TAG, "input op: %s x %d (USB dev tool)", CL_INPUT_NAMES[r->input], r->input_n);
    app_input_inject((int)r->input, r->input_n);
    reply_ok(r);
#else
    reply_err(r, CL_E_UNKNOWN_OP);
#endif
}

static void op_ota_fault(const cl_req_t *r)
{
    if (ota_client_fault(r->fault)) reply_ok(r);
    else reply_err(r, CL_E_UNKNOWN_OP);
}

static void op_ota_valid(const cl_req_t *r)
{
    static const cl_err_t E[] = {CL_OK, CL_E_NOT_PENDING, CL_E_NO_CHECKIN, CL_E_NOT_READY, CL_E_BUSY, CL_E_FAILED};
    _Static_assert(sizeof E / sizeof E[0] == OTA_MARK_FAILED + 1, "one reply per ota_mark_t");
    ota_mark_t m = ota_client_mark_valid();
    if (m == OTA_MARK_OK) reply_ok(r);
    else reply_err(r, (unsigned)m < sizeof E / sizeof E[0] ? E[m] : CL_E_FAILED);
}

static void op_reset(const cl_req_t *r)
{
    static const char *const NAMES[] = {"factory", "ember", "wifi"};
    esp_err_t err = ESP_OK;
    if (r->scope == CL_SCOPE_FACTORY) {
        err = config_store_erase();
    } else {
        config_store_get(s_cfg);
        const char *const ember[] = {"ember_url", "dev_id", "dev_tok"}, *const wifi[] = {"wifi_ssid", "wifi_pass"};
        const char *const *keys = r->scope == CL_SCOPE_EMBER ? ember : wifi;
        size_t n = r->scope == CL_SCOPE_EMBER ? 3 : 2;
        for (size_t i = 0; i < n; i++) cfg_set(s_cfg, keys[i], "");
        err = config_store_save(s_cfg);
        memset(s_cfg, 0, sizeof *s_cfg);
        if (err == ESP_OK && r->scope == CL_SCOPE_EMBER) err = config_store_clear_settings();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reset %s failed: %s", NAMES[r->scope], esp_err_to_name(err));
        reply_err(r, CL_E_BAD_VALUE);
        return;
    }
    if (r->scope != CL_SCOPE_EMBER) ember_client_forget_wifi();
    ESP_LOGW(TAG, "reset %s over USB: rebooting", NAMES[r->scope]);
    reply_ok(r);
    restart_soon();
}

static void handle_line(const char *json)
{
    cl_err_t e = cl_parse(json, s_req);
    enter_session();
    if (e != CL_OK) {
        reply_err(s_req, e);
    } else {
        switch (s_req->op) {
        case CL_OP_INFO: op_info(s_req); break;
        case CL_OP_SET_EMBER: op_set_ember(s_req); break;
        case CL_OP_STATUS: op_status(s_req); break;
        case CL_OP_RESET: op_reset(s_req); break;
        case CL_OP_DIAG_OVERRIDE: op_diag_override(s_req); break;
        case CL_OP_SNAPSHOT: op_snapshot(s_req); break;
        case CL_OP_CHASE: op_chase(s_req); break;
        case CL_OP_INPUT: op_input(s_req); break;
        case CL_OP_OTA_FAULT: op_ota_fault(s_req); break;
        case CL_OP_OTA_VALID: op_ota_valid(s_req); break;
        case CL_OP_REBOOT:
            ESP_LOGW(TAG, "reboot over USB");
            reply_ok(s_req);
            restart_soon();
            break;
        }
    }
    memset(s_req, 0, sizeof *s_req);
}

static void report(ember_link_t *last_link)
{
    if (s_pending && ember_client_online()) {
        s_pending = false;
        out_improv_state();
        char url[40];
        redirect_url(url, sizeof url);
        const char *s[] = {url};
        out_result(IMPROV_CMD_WIFI, s, 1);
        ESP_LOGW(TAG, "Improv: joined after provisioning");
    } else if (s_pending && !s_join_error_sent && ember_client_join_failures() >= JOIN_FAILS_REPORTED) {
        s_join_error_sent = true;
        out_improv_error(IMPROV_ERR_UNABLE_TO_CONNECT);
        ESP_LOGW(TAG, "Improv: cannot join the new network (still retrying)");
    }
    ember_link_t l = ember_client_link();
    if (l != *last_link) {
        *last_link = l;
        if (l == EMBER_LINK_OK || l == EMBER_LINK_UNREACHABLE || l == EMBER_LINK_UNAUTHORIZED)
            out_write(s_out, cl_event_ember(ember_link_name(l), s_out, CL_LINE_MAX));
    }
}

static void listener_task(void *arg)
{
    (void)arg;
    out_write(s_out, cl_event_boot(esp_app_get_description()->version, config_store_provisioned(), s_out, CL_LINE_MAX));
    out_improv_state();
    ember_link_t last_link = EMBER_LINK_OFF;
    uint8_t in[64];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(in, sizeof in, pdMS_TO_TICKS(100));
        if (n <= 0) prov_rx_idle(&s_rx);
        for (int i = 0; i < n; i++) {
            const uint8_t *o;
            size_t ol;
            switch (prov_rx_feed(&s_rx, in[i], &o, &ol)) {
            case PROV_RX_IMPROV: handle_improv(o, ol); break;
            case PROV_RX_LINE: handle_line((const char *)o); break;
            case PROV_RX_LINE_TOO_LONG:
                out_write(s_out, cl_reply_error(false, 0, CL_E_TOO_LONG, s_out, CL_LINE_MAX));
                break;
            case PROV_RX_NONE: break;
            }
        }
        memset(in, 0, sizeof in);
        report(&last_link);
        session_tick();
    }
}

void provision_usb_start(void)
{
    s_pending = s_improv_pending == IMPROV_PENDING_MAGIC;
    s_improv_pending = 0;
    uint8_t *rxbuf = heap_caps_malloc(CL_LINE_MAX, MALLOC_CAP_SPIRAM);
    s_out = heap_caps_malloc(CL_LINE_MAX, MALLOC_CAP_SPIRAM);
    s_cfg = heap_caps_calloc(1, sizeof *s_cfg, MALLOC_CAP_SPIRAM);
    s_req = heap_caps_calloc(1, sizeof *s_req, MALLOC_CAP_SPIRAM);
    if (!rxbuf || !s_out || !s_cfg || !s_req) {
        ESP_LOGE(TAG, "no memory for the USB listener");
        return;
    }
    prov_rx_init(&s_rx, rxbuf, CL_LINE_MAX);
    usb_serial_jtag_driver_config_t dc = {.rx_buffer_size = RX_RING, .tx_buffer_size = TX_RING};
    esp_err_t err = usb_serial_jtag_driver_install(&dc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB-Serial/JTAG driver: %s", esp_err_to_name(err));
        return;
    }
    usb_serial_jtag_vfs_use_driver();
    xTaskCreatePinnedToCore(listener_task, "prov", 5120, NULL, 2, NULL, 1);
    ESP_LOGI(TAG, "USB provisioning listener up%s", s_pending ? " (after Improv Wi-Fi)" : "");
}
