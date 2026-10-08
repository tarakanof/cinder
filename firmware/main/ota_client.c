#include "ota_client.h"

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bsp_knob_15_md50et.h"
#include "cfg.h"
#include "diag.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "ota_face.h"

#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE || CONFIG_BOOTLOADER_PROJECT_VER < 2
#error "OTA needs CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE and BOOTLOADER_PROJECT_VER >= 2: delete firmware/sdkconfig to regenerate it"
#endif

static const char *TAG = "ota";

#define NS "ota"
#define PIECE 4096
#define YIELD_EVERY (64 * 1024)

#if CONFIG_CINDER_OTA_TEST
#define ROLLBACK_LIMIT_MS (2 * 60 * 1000)
#define TEST_FAULT CONFIG_CINDER_OTA_TEST_FAULT
#else
#define ROLLBACK_LIMIT_MS OTA_ROLLBACK_MS
#define TEST_FAULT ""
#endif
#define NO_RENDER_AFTER_MS (10 * 1000)

typedef enum { PHASE_IDLE, PHASE_WAITING, PHASE_REBOOTING } phase_t;

typedef struct {
    ota_rec_t rec;
    ota_gate_t gate;
    bool rollback_ok;
    uint32_t boot_ver;
    char boot_idf[33];
    int slot;
    bool img_ok;
    uint32_t img_state;
    char build[OTA_BUILD_HEX + 1];
    ota_backoff_t rb;
    int mark_fails;
    bool has_offer;
    ota_offer_t offer;
    phase_t phase;
    const esp_partition_t *ready;
    char ready_ver[OTA_VERSION_MAX + 1];
    ota_decision_t last_wait;
    ota_health_in_t health;
    const char *health_shown;
    bool health_logged;
    char url[CFG_URL_MAX + 64];
} ota_state_t;

static ota_state_t *O;
static atomic_bool s_verifying;
static atomic_bool s_view_ok;
static atomic_uint s_frames, s_loop_ms;
static atomic_int s_link = OTA_LINK_PENDING;
static atomic_bool s_checkin_seen, s_switching, s_rec_valid_due;
static _Atomic int64_t s_last_input_us = -1;
static atomic_bool s_fault_net, s_fault_sha;

static int64_t uptime_ms(void) { return esp_timer_get_time() / 1000; }

static void get_str(nvs_handle_t h, const char *key, char *out, size_t cap)
{
    size_t n = cap;
    if (nvs_get_str(h, key, out, &n) != ESP_OK) out[0] = 0;
}

static void rec_load(ota_rec_t *r)
{
    memset(r, 0, sizeof *r);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t u8 = 0;
    if (nvs_get_u8(h, "att_state", &u8) == ESP_OK && u8 <= OTA_ATT_BOOT) r->att_state = (ota_att_state_t)u8;
    nvs_get_u32(h, "att_attempt", &r->att_attempt);
    get_str(h, "att_sha", r->att_sha, sizeof r->att_sha);
    get_str(h, "att_ver", r->att_ver, sizeof r->att_ver);
    get_str(h, "att_build", r->att_build, sizeof r->att_build);
    if (nvs_get_u8(h, "last_res", &u8) == ESP_OK && u8 <= OTA_RES_ROLLED_BACK) r->last.result = (ota_result_t)u8;
    nvs_get_u32(h, "last_att", &r->last.attempt);
    get_str(h, "last_err", r->last.error, sizeof r->last.error);
    get_str(h, "last_ver", r->last.version, sizeof r->last.version);
    get_str(h, "bad", r->bad, sizeof r->bad);
    nvs_close(h);
}

static esp_err_t put_str(nvs_handle_t h, const char *key, const char *v)
{
    if (v[0]) return nvs_set_str(h, key, v);
    esp_err_t err = nvs_erase_key(h, key);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

static void rec_save(void)
{
    const ota_rec_t *r = &O->rec;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "att_state", (uint8_t)r->att_state);
        if (err == ESP_OK) err = nvs_set_u32(h, "att_attempt", r->att_attempt);
        if (err == ESP_OK) err = put_str(h, "att_sha", r->att_sha);
        if (err == ESP_OK) err = put_str(h, "att_ver", r->att_ver);
        if (err == ESP_OK) err = put_str(h, "att_build", r->att_build);
        if (err == ESP_OK) err = nvs_set_u8(h, "last_res", (uint8_t)r->last.result);
        if (err == ESP_OK) err = nvs_set_u32(h, "last_att", r->last.attempt);
        if (err == ESP_OK) err = put_str(h, "last_err", r->last.error);
        if (err == ESP_OK) err = put_str(h, "last_ver", r->last.version);
        if (err == ESP_OK) err = put_str(h, "bad", r->bad);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "saving the update record failed: %s", esp_err_to_name(err));
}

static void read_state(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    O->slot = run && run->type == ESP_PARTITION_TYPE_APP && run->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                      run->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX
                  ? run->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN
                  : -1;
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    O->img_ok = run && esp_ota_get_state_partition(run, &st) == ESP_OK;
    O->img_state = (uint32_t)st;
}

#if CONFIG_CINDER_OTA_TEST
static void crash_cb(void *arg)
{
    (void)arg;
    ESP_LOGE(TAG, "OTA test fault crash_boot: aborting");
    abort();
}
#endif

void ota_client_boot(void)
{
    O = heap_caps_calloc(1, sizeof *O, MALLOC_CAP_SPIRAM);
    assert(O);
    const esp_app_desc_t *app = esp_app_get_description();
    ota_build_hex(app->app_elf_sha256, O->build);
    esp_bootloader_desc_t bd;
    if (esp_ota_get_bootloader_description(NULL, &bd) == ESP_OK) {
        O->boot_ver = bd.version;
        O->rollback_ok = ota_rollback_capable(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, bd.version);
        snprintf(O->boot_idf, sizeof O->boot_idf, "%.32s", bd.idf_ver);
    }
    read_state();
    rec_load(&O->rec);
    esp_app_desc_t inv;
    char inv_build[OTA_BUILD_HEX + 1] = "";
    const esp_partition_t *ip = O->rec.att_state == OTA_ATT_BOOT ? esp_ota_get_last_invalid_partition() : NULL;
    bool has_inv = ip && esp_ota_get_partition_description(ip, &inv) == ESP_OK;
    if (has_inv) ota_build_hex(inv.app_elf_sha256, inv_build);
    bool pending = O->img_ok && O->img_state == ESP_OTA_IMG_PENDING_VERIFY;
    ota_boot_in_t in = {
        .pending_verify = pending,
        .running_build = O->build,
        .running_ver = app->version,
        .has_invalid = has_inv,
        .invalid_build = inv_build,
        .invalid_ver = has_inv ? inv.version : NULL,
    };
    ota_boot_t b = ota_rec_boot(&O->rec, &in);
    if (b == OTA_BOOT_OK || b == OTA_BOOT_ROLLED_BACK || b == OTA_BOOT_INTERRUPTED) rec_save();
    atomic_store(&s_verifying, pending);
    static const char *const B[] = {"", "pending verification", "installed", "rolled back", "interrupted"};
    ESP_LOGI(TAG, "slot %d, image %s, build %s, bootloader v%" PRIu32 " (%s)%s%s", O->slot,
             ota_image_name(O->img_ok, O->img_state), O->build, O->boot_ver, O->rollback_ok ? "rollback" : "no rollback", b ? ": update " : "", B[b]);
    if (b == OTA_BOOT_ROLLED_BACK || b == OTA_BOOT_INTERRUPTED)
        ESP_LOGW(TAG, "last update %s: %s %s", O->rec.last.version, ota_result_name(O->rec.last.result),
                 O->rec.last.error);
#if CONFIG_CINDER_OTA_TEST
    if (pending && strcmp(TEST_FAULT, "crash_boot") == 0) {
        static esp_timer_handle_t t;
        const esp_timer_create_args_t a = {.callback = crash_cb, .name = "ota_crash"};
        if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, 20 * 1000000ULL);
        ESP_LOGW(TAG, "OTA test fault crash_boot armed (20 s)");
    }
#endif
}

void ota_client_note_frame(void) { atomic_fetch_add_explicit(&s_frames, 1, memory_order_relaxed); }

void ota_client_note_loop(void)
{
    atomic_store_explicit(&s_loop_ms, (unsigned)uptime_ms() | 1u, memory_order_relaxed);
}

void ota_client_note_link(ota_link_t r)
{
    if (r == OTA_LINK_FAIL) {
        atomic_store(&s_link, OTA_LINK_FAIL);
        return;
    }
    int pending = OTA_LINK_PENDING;
    if (r == OTA_LINK_OK) atomic_compare_exchange_strong(&s_link, &pending, OTA_LINK_OK);
}

void ota_client_note_view_ok(void) { atomic_store(&s_view_ok, true); }
void ota_client_note_input(void) { atomic_store(&s_last_input_us, esp_timer_get_time()); }

bool ota_client_verifying(void) { return atomic_load(&s_verifying); }

bool ota_client_checkin_blocked(void) { return atomic_load(&s_verifying) && strcmp(TEST_FAULT, "no_checkin") == 0; }

bool ota_client_render_frozen(void)
{
    return atomic_load(&s_verifying) && strcmp(TEST_FAULT, "no_render") == 0 && uptime_ms() >= NO_RENDER_AFTER_MS;
}

static bool switch_enter(void)
{
    bool idle = false;
    return atomic_compare_exchange_strong(&s_switching, &idle, true);
}

static void switch_leave(void) { atomic_store(&s_switching, false); }

static int64_t age_ms(uint32_t at, int64_t now)
{
    if (!at) return -1;
    int64_t a = now - (int64_t)at;
    return a < 0 ? 0 : a;
}

static ota_health_t health_read(void)
{
    ota_health_in_t *h = &O->health;
    int64_t now = uptime_ms();
    memset(h, 0, sizeof *h);
    h->link = (ota_link_t)atomic_load(&s_link);
    h->frames = atomic_load(&s_frames);
    h->loop_age_ms = age_ms(atomic_load(&s_loop_ms), now);
    h->touch_reads = bsp_knob_15_md50et_touch_reads();
    h->touch_age_ms = age_ms(bsp_knob_15_md50et_touch_read_ms(), now);
    h->input_seen = atomic_load(&s_last_input_us) >= 0;
    diag_health(h);
    ota_health_t r = ota_health_check(h);
    if (!O->health_logged || r.reason != O->health_shown) {
        O->health_logged = true;
        O->health_shown = r.reason;
        if (r.reason)
            ESP_LOGW(TAG, "health %s: %s (link %d, frames %" PRIu32 ", loop %lld ms, touch reads %" PRIu32
                          ", heap min %" PRIu32 " B, largest min %" PRIu32 " B)",
                     r.state == OTA_HEALTH_FAIL ? "failed" : "pending", r.reason, (int)h->link, h->frames,
                     (long long)h->loop_age_ms, h->touch_reads, h->heap_internal_min, h->heap_largest_min);
        else
            ESP_LOGI(TAG, "health checks pass");
    }
    return r;
}

static ota_valid_in_t valid_in(bool checkin_ok, ota_health_state_t health)
{
    return (ota_valid_in_t){
        .uptime_ms = uptime_ms(),
        .checkin_ok = checkin_ok,
        .frame = atomic_load(&s_frames) > 0,
        .view_ok = atomic_load(&s_view_ok),
        .health = health,
    };
}

ota_mark_t ota_client_mark_valid(void)
{
    if (!O) return OTA_MARK_NOT_PENDING;
    ota_valid_in_t v = valid_in(atomic_load(&s_checkin_seen), OTA_HEALTH_PENDING);
    switch (ota_override_check(atomic_load(&s_verifying), &v)) {
    case OTA_OVERRIDE_NOT_PENDING: return OTA_MARK_NOT_PENDING;
    case OTA_OVERRIDE_NO_CHECKIN: return OTA_MARK_NO_CHECKIN;
    case OTA_OVERRIDE_NOT_READY: return OTA_MARK_NOT_READY;
    case OTA_OVERRIDE_OK: break;
    }
    if (!switch_enter()) return OTA_MARK_BUSY;
    if (!atomic_load(&s_verifying)) {
        switch_leave();
        return OTA_MARK_NOT_PENDING;
    }
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        switch_leave();
        ESP_LOGE(TAG, "marking the image valid over USB failed: %s", esp_err_to_name(err));
        return OTA_MARK_FAILED;
    }
    atomic_store(&s_verifying, false);
    atomic_store(&s_rec_valid_due, true);
    switch_leave();
    ESP_LOGW(TAG, "image %s marked valid over USB (health checks skipped)", esp_app_get_description()->version);
    return OTA_MARK_OK;
}

bool ota_client_fault(const char *fault)
{
#if CONFIG_CINDER_OTA_TEST
    if (fault && strcmp(fault, "net") == 0) atomic_store(&s_fault_net, true);
    else if (fault && strcmp(fault, "sha") == 0) atomic_store(&s_fault_sha, true);
    else return false;
    ESP_LOGW(TAG, "OTA test fault %s armed for the next download", fault);
    return true;
#else
    (void)fault;
    return false;
#endif
}

const char *ota_client_build(void) { return O ? O->build : NULL; }

void ota_client_report(ota_report_t *out)
{
    static const char *const PH[] = {"idle", "waiting", "rebooting"};
    *out = (ota_report_t){
        .rollback = O->rollback_ok,
        .slot = O->slot,
        .image = ota_image_name(O->img_ok, O->img_state),
        .phase = PH[O->phase],
        .last = &O->rec.last,
    };
}

static void roll_back(const char *why)
{
    if (!ota_backoff_due(&O->rb, uptime_ms())) return;
    if (!switch_enter()) return;
    if (!atomic_load(&s_verifying)) {
        switch_leave();
        return;
    }
    if (ota_rec_rollback(&O->rec, why)) rec_save();
    ESP_LOGE(TAG, "%s: rolling back to the previous image", why);
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    switch_leave();
    ota_backoff_failed(&O->rb, uptime_ms());
    ESP_LOGE(TAG, "rollback failed: %s; retry in %d min", esp_err_to_name(err), ota_backoff_ms(O->rb.fails) / 60000);
}

void ota_client_checkin_done(int status, const dev_checkin_result_t *r, const ota_ctx_t *ctx)
{
    if (O->phase == PHASE_REBOOTING) return;
    O->has_offer = status == 200 && r && r->has_ota;
    if (O->has_offer) O->offer = r->ota;
    if (!atomic_load(&s_verifying)) return;
    if (status == 200) atomic_store(&s_checkin_seen, true);
    ota_valid_in_t v = valid_in(status == 200, health_read().state);
    if (!ota_valid_ready(&v)) return;
    if (!switch_enter()) return;
    if (!atomic_load(&s_verifying)) {
        switch_leave();
        return;
    }
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        switch_leave();
        ESP_LOGE(TAG, "marking the image valid failed: %s", esp_err_to_name(err));
        if (++O->mark_fails >= OTA_MARK_TRIES) roll_back("mark_valid");
        return;
    }
    atomic_store(&s_verifying, false);
    switch_leave();
    read_state();
    ota_rec_valid(&O->rec);
    rec_save();
    ESP_LOGI(TAG, "image %s marked valid after %lld s", esp_app_get_description()->version,
             (long long)(uptime_ms() / 1000));
    ctx->checkin_soon();
}

typedef struct {
    const ota_ctx_t *ctx;
    ota_offer_t pinned;
    const ota_offer_t *offer;
    const esp_partition_t *part;
    esp_ota_handle_t h;
    bool open;
    mbedtls_sha256_context sha;
    uint32_t written, from, next_yield;
    uint8_t hdr[OTA_HEADER_LEN];
    uint32_t hdr_have;
    bool hdr_ok;
    bool req_checked;
    int status;
    int64_t cl;
    bool has_cr;
    uint32_t cr_start;
    bool fault_net;
    int64_t t0_ms;
    char err[OTA_ERROR_MAX + 1];
} dl_t;

static bool dl_begin(dl_t *d)
{
    mbedtls_sha256_init(&d->sha);
    mbedtls_sha256_starts(&d->sha, 0);
    d->written = d->hdr_have = 0;
    d->hdr_ok = false;
    d->next_yield = YIELD_EVERY;
    esp_err_t err = esp_ota_begin(d->part, OTA_WITH_SEQUENTIAL_WRITES, &d->h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        strcpy(d->err, "flash");
        return false;
    }
    d->open = true;
    return true;
}

static void dl_abort(dl_t *d)
{
    if (d->open) esp_ota_abort(d->h);
    d->open = false;
    mbedtls_sha256_free(&d->sha);
}

static bool dl_write(dl_t *d, const uint8_t *p, int n)
{
    if (d->written + (uint32_t)n > d->offer->size) {
        strcpy(d->err, "size");
        return false;
    }
    mbedtls_sha256_update(&d->sha, p, (size_t)n);
    esp_err_t err = esp_ota_write(d->h, p, (size_t)n);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write at %" PRIu32 ": %s", d->written, esp_err_to_name(err));
        strcpy(d->err, "flash");
        return false;
    }
    d->written += (uint32_t)n;
    ota_face_pct(ota_pct(d->written, d->offer->size));
    if (d->written >= d->next_yield) {
        d->next_yield += YIELD_EVERY;
        vTaskDelay(1);
    }
    return true;
}

static void dl_header(const char *key, const char *value, void *ctx)
{
    dl_t *d = ctx;
    if (strcasecmp(key, "Content-Range") == 0) d->has_cr = ota_content_range_start(value, &d->cr_start);
}

static bool dl_check_response(dl_t *d)
{
    d->req_checked = true;
    ota_resp_t a = ota_resp_check(d->status, d->from, d->has_cr, d->cr_start, d->cl, d->offer->size, d->err);
    if (a == OTA_RESP_FAIL) return false;
    if (a == OTA_RESP_RESTART) {
        ESP_LOGW(TAG, "server answered the resume with the whole image: restarting from 0");
        dl_abort(d);
        if (!dl_begin(d)) return false;
    }
    return true;
}

static bool dl_sink(void *arg, const uint8_t *p, int n)
{
    dl_t *d = arg;
    d->ctx->alive();
    if (!d->req_checked && !dl_check_response(d)) return false;
    if (uptime_ms() - d->t0_ms > OTA_ATTEMPT_MS) {
        strcpy(d->err, "net");
        return false;
    }
    if (!d->hdr_ok) {
        uint32_t take = OTA_HEADER_LEN - d->hdr_have;
        if (take > (uint32_t)n) take = (uint32_t)n;
        memcpy(d->hdr + d->hdr_have, p, take);
        d->hdr_have += take;
        p += take;
        n -= (int)take;
        if (d->hdr_have < OTA_HEADER_LEN) return true;
        const char *e = ota_header_check(d->hdr, OTA_HEADER_LEN, d->offer->version, d->offer->build, O->boot_idf);
        if (e) {
            strcpy(d->err, e);
            return false;
        }
        d->hdr_ok = true;
        if (!dl_write(d, d->hdr, OTA_HEADER_LEN)) return false;
    }
    if (n > 0 && !dl_write(d, p, n)) return false;
    if (atomic_load(&s_fault_net) && d->written >= d->offer->size / 2) {
        atomic_store(&s_fault_net, false);
        d->fault_net = true;
        ESP_LOGW(TAG, "OTA test fault net at %" PRIu32 " B", d->written);
        return false;
    }
    return true;
}

static void sleep_alive(const ota_ctx_t *ctx, int ms)
{
    for (; ms > 0; ms -= 1000) {
        ctx->alive();
        vTaskDelay(pdMS_TO_TICKS(ms < 1000 ? ms : 1000));
    }
}

static bool sha_matches(dl_t *d)
{
    uint8_t sum[32];
    mbedtls_sha256_finish(&d->sha, sum);
    char hex[OTA_SHA_HEX + 1];
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", sum[i]);
    if (atomic_exchange(&s_fault_sha, false)) {
        ESP_LOGW(TAG, "OTA test fault sha");
        hex[0] = hex[0] == '0' ? '1' : '0';
    }
    return strcmp(hex, d->offer->sha256) == 0;
}

static bool download(dl_t *d, uint8_t *piece)
{
    const ota_ctx_t *ctx = d->ctx;
    snprintf(O->url, sizeof O->url, "%s%s%s", ctx->base, OTA_URL_PREFIX, d->offer->version);
    char range[32], if_range[OTA_SHA_HEX + 3];
    snprintf(if_range, sizeof if_range, "\"%s\"", d->offer->sha256);
    int resumes = 0;
    for (;;) {
        d->from = d->written;
        if (d->written == 0) d->hdr_have = 0;
        d->req_checked = false;
        d->status = 0;
        d->cl = -1;
        d->has_cr = false;
        d->fault_net = false;
        snprintf(range, sizeof range, "bytes=%" PRIu32 "-", d->from);
        const http_stream_opts_t o = {
            .range = d->from ? range : NULL,
            .if_range = d->from ? if_range : NULL,
            .on_header = dl_header,
            .hdr_ctx = d,
            .sink = dl_sink,
            .sink_ctx = d,
            .piece = piece,
            .piece_cap = PIECE,
            .content_length = &d->cl,
            .status = &d->status,
        };
        int st = http_conn_stream(ctx->conn, O->url, &o);
        ctx->alive();
        if (d->err[0]) return false;
        if (st == HTTP_CONN_ABORTED && d->fault_net) st = -1;
        if (st == 200 || st == 206) {
            if (!d->req_checked && !dl_check_response(d)) return false;
            if (d->written == d->offer->size) return true;
            strcpy(d->err, "size");
            return false;
        }
        if (!ota_resumable(st)) {
            ota_http_error(st < 0 ? 0 : st, d->err);
            return false;
        }
        int wait = ota_resume_delay_ms(resumes++);
        if (wait < 0 || uptime_ms() - d->t0_ms + wait > OTA_ATTEMPT_MS) {
            if (st < 0) strcpy(d->err, "net");
            else ota_http_error(st, d->err);
            return false;
        }
        ESP_LOGW(TAG, "download stopped at %" PRIu32 " B (%d); resume %d in %d s", d->written, st, resumes,
                 wait / 1000);
        sleep_alive(ctx, wait);
    }
}

static void fail(const char *error, bool bad)
{
    ota_rec_fail(&O->rec, error, bad);
    rec_save();
    ota_gate_failed(&O->gate, uptime_ms());
    ESP_LOGE(TAG, "update to %s failed: %s%s; retry in %d min", O->rec.last.version, error,
             bad ? " (image blocked until a retry)" : "", ota_backoff_ms(O->gate.fails) / 60000);
}

static void run_update(const ota_ctx_t *ctx)
{
    ota_gate_started(&O->gate);
    ota_rec_start(&O->rec, &O->offer);
    rec_save();
    dl_t *d = heap_caps_calloc(1, sizeof *d, MALLOC_CAP_SPIRAM);
    uint8_t *piece = heap_caps_malloc(PIECE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!piece) {
        ESP_LOGW(TAG, "no internal RAM for the 4 KB staging buffer: using PSRAM (slower flash writes)");
        piece = heap_caps_malloc(PIECE, MALLOC_CAP_SPIRAM);
    }
    if (!d || !piece) {
        free(d);
        free(piece);
        fail("flash", false);
        ctx->checkin_soon();
        return;
    }
    d->ctx = ctx;
    d->pinned = O->offer;
    d->offer = &d->pinned;
    const ota_offer_t *offer = d->offer;
    d->part = esp_ota_get_next_update_partition(NULL);
    d->t0_ms = uptime_ms();
    ESP_LOGI(TAG, "updating to %s (%" PRIu32 " B, attempt %" PRIu32 ") into %s", offer->version, offer->size,
             offer->attempt, d->part ? d->part->label : "?");
    ota_face_show(offer->version);
    ctx->alive();
    bool ok = d->part && offer->size <= d->part->size;
    if (!ok) strcpy(d->err, d->part ? "size" : "flash");
    if (ok) ok = dl_begin(d) && download(d, piece);
    free(piece);
    bool bad = false;
    if (ok && d->written != offer->size) {
        strcpy(d->err, "size");
        ok = false;
    }
    if (ok && !sha_matches(d)) {
        strcpy(d->err, "sha256");
        ok = false;
        bad = true;
    }
    if (ok) {
        d->open = false;
        mbedtls_sha256_free(&d->sha);
        ctx->alive();
        esp_err_t err = esp_ota_end(d->h);
        ctx->alive();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
            strcpy(d->err, "image");
            ok = false;
            bad = true;
        }
    } else {
        dl_abort(d);
    }
    int64_t ms = uptime_ms() - d->t0_ms;
    if (ok) {
        ESP_LOGI(TAG, "%s downloaded and verified in %lld ms", offer->version, (long long)ms);
        O->ready = d->part;
        ota_rec_ready(&O->rec);
        rec_save();
        snprintf(O->ready_ver, sizeof O->ready_ver, "%s", offer->version);
    } else {
        fail(d->err, bad);
        ota_face_hide();
        ctx->checkin_soon();
    }
    free(d);
}

static void finish(const ota_ctx_t *ctx)
{
    bool pomo = ctx->pomo_active();
    if (!pomo) pomo = !ctx->fresh_view() || ctx->pomo_active();
    ctx->alive();
    if (pomo) {
        if (O->phase != PHASE_WAITING) {
            O->phase = PHASE_WAITING;
            ota_face_hide();
            ESP_LOGI(TAG, "%s ready; waiting for the Pomodoro to end", O->ready_ver);
            ctx->checkin_soon();
        }
        return;
    }
    ota_rec_rebooting(&O->rec);
    rec_save();
    esp_err_t err = esp_ota_set_boot_partition(O->ready);
    ctx->alive();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        O->ready = NULL;
        O->phase = PHASE_IDLE;
        fail("flash", false);
        ota_face_hide();
        ctx->checkin_soon();
        return;
    }
    O->phase = PHASE_REBOOTING;
    ota_face_restarting();
    ESP_LOGW(TAG, "restarting into %s", O->ready_ver);
    ctx->checkin();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

void ota_client_service(const ota_ctx_t *ctx)
{
    if (atomic_exchange(&s_rec_valid_due, false)) {
        read_state();
        ota_rec_valid(&O->rec);
        rec_save();
        ctx->checkin_soon();
    }
    if (atomic_load(&s_verifying)) {
        bool due = ota_rollback_due(uptime_ms(), ctx->pomo_active(), ROLLBACK_LIMIT_MS);
        if (!due && atomic_load(&s_link) != OTA_LINK_FAIL) return;
        ota_health_t h = health_read();
        const char *why = ota_verify_rollback(due, atomic_load(&s_checkin_seen), &h);
        if (why) roll_back(why);
        return;
    }
    if (O->ready) {
        finish(ctx);
        return;
    }
    if (!O->has_offer) return;
    O->has_offer = false;
    if (ota_rec_retry(&O->rec, &O->offer)) {
        rec_save();
        ESP_LOGI(TAG, "retry from Ember: %s allowed again", O->offer.version);
    }
    int64_t last = atomic_load(&s_last_input_us);
    ota_env_t e = {
        .rollback = O->rollback_ok,
        .image_valid = O->img_ok && O->img_state == ESP_OTA_IMG_VALID,
        .online = ctx->online,
        .paired = ctx->paired,
        .pomo_active = ctx->pomo_active(),
        .now_ms = uptime_ms(),
        .last_input_ms = last < 0 ? -1 : last / 1000,
    };
    ota_decision_t dec = ota_start_decide(&O->gate, &O->rec, &O->offer, &e);
    if (dec == OTA_GO) {
        O->last_wait = OTA_GO;
        run_update(ctx);
        if (O->ready) finish(ctx);
        return;
    }
    if (dec == OTA_REFUSE_BAD || dec == OTA_REFUSE_CAP) {
        if (ota_rec_refuse(&O->rec, &O->offer)) {
            rec_save();
            ESP_LOGW(TAG, "update to %s refused (%s)", O->offer.version, ota_decision_name(dec));
            ctx->checkin_soon();
        }
        return;
    }
    if (dec != O->last_wait) ESP_LOGI(TAG, "update to %s waits: %s", O->offer.version, ota_decision_name(dec));
    O->last_wait = dec;
}
