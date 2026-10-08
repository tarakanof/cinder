#include "cinder_line.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "cfg.h"

const char *const CL_INPUT_NAMES[CL_INPUT_COUNT] = {"turn", "push", "long", "touch", "page", "swipe_up", "swipe_down"};

static bool printable(const char *v, bool allow_space)
{
    for (; *v; v++) {
        unsigned char c = (unsigned char)*v;
        if (c < 0x20 || c == 0x7f || (!allow_space && c == ' ')) return false;
    }
    return true;
}

static cl_err_t get_str(const cJSON *root, const char *key, bool required, char *dst, size_t cap, cl_err_t too_long)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    dst[0] = 0;
    if (!v || cJSON_IsNull(v)) return required ? CL_E_BAD_JSON : CL_OK;
    if (!cJSON_IsString(v)) return CL_E_BAD_JSON;
    if (strlen(v->valuestring) >= cap) return too_long;
    strcpy(dst, v->valuestring);
    return CL_OK;
}

static cl_err_t parse_set_ember(const cJSON *root, cl_req_t *out)
{
    cl_err_t e;
    if ((e = get_str(root, "url", true, out->url, sizeof out->url, CL_E_BAD_URL)) != CL_OK) return e;
    if (!cfg_url_valid(out->url)) return CL_E_BAD_URL;
    cfg_url_normalize(out->url);
    if ((e = get_str(root, "token", true, out->token, sizeof out->token, CL_E_TOO_LONG)) != CL_OK) return e;
    if (!out->token[0] || !printable(out->token, false)) return CL_E_BAD_VALUE;
    if ((e = get_str(root, "device_id", true, out->device_id, sizeof out->device_id, CL_E_TOO_LONG)) != CL_OK) return e;
    if (!out->device_id[0]) return CL_E_BAD_VALUE;
    for (const char *p = out->device_id; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return CL_E_BAD_VALUE;
    }
    if ((e = get_str(root, "name", false, out->name, sizeof out->name, CL_E_TOO_LONG)) != CL_OK) return e;
    if (!printable(out->name, true)) return CL_E_BAD_VALUE;
    return CL_OK;
}

static cl_err_t get_opt_int(const cJSON *root, const char *key, int lo, int hi, int *dst)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    *dst = -1;
    if (!v || cJSON_IsNull(v)) return CL_OK;
    if (!cJSON_IsNumber(v) || v->valuedouble != floor(v->valuedouble)) return CL_E_BAD_JSON;
    double d = v->valuedouble;
    if (d != 0 && (d < lo || d > hi)) return CL_E_BAD_VALUE;
    *dst = (int)d;
    return CL_OK;
}

cl_err_t cl_parse(const char *json, cl_req_t *out)
{
    memset(out, 0, sizeof *out);
    if (!json || strlen(json) > CL_LINE_MAX) return CL_E_TOO_LONG;
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return CL_E_BAD_JSON;
    }
    cl_err_t e = CL_OK;
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsNumber(id) && id->valuedouble == floor(id->valuedouble) && fabs(id->valuedouble) < 1e15) {
        out->has_id = true;
        out->id = (long)id->valuedouble;
    } else if (id && !cJSON_IsNull(id)) {
        e = CL_E_BAD_JSON;
    }
    const cJSON *op = cJSON_GetObjectItemCaseSensitive(root, "op");
    const char *o = cJSON_GetStringValue(op);
    if (e != CL_OK) {
    } else if (!o) {
        e = CL_E_BAD_JSON;
    } else if (strcmp(o, "info") == 0) {
        out->op = CL_OP_INFO;
    } else if (strcmp(o, "status") == 0) {
        out->op = CL_OP_STATUS;
    } else if (strcmp(o, "reboot") == 0) {
        out->op = CL_OP_REBOOT;
    } else if (strcmp(o, "set_ember") == 0) {
        out->op = CL_OP_SET_EMBER;
        e = parse_set_ember(root, out);
    } else if (strcmp(o, "reset") == 0) {
        out->op = CL_OP_RESET;
        const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "scope"));
        if (!s) e = CL_E_BAD_JSON;
        else if (strcmp(s, "factory") == 0) out->scope = CL_SCOPE_FACTORY;
        else if (strcmp(s, "ember") == 0) out->scope = CL_SCOPE_EMBER;
        else if (strcmp(s, "wifi") == 0) out->scope = CL_SCOPE_WIFI;
        else e = CL_E_BAD_VALUE;
    } else if (strcmp(o, "snapshot") == 0) {
        out->op = CL_OP_SNAPSHOT;
    } else if (strcmp(o, "chase") == 0) {
        out->op = CL_OP_CHASE;
        const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "style");
        if (st && !cJSON_IsString(st)) e = CL_E_BAD_JSON;
        else if (st && strcmp(st->valuestring, "half") == 0) out->chase_style = 1;
        else if (st && strcmp(st->valuestring, "full") != 0) e = CL_E_BAD_VALUE;
        if (e == CL_OK) e = get_opt_int(root, "fps", CL_CHASE_FPS_MIN, CL_CHASE_FPS_MAX, &out->chase_fps);
        if (e == CL_OK) e = get_opt_int(root, "laps", 1, CL_CHASE_LAPS_MAX, &out->chase_laps);
        if (out->chase_fps < 0) out->chase_fps = 0;
        if (out->chase_laps < 0) out->chase_laps = 0;
    } else if (strcmp(o, "input") == 0) {
        out->op = CL_OP_INPUT;
        const char *in = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "input"));
        e = CL_E_BAD_VALUE;
        for (int i = 0; in && i < CL_INPUT_COUNT; i++)
            if (strcmp(in, CL_INPUT_NAMES[i]) == 0) {
                out->input = (cl_input_t)i;
                e = CL_OK;
            }
        if (!in) e = CL_E_BAD_JSON;
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(root, "n");
        out->input_n = 1;
        if (e == CL_OK && n) {
            if (!cJSON_IsNumber(n) || n->valuedouble != floor(n->valuedouble)) e = CL_E_BAD_JSON;
            else if (out->input == CL_INPUT_TURN || out->input == CL_INPUT_PAGE ? (n->valuedouble == 0 || fabs(n->valuedouble) > 24)
                                                 : (n->valuedouble < 1 || n->valuedouble > 3))
                e = CL_E_BAD_VALUE;
            else out->input_n = (int)n->valuedouble;
        }
    } else if (strcmp(o, "ota_fault") == 0) {
        out->op = CL_OP_OTA_FAULT;
        const char *f = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "fault"));
        if (!f) e = CL_E_BAD_JSON;
        else if (strcmp(f, "net") == 0 || strcmp(f, "sha") == 0) snprintf(out->fault, sizeof out->fault, "%s", f);
        else e = CL_E_BAD_VALUE;
    } else if (strcmp(o, "ota_valid") == 0) {
        out->op = CL_OP_OTA_VALID;
    } else if (strcmp(o, "diag_override") == 0) {
        out->op = CL_OP_DIAG_OVERRIDE;
        e = get_opt_int(root, "stats_s", CL_DIAG_STATS_S_MIN, CL_DIAG_STATS_S_MAX, &out->stats_s);
        if (e == CL_OK) e = get_opt_int(root, "live_s", CL_DIAG_LIVE_S_MIN, CL_DIAG_LIVE_S_MAX, &out->live_s);
    } else {
        e = CL_E_UNKNOWN_OP;
    }
    cJSON_Delete(root);
    if (e != CL_OK) {
        memset(out->token, 0, sizeof out->token);
    }
    return e;
}

const char *cl_err_name(cl_err_t e)
{
    switch (e) {
    case CL_OK: return "ok";
    case CL_E_BAD_JSON: return "bad_json";
    case CL_E_UNKNOWN_OP: return "unknown_op";
    case CL_E_BAD_URL: return "bad_url";
    case CL_E_TOO_LONG: return "too_long";
    case CL_E_BAD_VALUE: return "bad_value";
    case CL_E_BUSY: return "busy";
    case CL_E_FAILED: return "failed";
    case CL_E_NOT_WORKING: return "not_working";
    case CL_E_NOT_PENDING: return "not_pending";
    case CL_E_NO_CHECKIN: return "no_checkin";
    case CL_E_NOT_READY: return "not_ready";
    }
    return "bad_json";
}

typedef struct {
    char *out;
    size_t cap, len;
    bool fail;
    bool first;
} jw_t;

static void jw_raw(jw_t *w, const char *s, size_t n)
{
    if (w->fail || w->len + n >= w->cap) {
        w->fail = true;
        return;
    }
    memcpy(w->out + w->len, s, n);
    w->len += n;
    w->out[w->len] = 0;
}

static void jw_puts(jw_t *w, const char *s) { jw_raw(w, s, strlen(s)); }

static void jw_printf(jw_t *w, const char *fmt, ...)
{
    char tmp[32];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof tmp) w->fail = true;
    else jw_raw(w, tmp, (size_t)n);
}

static void jw_string(jw_t *w, const char *s)
{
    jw_puts(w, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': jw_puts(w, "\\\""); break;
        case '\\': jw_puts(w, "\\\\"); break;
        case '\n': jw_puts(w, "\\n"); break;
        case '\r': jw_puts(w, "\\r"); break;
        case '\t': jw_puts(w, "\\t"); break;
        default:
            if (*p < 0x20) jw_printf(w, "\\u%04x", *p);
            else jw_raw(w, (const char *)p, 1);
        }
    }
    jw_puts(w, "\"");
}

static void jw_key(jw_t *w, const char *k)
{
    if (!w->first) jw_puts(w, ",");
    w->first = false;
    jw_string(w, k);
    jw_puts(w, ":");
}

static void jw_open(jw_t *w, const char *key)
{
    if (key) jw_key(w, key);
    jw_puts(w, "{");
    w->first = true;
}

static void jw_close(jw_t *w)
{
    jw_puts(w, "}");
    w->first = false;
}

static void jw_kstr(jw_t *w, const char *k, const char *v)
{
    jw_key(w, k);
    if (v) jw_string(w, v);
    else jw_puts(w, "null");
}

static void jw_kint(jw_t *w, const char *k, long v)
{
    jw_key(w, k);
    jw_printf(w, "%ld", v);
}

static void jw_kbool(jw_t *w, const char *k, bool v)
{
    jw_key(w, k);
    jw_puts(w, v ? "true" : "false");
}

static void jw_kid(jw_t *w, bool has_id, long id)
{
    if (has_id) jw_kint(w, "id", id);
    else jw_kstr(w, "id", NULL);
}

static jw_t jw_begin(char *out, size_t cap)
{
    jw_t w = {.out = out, .cap = cap};
    if (cap) out[0] = 0;
    jw_puts(&w, CL_PREFIX);
    jw_open(&w, NULL);
    return w;
}

static size_t jw_end(jw_t *w)
{
    jw_close(w);
    jw_puts(w, "\n");
    return w->fail ? 0 : w->len;
}

size_t cl_reply_ok(bool has_id, long id, char *out, size_t cap)
{
    jw_t w = jw_begin(out, cap);
    jw_kid(&w, has_id, id);
    jw_kbool(&w, "ok", true);
    return jw_end(&w);
}

size_t cl_reply_error(bool has_id, long id, cl_err_t e, char *out, size_t cap)
{
    jw_t w = jw_begin(out, cap);
    jw_kstr(&w, "error", cl_err_name(e));
    jw_kid(&w, has_id, id);
    jw_kbool(&w, "ok", false);
    return jw_end(&w);
}

size_t cl_reply_info(long id, const cl_info_t *in, char *out, size_t cap)
{
    jw_t w = jw_begin(out, cap);
    jw_kstr(&w, "device_id", in->device_id && in->device_id[0] ? in->device_id : NULL);
    jw_open(&w, "ember");
    jw_kbool(&w, "configured", in->ember_configured);
    jw_close(&w);
    jw_kstr(&w, "fw", in->fw);
    jw_kstr(&w, "hw_id", in->hw_id);
    jw_kint(&w, "id", id);
    jw_kbool(&w, "ok", true);
    jw_open(&w, "wifi");
    jw_kbool(&w, "configured", in->wifi_configured);
    jw_close(&w);
    return jw_end(&w);
}

size_t cl_reply_status(long id, const cl_status_t *st, char *out, size_t cap)
{
    jw_t w = jw_begin(out, cap);
    if (st->diag_override) {
        jw_open(&w, "diag");
        jw_kint(&w, "live_s", st->diag_live_s);
        jw_kbool(&w, "override", true);
        jw_kint(&w, "stats_s", st->diag_stats_s);
        jw_close(&w);
    }
    jw_open(&w, "ember");
    if (st->config_version >= 0) jw_kint(&w, "config_version", st->config_version);
    if (st->last_checkin_s >= 0) jw_kint(&w, "last_checkin_s", st->last_checkin_s);
    jw_kstr(&w, "state", st->ember_state);
    jw_close(&w);
    jw_open(&w, "heap");
    jw_kint(&w, "internal_free", (long)st->internal_free);
    jw_kint(&w, "internal_largest", (long)st->internal_largest);
    jw_kint(&w, "psram_free", (long)st->psram_free);
    jw_close(&w);
    jw_kint(&w, "id", id);
    jw_kbool(&w, "ok", true);
    jw_open(&w, "wifi");
    if (st->ip) jw_kstr(&w, "ip", st->ip);
    if (st->has_rssi) jw_kint(&w, "rssi", st->rssi);
    if (st->ssid) jw_kstr(&w, "ssid", st->ssid);
    jw_kstr(&w, "state", st->wifi_state);
    jw_close(&w);
    return jw_end(&w);
}

size_t cl_reply_diag(long id, const cl_diag_t *d, char *out, size_t cap)
{
    jw_t w = jw_begin(out, cap);
    jw_kint(&w, "checkin_s", d->checkin_s);
    jw_kstr(&w, "diagnostics", d->diagnostics);
    jw_kint(&w, "id", id);
    jw_kint(&w, "live_s", d->live_s);
    jw_kbool(&w, "ok", true);
    jw_kbool(&w, "override", d->override);
    jw_kint(&w, "requests", (long)d->requests);
    jw_kint(&w, "rx_bytes", (long)d->rx_bytes);
    jw_kint(&w, "stats_s", d->stats_s);
    jw_kint(&w, "tx_bytes", (long)d->tx_bytes);
    jw_key(&w, "uptime_ms");
    jw_printf(&w, "%lld", (long long)d->uptime_ms);
    return jw_end(&w);
}

size_t cl_event_boot(const char *fw, bool provisioned, char *out, size_t cap)
{
    jw_t w = jw_begin(out, cap);
    jw_kstr(&w, "ev", "boot");
    jw_kstr(&w, "fw", fw);
    jw_kbool(&w, "provisioned", provisioned);
    return jw_end(&w);
}

size_t cl_event_wifi(const char *state, char *out, size_t cap)
{
    jw_t w = jw_begin(out, cap);
    jw_kstr(&w, "ev", "wifi");
    jw_kstr(&w, "state", state);
    return jw_end(&w);
}

size_t cl_event_ember(const char *state, char *out, size_t cap)
{
    jw_t w = jw_begin(out, cap);
    jw_kstr(&w, "ev", "ember");
    jw_kstr(&w, "state", state);
    return jw_end(&w);
}
