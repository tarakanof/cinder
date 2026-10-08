#include "ota_policy.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool ident_ok(const char *s, size_t n, bool numeric_only)
{
    if (n == 0) return false;
    bool all_digits = true;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (is_digit(c)) continue;
        all_digits = false;
        if (numeric_only) return false;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-')) return false;
    }
    if (all_digits && n > 1 && s[0] == '0') return false;
    return true;
}

bool ota_semver_valid(const char *s)
{
    if (!s) return false;
    size_t len = strlen(s);
    if (len == 0 || len > OTA_VERSION_MAX) return false;
    const char *p = s;
    for (int part = 0; part < 3; part++) {
        const char *start = p;
        while (is_digit(*p)) p++;
        if (!ident_ok(start, (size_t)(p - start), true)) return false;
        if (part < 2) {
            if (*p != '.') return false;
            p++;
        }
    }
    if (*p == 0) return true;
    if (*p != '-') return false;
    p++;
    for (;;) {
        const char *start = p;
        while (*p && *p != '.') p++;
        if (!ident_ok(start, (size_t)(p - start), false)) return false;
        if (*p == 0) return true;
        p++;
    }
}

bool ota_hex_valid(const char *s, size_t len)
{
    if (!s || strlen(s) != len) return false;
    for (size_t i = 0; i < len; i++)
        if (!(is_digit(s[i]) || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

static bool get_uint(const cJSON *o, const char *key, double lo, double hi, uint32_t *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v)) return false;
    double d = v->valuedouble;
    if (d < lo || d > hi || d != (double)(uint32_t)d) return false;
    *out = (uint32_t)d;
    return true;
}

static bool get_bool(const cJSON *o, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, key));
}

bool ota_offer_parse(const cJSON *o, uint32_t slot_size, ota_offer_t *out)
{
    memset(out, 0, sizeof *out);
    if (!cJSON_IsObject(o)) return false;
    const char *build = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "build"));
    const char *sha = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "sha256"));
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "url"));
    const char *ver = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, "version"));
    if (!get_uint(o, "attempt", 1, 2147483647.0, &out->attempt)) return false;
    if (!get_uint(o, "size", OTA_HEADER_LEN, slot_size, &out->size)) return false;
    if (!ota_hex_valid(build, OTA_BUILD_HEX) || !ota_hex_valid(sha, OTA_SHA_HEX) || !ota_semver_valid(ver)) return false;
    size_t plen = strlen(OTA_URL_PREFIX);
    if (!url || strncmp(url, OTA_URL_PREFIX, plen) != 0 || strcmp(url + plen, ver) != 0) return false;
    out->is_auto = get_bool(o, "auto");
    out->retry = get_bool(o, "retry");
    strcpy(out->build, build);
    strcpy(out->sha256, sha);
    strcpy(out->version, ver);
    return true;
}

const char *ota_result_name(ota_result_t r)
{
    switch (r) {
    case OTA_RES_OK: return "ok";
    case OTA_RES_FAILED: return "failed";
    case OTA_RES_ROLLED_BACK: return "rolled_back";
    default: return "";
    }
}

static void att_clear(ota_rec_t *r)
{
    r->att_state = OTA_ATT_NONE;
    r->att_attempt = 0;
    r->att_sha[0] = r->att_ver[0] = r->att_build[0] = 0;
}

static void set_last(ota_rec_t *r, ota_result_t res, uint32_t attempt, const char *error, const char *version)
{
    memset(&r->last, 0, sizeof r->last);
    r->last.result = res;
    r->last.attempt = attempt;
    snprintf(r->last.error, sizeof r->last.error, "%s", error ? error : "");
    snprintf(r->last.version, sizeof r->last.version, "%s", version ? version : "");
}

void ota_rec_start(ota_rec_t *r, const ota_offer_t *o)
{
    memset(&r->last, 0, sizeof r->last);
    r->att_state = OTA_ATT_DL;
    r->att_attempt = o->attempt;
    strcpy(r->att_sha, o->sha256);
    strcpy(r->att_ver, o->version);
    strcpy(r->att_build, o->build);
}

void ota_rec_fail(ota_rec_t *r, const char *error, bool mark_bad)
{
    set_last(r, OTA_RES_FAILED, r->att_attempt, error, r->att_ver);
    if (mark_bad && r->att_sha[0]) strcpy(r->bad, r->att_sha);
    att_clear(r);
}

void ota_rec_ready(ota_rec_t *r) { r->att_state = OTA_ATT_READY; }

void ota_rec_rebooting(ota_rec_t *r) { r->att_state = OTA_ATT_BOOT; }

bool ota_rec_refuse(ota_rec_t *r, const ota_offer_t *o)
{
    if (r->last.result == OTA_RES_FAILED && r->last.attempt == o->attempt && strcmp(r->last.error, "refused") == 0)
        return false;
    set_last(r, OTA_RES_FAILED, o->attempt, "refused", o->version);
    return true;
}

bool ota_rec_retry(ota_rec_t *r, const ota_offer_t *o)
{
    if (!o->retry || !r->bad[0] || strcmp(r->bad, o->sha256) != 0) return false;
    r->bad[0] = 0;
    return true;
}

void ota_rec_valid(ota_rec_t *r)
{
    if (r->att_state != OTA_ATT_NONE) set_last(r, OTA_RES_OK, r->att_attempt, NULL, r->att_ver);
    att_clear(r);
}

bool ota_rec_rollback(ota_rec_t *r, const char *error)
{
    ota_rec_t before = *r;
    set_last(r, OTA_RES_ROLLED_BACK, r->att_attempt, error, r->att_ver);
    if (r->att_sha[0]) strcpy(r->bad, r->att_sha);
    return memcmp(&before, r, sizeof before) != 0;
}

static bool same_image(const ota_rec_t *r, const char *build, const char *ver)
{
    if (r->att_build[0] && build && build[0]) return strcmp(r->att_build, build) == 0;
    return ver && strcmp(r->att_ver, ver) == 0;
}

ota_boot_t ota_rec_boot(ota_rec_t *r, const ota_boot_in_t *in)
{
    if (r->att_state == OTA_ATT_DL) {
        ota_rec_fail(r, "interrupted", false);
        return OTA_BOOT_INTERRUPTED;
    }
    if (r->att_state == OTA_ATT_READY) {
        ota_rec_fail(r, "reset_waiting", false);
        return OTA_BOOT_INTERRUPTED;
    }
    if (r->att_state != OTA_ATT_BOOT) return in->pending_verify ? OTA_BOOT_VERIFY : OTA_BOOT_NOTHING;
    if (same_image(r, in->running_build, in->running_ver)) {
        if (in->pending_verify) return OTA_BOOT_VERIFY;
        ota_rec_valid(r);
        return OTA_BOOT_OK;
    }
    if (in->has_invalid && same_image(r, in->invalid_build, in->invalid_ver)) {
        bool kept = r->last.result == OTA_RES_ROLLED_BACK && r->last.attempt == r->att_attempt;
        if (!kept) set_last(r, OTA_RES_ROLLED_BACK, r->att_attempt, "boot", r->att_ver);
        if (r->att_sha[0]) strcpy(r->bad, r->att_sha);
        att_clear(r);
        return OTA_BOOT_ROLLED_BACK;
    }
    ota_rec_fail(r, "interrupted", false);
    return OTA_BOOT_INTERRUPTED;
}

const char *ota_decision_name(ota_decision_t d)
{
    static const char *const N[] = {"go", "not_ready", "pomodoro", "idle_input", "backoff", "bad_image", "attempt_cap"};
    return (unsigned)d < sizeof N / sizeof N[0] ? N[d] : "?";
}

ota_decision_t ota_start_decide(ota_gate_t *g, const ota_rec_t *rec, const ota_offer_t *o, const ota_env_t *e)
{
    if (strcmp(g->sha, o->sha256) != 0) {
        memset(g, 0, sizeof *g);
        strcpy(g->sha, o->sha256);
    }
    if (g->attempt != o->attempt) {
        g->attempt = o->attempt;
        g->started = 0;
    }
    if (o->retry && g->retry_attempt != o->attempt) {
        g->retry_attempt = o->attempt;
        g->fails = 0;
        g->next_ms = 0;
    }
    if (!e->rollback || !e->image_valid || !e->online || !e->paired) return OTA_WAIT_NOT_READY;
    if (rec->bad[0] && strcmp(rec->bad, o->sha256) == 0) return OTA_REFUSE_BAD;
    if (g->started >= OTA_ATTEMPTS_PER_BOOT) return OTA_REFUSE_CAP;
    if (e->pomo_active) return OTA_WAIT_POMODORO;
    if (o->is_auto && e->last_input_ms >= 0 && e->now_ms - e->last_input_ms < OTA_AUTO_IDLE_MS) return OTA_WAIT_INPUT;
    if (e->now_ms < g->next_ms) return OTA_WAIT_BACKOFF;
    return OTA_GO;
}

void ota_gate_started(ota_gate_t *g) { g->started++; }

int ota_backoff_ms(int fails)
{
    if (fails <= 0) return 0;
    if (fails == 1) return 60 * 1000;
    if (fails == 2) return 5 * 60 * 1000;
    return 30 * 60 * 1000;
}

void ota_gate_failed(ota_gate_t *g, int64_t now_ms)
{
    if (g->fails < 3) g->fails++;
    g->next_ms = now_ms + ota_backoff_ms(g->fails);
}

bool ota_resumable(int status) { return status == -1 || (status >= 500 && status <= 599); }

int ota_resume_delay_ms(int resumes_used)
{
    static const int D[OTA_RESUMES_MAX] = {5000, 15000, 30000};
    return resumes_used >= 0 && resumes_used < OTA_RESUMES_MAX ? D[resumes_used] : -1;
}

void ota_http_error(int status, char *out)
{
    if (status < 0 || status > 999) status = 0;
    snprintf(out, OTA_ERROR_MAX + 1, "http_%d", status);
}

ota_resp_t ota_resp_check(int status, uint32_t requested_from, bool has_range, uint32_t range_start,
                          int64_t content_length, uint32_t size, char *error)
{
    if (status == 200) {
        if (content_length != (int64_t)size) {
            strcpy(error, "size");
            return OTA_RESP_FAIL;
        }
        return requested_from ? OTA_RESP_RESTART : OTA_RESP_CONTINUE;
    }
    if (status == 206 && requested_from) {
        if (!has_range || range_start != requested_from || requested_from >= size ||
            content_length != (int64_t)(size - requested_from)) {
            strcpy(error, "size");
            return OTA_RESP_FAIL;
        }
        return OTA_RESP_CONTINUE;
    }
    ota_http_error(status, error);
    return OTA_RESP_FAIL;
}

bool ota_content_range_start(const char *v, uint32_t *start)
{
    if (!v || strncmp(v, "bytes ", 6) != 0) return false;
    const char *p = v + 6;
    if (!is_digit(*p)) return false;
    uint64_t n = 0;
    for (; is_digit(*p); p++) {
        n = n * 10 + (uint64_t)(*p - '0');
        if (n > UINT32_MAX) return false;
    }
    if (*p != '-') return false;
    *start = (uint32_t)n;
    return true;
}

int ota_idf_major(const char *v)
{
    if (!v) return -1;
    if (*v == 'v') v++;
    if (!is_digit(*v)) return -1;
    int n = 0;
    for (; is_digit(*v) && n < 1000; v++) n = n * 10 + (*v - '0');
    return n;
}

static void field(const uint8_t *b, size_t off, char out[33])
{
    memcpy(out, b + off, 32);
    out[32] = 0;
}

static uint32_t le32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }

const char *ota_header_check(const uint8_t *b, size_t n, const char *version, const char *build, const char *boot_idf_ver)
{
    if (!b || n < OTA_HEADER_LEN || b[0] != 0xE9) return "desc";
    if ((b[12] | b[13] << 8) != OTA_CHIP_ESP32S3) return "desc";
    if (le32(b + 32) != 0xABCD5432u) return "desc";
    char s[33];
    field(b, 48, s);
    if (!version || strcmp(s, version) != 0) return "desc";
    field(b, 80, s);
    if (strcmp(s, OTA_PROJECT) != 0) return "desc";
    if (build) {
        char hb[OTA_BUILD_HEX + 1];
        ota_build_hex(b + 176, hb);
        if (strcmp(hb, build) != 0) return "desc";
    }
    field(b, 144, s);
    int img = ota_idf_major(s), boot = ota_idf_major(boot_idf_ver);
    if (img < 0 || (boot >= 0 && img != boot)) return "desc";
    return NULL;
}

int ota_pct(uint32_t written, uint32_t size)
{
    if (!size) return 0;
    uint64_t p = (uint64_t)written * 100 / size;
    return p > 100 ? 100 : (int)p;
}

ota_link_t ota_link_result(bool failed, bool fast_clock)
{
    return failed && !fast_clock ? OTA_LINK_FAIL : OTA_LINK_OK;
}

static bool recent(int64_t age_ms, int64_t limit_ms) { return age_ms >= 0 && age_ms <= limit_ms; }

static bool stacks_ok(const ota_health_in_t *in)
{
    for (int i = 0; i < in->n_tasks && i < OTA_HEALTH_TASKS_MAX; i++) {
        const ota_task_stack_t *t = &in->tasks[i];
        uint32_t need = t->name && strcmp(t->name, "lvgl") == 0 ? OTA_HEALTH_STACK_LVGL : OTA_HEALTH_STACK_MIN;
        if (t->stack_free < need) return false;
    }
    return true;
}

ota_health_t ota_health_check(const ota_health_in_t *in)
{
    if (in->link == OTA_LINK_FAIL) return (ota_health_t){OTA_HEALTH_FAIL, "health_display"};
    const char *why = NULL;
    if (in->link != OTA_LINK_OK) why = "health_display";
    else if (in->frames < OTA_HEALTH_MIN_FRAMES || !recent(in->loop_age_ms, OTA_HEALTH_LOOP_MS)) why = "health_render";
    else if (in->heap_internal_min < OTA_HEALTH_HEAP_MIN || in->heap_largest_min < OTA_HEALTH_LARGEST_MIN)
        why = "health_heap";
    else if (!stacks_ok(in)) why = "health_stack";
    return (ota_health_t){why ? OTA_HEALTH_PENDING : OTA_HEALTH_PASS, why};
}

bool ota_valid_ready(const ota_valid_in_t *in)
{
    return in->uptime_ms >= OTA_VALID_AFTER_MS && in->checkin_ok && in->frame && in->view_ok &&
           in->health == OTA_HEALTH_PASS;
}

bool ota_rollback_due(int64_t uptime_ms, bool pomo_active, int64_t limit_ms)
{
    if (uptime_ms < limit_ms) return false;
    return !pomo_active || uptime_ms >= limit_ms + OTA_ROLLBACK_POMO_MS;
}

const char *ota_verify_rollback(bool due, bool checkin_seen, const ota_health_t *h)
{
    if (h->state == OTA_HEALTH_FAIL) return h->reason;
    if (!due) return NULL;
    if (checkin_seen && h->state == OTA_HEALTH_PENDING && h->reason) return h->reason;
    return "no_checkin";
}

bool ota_verify_needs_health(bool due, ota_link_t link) { return due || link == OTA_LINK_FAIL; }

ota_override_t ota_override_check(bool verifying, const ota_valid_in_t *v)
{
    if (!verifying) return OTA_OVERRIDE_NOT_PENDING;
    if (!v->checkin_ok) return OTA_OVERRIDE_NO_CHECKIN;
    ota_valid_in_t w = *v;
    w.health = OTA_HEALTH_PASS;
    return ota_valid_ready(&w) ? OTA_OVERRIDE_OK : OTA_OVERRIDE_NOT_READY;
}

bool ota_rollback_capable(bool app_rollback, uint32_t bootloader_ver)
{
    return app_rollback && bootloader_ver >= OTA_BOOTLOADER_ROLLBACK_VER;
}

bool ota_backoff_due(const ota_backoff_t *b, int64_t now_ms) { return now_ms >= b->next_ms; }

void ota_backoff_failed(ota_backoff_t *b, int64_t now_ms)
{
    if (b->fails < 3) b->fails++;
    b->next_ms = now_ms + ota_backoff_ms(b->fails);
}

ota_reboot_kind_t ota_reboot_request(ota_reboot_gate_t *g, ota_reboot_kind_t k, bool verifying)
{
    if (!verifying) return k;
    if (k > g->held) g->held = k;
    return OTA_REBOOT_NONE;
}

ota_reboot_kind_t ota_reboot_release(ota_reboot_gate_t *g, bool verifying)
{
    if (verifying) return OTA_REBOOT_NONE;
    ota_reboot_kind_t k = g->held;
    g->held = OTA_REBOOT_NONE;
    return k;
}

const char *ota_image_name(bool ok, uint32_t state)
{
    if (!ok) return "undefined";
    switch (state) {
    case 0x0: return "new";
    case 0x1: return "pending_verify";
    case 0x2: return "valid";
    default: return "undefined";
    }
}

static bool error_ok(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > OTA_ERROR_MAX) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || is_digit(*s) || *s == '_')) return false;
    return true;
}

static bool put(char *out, size_t cap, size_t *len, const char *s)
{
    size_t n = strlen(s);
    if (*len + n >= cap) {
        *len = cap;
        return false;
    }
    memcpy(out + *len, s, n + 1);
    *len += n;
    return true;
}

size_t ota_report_json(const ota_report_t *r, char *out, size_t cap)
{
    if (!cap) return 0;
    size_t len = 0;
    char tmp[160];
    out[0] = 0;
    put(out, cap, &len, "{");
    if (r->image) {
        snprintf(tmp, sizeof tmp, "\"image\":\"%s\",", r->image);
        put(out, cap, &len, tmp);
    }
    const ota_last_t *l = r->last;
    if (l && l->result != OTA_RES_NONE) {
        const char *sep = "";
        put(out, cap, &len, "\"last\":{");
        if (l->attempt) {
            snprintf(tmp, sizeof tmp, "\"attempt\":%" PRIu32, l->attempt);
            put(out, cap, &len, tmp);
            sep = ",";
        }
        if (error_ok(l->error)) {
            snprintf(tmp, sizeof tmp, "%s\"error\":\"%s\"", sep, l->error);
            put(out, cap, &len, tmp);
            sep = ",";
        }
        snprintf(tmp, sizeof tmp, "%s\"result\":\"%s\"", sep, ota_result_name(l->result));
        put(out, cap, &len, tmp);
        if (ota_semver_valid(l->version)) {
            snprintf(tmp, sizeof tmp, ",\"version\":\"%s\"", l->version);
            put(out, cap, &len, tmp);
        }
        put(out, cap, &len, "},");
    }
    if (r->phase) {
        snprintf(tmp, sizeof tmp, "\"phase\":\"%s\",", r->phase);
        put(out, cap, &len, tmp);
    }
    put(out, cap, &len, r->rollback ? "\"rollback\":true" : "\"rollback\":false");
    if (r->slot == 0 || r->slot == 1) {
        snprintf(tmp, sizeof tmp, ",\"slot\":%d", r->slot);
        put(out, cap, &len, tmp);
    }
    if (!put(out, cap, &len, "}")) return 0;
    return len;
}

void ota_build_hex(const uint8_t *elf_sha, char out[OTA_BUILD_HEX + 1])
{
    snprintf(out, OTA_BUILD_HEX + 1, "%02x%02x%02x%02x", elf_sha[0], elf_sha[1], elf_sha[2], elf_sha[3]);
}

static void kv_str(const ota_kv_t *kv, const char *key, char *out, size_t cap)
{
    if (!kv->get_str(kv->ctx, key, out, cap)) out[0] = 0;
}

void ota_rec_load(ota_rec_t *r, const ota_kv_t *kv)
{
    memset(r, 0, sizeof *r);
    uint8_t u8 = 0;
    uint32_t u32 = 0;
    if (kv->get_u8(kv->ctx, "att_state", &u8) && u8 <= OTA_ATT_READY) r->att_state = (ota_att_state_t)u8;
    if (kv->get_u32(kv->ctx, "att_attempt", &u32)) r->att_attempt = u32;
    kv_str(kv, "att_sha", r->att_sha, sizeof r->att_sha);
    kv_str(kv, "att_ver", r->att_ver, sizeof r->att_ver);
    kv_str(kv, "att_build", r->att_build, sizeof r->att_build);
    if (kv->get_u8(kv->ctx, "last_res", &u8) && u8 <= OTA_RES_ROLLED_BACK) r->last.result = (ota_result_t)u8;
    if (kv->get_u32(kv->ctx, "last_att", &u32)) r->last.attempt = u32;
    kv_str(kv, "last_err", r->last.error, sizeof r->last.error);
    kv_str(kv, "last_ver", r->last.version, sizeof r->last.version);
    kv_str(kv, "bad", r->bad, sizeof r->bad);
}

static int kv_put_str(const ota_kv_t *kv, const char *key, const char *v)
{
    return v[0] ? kv->set_str(kv->ctx, key, v) : kv->erase(kv->ctx, key);
}

int ota_rec_save(const ota_rec_t *r, const ota_kv_t *kv)
{
    int err = kv->set_u8(kv->ctx, "att_state", (uint8_t)r->att_state);
    if (!err) err = kv->set_u32(kv->ctx, "att_attempt", r->att_attempt);
    if (!err) err = kv_put_str(kv, "att_sha", r->att_sha);
    if (!err) err = kv_put_str(kv, "att_ver", r->att_ver);
    if (!err) err = kv_put_str(kv, "att_build", r->att_build);
    if (!err) err = kv->set_u8(kv->ctx, "last_res", (uint8_t)r->last.result);
    if (!err) err = kv->set_u32(kv->ctx, "last_att", r->last.attempt);
    if (!err) err = kv_put_str(kv, "last_err", r->last.error);
    if (!err) err = kv_put_str(kv, "last_ver", r->last.version);
    if (!err) err = kv_put_str(kv, "bad", r->bad);
    return err;
}
