#include "device_api.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "coredump_up.h"
#include "knob_rotation.h"

static bool json_safe(const char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || *s == '"' || *s == '\\') return false;
    return true;
}

static bool put(char *out, size_t cap, size_t *len, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static bool put(char *out, size_t cap, size_t *len, const char *fmt, ...)
{
    if (*len >= cap) return false;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *len, cap - *len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *len) {
        *len = cap;
        return false;
    }
    *len += (size_t)n;
    return true;
}

static bool cap_id_valid(const char *s, size_t max, bool dash)
{
    if (!s || s[0] < 'a' || s[0] > 'z' || strlen(s) > max) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_' || (dash && *s == '-'))) return false;
    return true;
}

static void put_ids(const char *const *ids, int n, bool page, char *out, size_t cap, size_t *len)
{
    put(out, cap, len, "[");
    const char *sep = "";
    for (int i = 0; ids && i < n; i++) {
        if (!cap_id_valid(ids[i], page ? DEV_CAPS_PAGE_MAX : DEV_CAPS_FEATURE_MAX, page)) continue;
        put(out, cap, len, "%s\"%s\"", sep, ids[i]);
        sep = ",";
    }
    put(out, cap, len, "]");
}

static void put_rotations(const int *r, int n, char *out, size_t cap, size_t *len)
{
    bool seen[4] = {false};
    for (int i = 0; r && i < n; i++)
        if (kr_valid(r[i])) seen[r[i] / 90] = true;
    if (!seen[0]) return;
    put(out, cap, len, ",\"rotations\":[");
    const char *sep = "";
    for (int i = 0; i < n; i++) {
        if (!kr_valid(r[i]) || !seen[r[i] / 90]) continue;
        seen[r[i] / 90] = false;
        put(out, cap, len, "%s%d", sep, r[i]);
        sep = ",";
    }
    put(out, cap, len, "]");
}

static void put_caps(const dev_caps_t *c, char *out, size_t cap, size_t *len)
{
    put(out, cap, len, "\"caps\":{\"features\":");
    put_ids(c->features, c->n_features, false, out, cap, len);
    if (c->view_bytes || c->config_bytes) {
        put(out, cap, len, ",\"limits\":{");
        if (c->config_bytes) put(out, cap, len, "\"config_bytes\":%" PRIu32 "%s", c->config_bytes, c->view_bytes ? "," : "");
        if (c->view_bytes) put(out, cap, len, "\"view_bytes\":%" PRIu32, c->view_bytes);
        put(out, cap, len, "}");
    }
    put(out, cap, len, ",\"pages\":");
    put_ids(c->pages, c->n_pages, true, out, cap, len);
    put_rotations(c->rotations, c->n_rotations, out, cap, len);
    put(out, cap, len, ",\"view\":[%d,%d]},", c->view_min, c->view_max);
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static bool reason_valid(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 16 || s[0] < 'a' || s[0] > 'z') return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) return false;
    return true;
}

static void put_stats(const dev_stats_t *st, char *out, size_t cap, size_t *len)
{
    const char *sep = "";
#define KEY(k) (put(out, cap, len, "%s\"" k "\":", sep), sep = ",")
    put(out, cap, len, "{");
    bool w = !st->no_window;
    if (w && st->n_cpu > 0) {
        KEY("cpu_pct");
        put(out, cap, len, "[");
        int n = st->n_cpu > DEV_STATS_MAX_CPU ? DEV_STATS_MAX_CPU : st->n_cpu;
        for (int i = 0; i < n; i++) put(out, cap, len, "%s%.1f", i ? "," : "", clampf(st->cpu_pct[i], 0, 100));
        put(out, cap, len, "]");
    }
    if (w && st->has_frames) {
        KEY("fps"), put(out, cap, len, "%.1f", st->fps < 0 ? 0 : st->fps);
        KEY("frame_ms_avg"), put(out, cap, len, "%.2f", st->frame_ms_avg < 0 ? 0 : st->frame_ms_avg);
        KEY("frame_ms_max"), put(out, cap, len, "%" PRIu32, st->frame_ms_max);
    }
    if (st->has_heap_min) KEY("heap_internal_min"), put(out, cap, len, "%" PRIu32, st->heap_internal_min);
    uint32_t period = st->period_ms < 1 ? 1 : st->period_ms > 3600000 ? 3600000 : st->period_ms;
    if (w) KEY("period_ms"), put(out, cap, len, "%" PRIu32, period);
    if (st->has_psram) {
        KEY("psram_free"), put(out, cap, len, "%" PRIu32, st->psram_free);
        KEY("psram_largest"), put(out, cap, len, "%" PRIu32, st->psram_largest);
        KEY("psram_min"), put(out, cap, len, "%" PRIu32, st->psram_min);
    }
    if (w && st->has_req) {
        KEY("req_fail"), put(out, cap, len, "%" PRIu32, st->req_fail);
        if (st->req_ok + st->req_fail > 0) {
            KEY("req_ms_avg"), put(out, cap, len, "%.1f", st->req_ms_avg < 0 ? 0 : st->req_ms_avg);
            KEY("req_ms_max"), put(out, cap, len, "%" PRIu32, st->req_ms_max);
        }
        KEY("req_ok"), put(out, cap, len, "%" PRIu32, st->req_ok);
    }
    if (st->reset_reason && reason_valid(st->reset_reason)) KEY("reset_reason"), put(out, cap, len, "\"%s\"", st->reset_reason);
    if (st->has_temp && st->temp_c >= -40 && st->temp_c <= 150) KEY("temp_c"), put(out, cap, len, "%.1f", st->temp_c);
    put(out, cap, len, "}");
#undef KEY
}

static bool name_valid(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > DEV_TASK_NAME_MAX) return false;
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7e || *s == '"' || *s == '\\') return false;
    return true;
}

static void put_diag(const dev_diag_t *d, char *out, size_t cap, size_t *len)
{
    put(out, cap, len, "{\"boots\":%" PRIu32, d->boots);
    if (d->has_crash) {
        const char *reason = d->crash_reason && reason_valid(d->crash_reason) ? d->crash_reason : "unknown";
        put(out, cap, len, ",\"crash\":{");
        if (ota_hex_valid(d->crash_elf, OTA_BUILD_HEX)) put(out, cap, len, "\"elf\":\"%s\",", d->crash_elf);
        if (d->has_crash_id) {
            char id[CD_ID_LEN + 1];
            cd_id_format(d->crash_id, id);
            put(out, cap, len, "\"id\":\"%s\",", id);
        }
        put(out, cap, len, "\"pc\":\"0x%08" PRIx32 "\",\"reason\":\"%s\"", d->crash_pc, reason);
        if (d->has_crash_id) put(out, cap, len, ",\"size\":%" PRIu32, d->crash_size);
        if (name_valid(d->crash_task)) put(out, cap, len, ",\"task\":\"%s\"", d->crash_task);
        put(out, cap, len, "}");
    }
    put(out, cap, len, ",\"heap_internal_min\":%" PRIu32 ",\"heap_largest_min\":%" PRIu32, d->heap_internal_min,
        d->heap_largest_min);
    if (d->reset_reason && reason_valid(d->reset_reason)) put(out, cap, len, ",\"reset_reason\":\"%s\"", d->reset_reason);
    int n = d->n_tasks > DEV_DIAG_MAX_TASKS ? DEV_DIAG_MAX_TASKS : d->n_tasks;
    const char *sep = "";
    for (int i = 0; i < n; i++) {
        if (!name_valid(d->tasks[i].name)) continue;
        if (!*sep) put(out, cap, len, ",\"stack_free\":{");
        put(out, cap, len, "%s\"%s\":%" PRIu32, sep, d->tasks[i].name, d->tasks[i].stack_free);
        sep = ",";
    }
    if (*sep) put(out, cap, len, "}");
    put(out, cap, len, "}");
}

size_t dev_checkin_body(const dev_checkin_t *c, const dev_stats_t *stats, char *out, size_t cap)
{
    const char *fw = c->fw && json_safe(c->fw) && strlen(c->fw) <= 32 ? c->fw : "";
    char ip[48] = "";
    if (c->ip && c->ip[0] && json_safe(c->ip)) snprintf(ip, sizeof ip, ",\"ip\":\"%s\"", c->ip);
    char link[48] = "";
    if (c->link_mhz > 0)
        snprintf(link, sizeof link, ",\"link_fallback\":%s,\"link_mhz\":%d", c->link_fallback ? "true" : "false",
                 c->link_mhz);
    size_t len = 0;
    put(out, cap, &len, "{");
    if (c->caps) put_caps(c->caps, out, cap, &len);
    put(out, cap, &len, "\"config_version\":%" PRIu32, c->config_version);
    if (c->diag) {
        put(out, cap, &len, ",\"diag\":");
        put_diag(c->diag, out, cap, &len);
    }
    put(out, cap, &len, ",\"fw\":\"%s\"", fw);
    if (c->fw_build && ota_hex_valid(c->fw_build, OTA_BUILD_HEX)) put(out, cap, &len, ",\"fw_build\":\"%s\"", c->fw_build);
    put(out, cap, &len, ",\"heap_internal_free\":%" PRIu32 ",\"heap_internal_largest\":%" PRIu32 "%s%s",
        c->heap_internal_free, c->heap_internal_largest, ip, link);
    if (c->ota) {
        char ota[320];
        if (ota_report_json(c->ota, ota, sizeof ota)) put(out, cap, &len, ",\"ota\":%s", ota);
    }
    put(out, cap, &len, ",\"rssi\":%d", c->rssi);
    if (stats) {
        put(out, cap, &len, ",\"stats\":");
        put_stats(stats, out, cap, &len);
    }
    put(out, cap, &len, ",\"uptime_s\":%" PRId64, c->uptime_s);
    const dev_wifi_t *w = c->wifi;
    if (w) {
        put(out, cap, &len, ",\"wifi\":{");
        if (w->has_bssid)
            put(out, cap, &len, "\"bssid\":\"%02x:%02x:%02x:%02x:%02x:%02x\",", w->bssid[0], w->bssid[1], w->bssid[2],
                w->bssid[3], w->bssid[4], w->bssid[5]);
        if (w->channel > 0) put(out, cap, &len, "\"channel\":%d,", w->channel);
        put(out, cap, &len, "\"disconnects\":%" PRIu32, w->disconnects);
        if (w->last_reason > 0) put(out, cap, &len, ",\"last_reason\":%d", w->last_reason);
        if (w->has_rssi_min) put(out, cap, &len, ",\"rssi_min\":%d", w->rssi_min);
        put(out, cap, &len, "}");
    }
    if (!put(out, cap, &len, "}")) return 0;
    return len;
}

const char *dev_reset_reason_name(int reason)
{
    static const char *const N[] = {"unknown", "poweron",  "ext",     "sw",   "panic", "int_wdt",
                                    "task_wdt", "wdt",     "deepsleep", "brownout", "sdio", "usb",
                                    "jtag",    "efuse",    "pwr_glitch", "cpu_lockup"};
    _Static_assert(sizeof N / sizeof N[0] == DEV_RR_COUNT, "one name per esp_reset_reason_t value");
    return reason >= 0 && reason < (int)(sizeof N / sizeof N[0]) ? N[reason] : "unknown";
}

const char *dev_crash_reason_name(int reset_reason)
{
    switch (reset_reason) {
    case DEV_RR_PANIC: case DEV_RR_INT_WDT: case DEV_RR_TASK_WDT: case DEV_RR_WDT: return dev_reset_reason_name(reset_reason);
    case DEV_RR_LVGL_STALL: return "lvgl_stall";
    default: return "unknown";
    }
}

const char *dev_boot_reason_name(int reset_reason, bool lvgl_stall)
{
    return lvgl_stall ? "lvgl_stall" : dev_reset_reason_name(reset_reason);
}

bool dev_stall_boot(dev_stall_note_t *n, int reset_reason)
{
    bool valid = n->magic == DEV_STALL_MAGIC;
    bool stall = valid && n->marked && reset_reason == DEV_RR_PANIC;
    if (!valid || (!stall && reset_reason != DEV_RR_SW)) n->resets = 0;
    else if (stall && n->resets < DEV_STALL_MAX_RESETS) n->resets++;
    n->magic = DEV_STALL_MAGIC;
    n->marked = 0;
    n->seen = 0;
    return stall;
}

dev_stall_act_t dev_stall_check(dev_stall_note_t *n, bool stalled, int64_t uptime_ms)
{
    if (stalled) {
        n->seen = 1;
        if (n->resets >= DEV_STALL_MAX_RESETS) return DEV_STALL_LOG;
        n->marked = 1;
        return DEV_STALL_ABORT;
    }
    if (!n->seen && n->resets && uptime_ms >= DEV_STALL_CLEAR_MS) n->resets = 0;
    return DEV_STALL_NONE;
}

void dev_twdt_capture_msg(dev_twdt_capture_t *c, const char *msg)
{
    if (!msg) return;
    if (c->state == 0) {
        if (msg[0] == '\n' && msg[1] == ' ' && msg[2] == '-' && msg[3] == ' ' && msg[4] == 0) c->state = 1;
        return;
    }
    if (c->state != 1) return;
    size_t i = 0;
    for (; msg[i] && i < DEV_TASK_NAME_MAX; i++) c->name[i] = msg[i];
    c->name[i] = 0;
    c->state = 2;
}

const char *dev_crash_task(const char *reason, const char *wdt_culprit, const char *dump_task)
{
    if (reason && strcmp(reason, "task_wdt") == 0) return wdt_culprit && wdt_culprit[0] ? wdt_culprit : "twdt";
    if (reason && strcmp(reason, "lvgl_stall") == 0) return "lvgl";
    return dump_task ? dump_task : "";
}

bool dev_window_ok(int64_t period_us) { return period_us > 0 && period_us <= 3600000000LL; }

float dev_cpu_pct(uint64_t idle_delta_us, uint64_t period_us)
{
    if (period_us == 0) return 0;
    float busy = 100.0f * (1.0f - (float)idle_delta_us / (float)period_us);
    return clampf(busy, 0, 100);
}

int64_t dev_live_deadline_ms(long long live_until, long long server_now, int64_t now_ms)
{
    if (live_until <= 0 || server_now <= 0 || live_until <= server_now) return 0;
    long long left_ms = (live_until - server_now) * 1000LL;
    if (left_ms > DEV_LIVE_MAX_MS) left_ms = DEV_LIVE_MAX_MS;
    return now_ms + left_ms;
}

static bool token_valid(const char *t)
{
    size_t n = strlen(t);
    if (n == 0 || n > DEV_TOKEN_MAX) return false;
    for (; *t; t++)
        if ((unsigned char)*t <= 0x20 || (unsigned char)*t >= 0x7f) return false;
    return true;
}

static bool depth_ok(const cJSON *root)
{
    const cJSON *st[DEV_CONFIG_MAX_DEPTH + 1];
    int d = 0;
    st[0] = root;
    for (;;) {
        const cJSON *n = st[d];
        if (n && (cJSON_IsObject(n) || cJSON_IsArray(n)) && d >= DEV_CONFIG_MAX_DEPTH) return false;
        if (n && n->child) {
            st[++d] = n->child;
            continue;
        }
        if (d == 0) return true;
        if (n) {
            st[d] = n->next;
            continue;
        }
        if (--d == 0) return true;
        st[d] = st[d]->next;
    }
}

void dev_checkin_parse(const char *json, dev_checkin_result_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "config_version");
    if (cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble <= 4294967295.0) {
        out->ok = true;
        out->config_version = (uint32_t)v->valuedouble;
    }
    const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(root, "config");
    if (out->ok && cJSON_IsObject(cfg)) {
        if (depth_ok(cfg)) out->config = cJSON_PrintUnformatted(cfg);
        else out->config_too_deep = true;
    }
    const char *tok = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "new_token"));
    if (tok && token_valid(tok)) {
        out->has_new_token = true;
        strcpy(out->new_token, tok);
    }
    const cJSON *lu = cJSON_GetObjectItemCaseSensitive(root, "diag_live_until");
    if (cJSON_IsNumber(lu) && lu->valuedouble > 0) out->diag_live_until = (long long)lu->valuedouble;
    out->has_coredump_wanted =
        cd_id_parse(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "coredump_wanted")), &out->coredump_wanted);
    out->has_coredump_ack =
        cd_id_parse(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "coredump_ack")), &out->coredump_ack);
    const cJSON *ota = cJSON_GetObjectItemCaseSensitive(root, "ota");
    if (ota) out->has_ota = ota_offer_parse(ota, DEV_OTA_MAX_SIZE, &out->ota);
    cJSON_Delete(root);
}

void dev_checkin_result_free(dev_checkin_result_t *r)
{
    free(r->config);
    r->config = NULL;
    memset(r->new_token, 0, sizeof r->new_token);
    r->has_new_token = false;
}

void dev_sched_init(dev_sched_t *s, int64_t now_ms)
{
    memset(s, 0, sizeof *s);
    s->next_ms = now_ms;
    s->last_ms = -1;
    s->period_ms = DEV_CHECKIN_PERIOD_MS;
    s->live_ms = DEV_CHECKIN_LIVE_MS;
}

int dev_checkin_period_ms(int stats_interval_ms, bool diag_on)
{
    if (diag_on && stats_interval_ms > 0 && stats_interval_ms < DEV_CHECKIN_PERIOD_MS) return stats_interval_ms;
    return DEV_CHECKIN_PERIOD_MS;
}

bool dev_stats_due(int64_t window_ms, int stats_interval_ms, int period_ms, bool live, bool first)
{
    if (live || first) return true;
    return window_ms >= (int64_t)stats_interval_ms - period_ms / 2;
}

static int sched_period(const dev_sched_t *s, int64_t now_ms)
{
    return s->live_until_ms > now_ms ? s->live_ms : s->period_ms;
}

void dev_sched_intervals(dev_sched_t *s, int period_ms, int live_ms, int64_t now_ms)
{
    if (period_ms <= 0 || live_ms <= 0) return;
    if (s->period_ms == period_ms && s->live_ms == live_ms) return;
    s->period_ms = period_ms;
    s->live_ms = live_ms;
    if (s->backoff_ms || s->last_ms < 0) return;
    int64_t at = s->last_ms + sched_period(s, now_ms);
    if (at < now_ms) at = now_ms;
    if (at < s->next_ms) s->next_ms = at;
}

bool dev_sched_due(const dev_sched_t *s, int64_t now_ms) { return now_ms >= s->next_ms; }

void dev_sched_epoch(dev_sched_t *s, const char *epoch, int64_t now_ms)
{
    if (!epoch || !epoch[0]) return;
    if (s->have_epoch && strncmp(s->epoch, epoch, sizeof s->epoch - 1) != 0) {
        int64_t soonest = s->last_ms < 0 ? now_ms : s->last_ms + DEV_CHECKIN_MIN_GAP_MS;
        int64_t at = soonest > now_ms ? soonest : now_ms;
        if (at < s->next_ms) s->next_ms = at;
    }
    s->have_epoch = true;
    snprintf(s->epoch, sizeof s->epoch, "%s", epoch);
}

void dev_sched_epoch_reset(dev_sched_t *s)
{
    s->have_epoch = false;
    s->epoch[0] = 0;
}

void dev_sched_done(dev_sched_t *s, dev_checkin_outcome_t o, int64_t now_ms)
{
    s->last_ms = now_ms;
    if (o == DEV_CHECKIN_FAILED) {
        s->backoff_ms = s->backoff_ms ? s->backoff_ms * 2 : DEV_CHECKIN_PERIOD_MS;
        if (s->backoff_ms > DEV_CHECKIN_BACKOFF_MAX_MS) s->backoff_ms = DEV_CHECKIN_BACKOFF_MAX_MS;
        s->next_ms = now_ms + s->backoff_ms;
        return;
    }
    s->backoff_ms = 0;
    s->next_ms = now_ms + (o == DEV_CHECKIN_UNAUTHORIZED ? DEV_CHECKIN_PERIOD_MS : sched_period(s, now_ms));
}

void dev_sched_live(dev_sched_t *s, int64_t deadline_ms, int64_t now_ms)
{
    bool was = s->live_until_ms > now_ms;
    s->live_until_ms = deadline_ms > now_ms ? deadline_ms : 0;
    if (!was && s->live_until_ms && !s->backoff_ms) {
        int64_t at = s->last_ms < 0 ? now_ms : s->last_ms + s->live_ms;
        if (at < now_ms) at = now_ms;
        if (at < s->next_ms) s->next_ms = at;
    }
}

void dev_sched_now(dev_sched_t *s, int64_t now_ms)
{
    if (s->next_ms > now_ms) s->next_ms = now_ms;
}

dev_link_t dev_link_combine(dev_link_t poll, bool has_device, bool checked_in, bool unauthorized)
{
    if (poll != DEV_LINK_OK || !has_device) return poll;
    if (unauthorized) return DEV_LINK_UNAUTHORIZED;
    return checked_in ? DEV_LINK_OK : DEV_LINK_CONNECTING;
}
