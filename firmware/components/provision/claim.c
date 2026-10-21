#include "claim.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

bool claim_b64url(const uint8_t *in, size_t n, char *out, size_t cap)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t need = n / 3 * 4 + (n % 3 ? n % 3 + 1 : 0);
    if (!out || need >= cap) return false;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        size_t k = n - i >= 3 ? 4 : n - i + 1;
        for (size_t j = 0; j < k; j++) out[o++] = A[(v >> (18 - 6 * j)) & 63];
    }
    out[o] = 0;
    return true;
}

static bool json_safe(const char *s)
{
    if (!s) return false;
    for (; *s; s++)
        if (*s < 0x20 || *s > 0x7e || *s == '"' || *s == '\\') return false;
    return true;
}

static size_t fit(int n, size_t cap) { return n > 0 && (size_t)n < cap ? (size_t)n : 0; }

size_t claim_open_body(const char *hw_id, const char *fw, const char *key, char *out, size_t cap)
{
    if (!json_safe(hw_id) || !json_safe(fw) || !json_safe(key)) return 0;
    return fit(snprintf(out, cap, "{\"hw_id\":\"%s\",\"fw\":\"%s\",\"claim_key\":\"%s\"}", hw_id, fw, key), cap);
}

size_t claim_poll_body(const char *claim_id, const char *key, char *out, size_t cap)
{
    if (!json_safe(claim_id) || !json_safe(key)) return 0;
    return fit(snprintf(out, cap, "{\"claim_id\":\"%s\",\"claim_key\":\"%s\"}", claim_id, key), cap);
}

bool claim_server_pairs(const char *json)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    const cJSON *f = cJSON_GetObjectItemCaseSensitive(root, "features"), *e;
    bool yes = false;
    if (cJSON_IsArray(f)) {
        cJSON_ArrayForEach(e, f) yes |= cJSON_IsString(e) && strcmp(e->valuestring, CLAIM_FEATURE) == 0;
    }
    cJSON_Delete(root);
    return yes;
}

static bool id_valid(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > CLAIM_ID_MAX) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '-')) return false;
    return true;
}

static bool code_valid(const char *s)
{
    if (strlen(s) != CLAIM_CODE_LEN) return false;
    for (; *s; s++)
        if (*s < '0' || *s > '9') return false;
    return true;
}

static int int_in(const cJSON *root, const char *key, int lo, int hi, int dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(v)) return dflt;
    double d = v->valuedouble;
    return d < lo ? lo : d > hi ? hi : (int)d;
}

bool claim_reply_parse(const char *json, claim_reply_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "claim_id"));
    const char *code = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "code"));
    bool ok = cJSON_IsObject(root) && id && code && id_valid(id) && code_valid(code);
    if (ok) {
        strcpy(out->claim_id, id);
        strcpy(out->code, code);
        out->expires_in_s = int_in(root, "expires_in_s", 1, CLAIM_EXPIRES_S_MAX, CLAIM_EXPIRES_S_DEFAULT);
        out->poll_s = int_in(root, "poll_s", CLAIM_POLL_S_MIN, CLAIM_POLL_S_MAX, CLAIM_POLL_S_DEFAULT);
    }
    cJSON_Delete(root);
    return ok;
}

static bool printable(const char *s, size_t max, bool space)
{
    size_t n = strlen(s);
    if (n == 0 || n > max) return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7f || (!space && c == ' ')) return false;
    }
    return true;
}

static void wipe_strings(cJSON *n)
{
    for (; n; n = n->next) {
        if (cJSON_IsString(n) && n->valuestring) memset(n->valuestring, 0, strlen(n->valuestring));
        wipe_strings(n->child);
    }
}

static claim_poll_t poll_parse(const char *json, claim_approved_t *a, int *expires_in_s)
{
    memset(a, 0, sizeof *a);
    *expires_in_s = 0;
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    const char *state = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "state"));
    claim_poll_t r = CLAIM_POLL_FAILED;
    if (state && strcmp(state, "pending") == 0) {
        r = CLAIM_POLL_PENDING;
        *expires_in_s = int_in(root, "expires_in_s", 0, CLAIM_EXPIRES_S_MAX + 1, 0);
        if (*expires_in_s > CLAIM_EXPIRES_S_MAX) *expires_in_s = 0;
    } else if (state && strcmp(state, "approved") == 0) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "device_id"));
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "name"));
        const char *t = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "token"));
        if (id && t && printable(id, CFG_DEV_ID_MAX, false) && printable(t, CFG_TOKEN_MAX, false)) {
            strcpy(a->device_id, id);
            strcpy(a->token, t);
            if (name && printable(name, CFG_NAME_MAX, true)) strcpy(a->name, name);
            r = CLAIM_POLL_APPROVED;
        }
    }
    wipe_strings(root);
    cJSON_Delete(root);
    return r;
}

claim_poll_t claim_poll_parse(const char *json, claim_approved_t *approved)
{
    int expires;
    claim_poll_t r = poll_parse(json, approved, &expires);
    if (r != CLAIM_POLL_APPROVED) memset(approved, 0, sizeof *approved);
    return r;
}

void claim_init(claim_t *c)
{
    memset(c, 0, sizeof *c);
    c->phase = CLAIM_IDLE;
}

static void drop_key(claim_t *c);

void claim_reset(claim_t *c)
{
    drop_key(c);
    claim_init(c);
}

void claim_want(claim_t *c, bool want, int64_t now_ms)
{
    if (!want) {
        if (c->phase != CLAIM_IDLE || c->has_key) claim_reset(c);
        return;
    }
    if (c->phase != CLAIM_IDLE) return;
    c->phase = CLAIM_PROBE;
    c->due_ms = now_ms;
    c->ignored = 0;
    c->backoff_ms = 0;
}

static void wait(claim_t *c, int64_t now_ms, int ms, claim_phase_t then)
{
    c->phase = CLAIM_WAIT;
    c->after_wait = then;
    c->due_ms = now_ms + ms;
}

static int backoff(claim_t *c)
{
    c->backoff_ms = c->backoff_ms ? c->backoff_ms * 2 : CLAIM_BACKOFF_MIN_MS;
    if (c->backoff_ms > CLAIM_BACKOFF_MAX_MS) c->backoff_ms = CLAIM_BACKOFF_MAX_MS;
    return c->backoff_ms;
}

static void ended(claim_t *c, int64_t now_ms)
{
    drop_key(c);
    c->ignored++;
    c->id[0] = c->code[0] = 0;
    c->phase = c->ignored >= CLAIM_IGNORED_MAX ? CLAIM_INPUT : CLAIM_OPEN;
    c->due_ms = now_ms;
}

void claim_input(claim_t *c, int64_t now_ms)
{
    if (c->phase != CLAIM_INPUT) return;
    c->phase = CLAIM_OPEN;
    c->ignored = 0;
    c->due_ms = now_ms;
}

claim_act_t claim_next(claim_t *c, int64_t now_ms)
{
    if (c->phase == CLAIM_WAIT && now_ms >= c->due_ms) c->phase = c->after_wait;
    if (c->phase == CLAIM_SHOW && now_ms >= c->expires_ms) ended(c, now_ms);
    if (now_ms < c->due_ms) return CLAIM_ACT_NONE;
    switch (c->phase) {
    case CLAIM_PROBE: return CLAIM_ACT_PROBE;
    case CLAIM_OPEN: return CLAIM_ACT_OPEN;
    case CLAIM_SHOW: return CLAIM_ACT_POLL;
    default: return CLAIM_ACT_NONE;
    }
}

bool claim_needs_key(const claim_t *c) { return !c->has_key; }

void claim_set_key(claim_t *c, const uint8_t rnd[CLAIM_KEY_BYTES])
{
    c->has_key = claim_b64url(rnd, CLAIM_KEY_BYTES, c->key, sizeof c->key);
}

static bool old_server(int status) { return status == 401 || status == 403 || status == 404 || status == 405; }

void claim_probed(claim_t *c, int status, const char *body, int64_t now_ms)
{
    if (c->phase != CLAIM_PROBE) return;
    if (status == 200 && claim_server_pairs(body)) {
        c->backoff_ms = 0;
        c->phase = CLAIM_OPEN;
        c->due_ms = now_ms;
    } else if (status == 200 || old_server(status)) {
        wait(c, now_ms, CLAIM_OLD_SERVER_MS, CLAIM_PROBE);
    } else {
        wait(c, now_ms, backoff(c), CLAIM_PROBE);
    }
}

static void drop_key(claim_t *c)
{
    volatile char *k = c->key;
    for (size_t i = 0; i < sizeof c->key; i++) k[i] = 0;
    c->has_key = false;
}

int claim_retry_after_s(const char *hdr)
{
    if (!hdr || !*hdr) return -1;
    long v = 0;
    for (const char *p = hdr; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        v = v * 10 + (*p - '0');
        if (v > CLAIM_RETRY_AFTER_S_MAX) v = CLAIM_RETRY_AFTER_S_MAX;
    }
    return v < 1 ? 1 : (int)v;
}

void claim_opened(claim_t *c, int status, const char *body, int retry_after_s, int64_t now_ms)
{
    if (c->phase != CLAIM_OPEN) return;
    claim_reply_t r;
    if ((status == 200 || status == 201) && c->has_key && claim_reply_parse(body, &r)) {
        strcpy(c->id, r.claim_id);
        strcpy(c->code, r.code);
        c->expires_ms = c->deadline_ms = now_ms + (int64_t)r.expires_in_s * 1000;
        c->poll_ms = r.poll_s * 1000;
        c->due_ms = now_ms + c->poll_ms;
        c->backoff_ms = 0;
        c->phase = CLAIM_SHOW;
    } else if (old_server(status)) {
        wait(c, now_ms, CLAIM_OLD_SERVER_MS, CLAIM_PROBE);
    } else if (status == 409 || status == 429) {
        drop_key(c);
        int ms = retry_after_s > 0 ? retry_after_s * 1000 : status == 409 ? CLAIM_LIVE_MS : backoff(c);
        wait(c, now_ms, ms, CLAIM_OPEN);
    } else {
        wait(c, now_ms, backoff(c), CLAIM_OPEN);
    }
}

claim_poll_t claim_polled(claim_t *c, int status, const char *body, int retry_after_s, int64_t now_ms, claim_approved_t *approved)
{
    memset(approved, 0, sizeof *approved);
    if (c->phase != CLAIM_SHOW) return CLAIM_POLL_FAILED;
    if (old_server(status)) {
        ended(c, now_ms);
        return CLAIM_POLL_GONE;
    }
    c->due_ms = now_ms + (status != 429 ? c->poll_ms : retry_after_s > 0 ? retry_after_s * 1000 : backoff(c));
    if (status != 200 && status != 202) return CLAIM_POLL_FAILED;
    int expires;
    claim_poll_t r = poll_parse(body, approved, &expires);
    if (r != CLAIM_POLL_FAILED) c->backoff_ms = 0;
    if (r == CLAIM_POLL_PENDING && expires > 0) {
        int64_t at = now_ms + (int64_t)expires * 1000;
        c->expires_ms = at < c->deadline_ms ? at : c->deadline_ms;
    }
    if (r != CLAIM_POLL_APPROVED) memset(approved, 0, sizeof *approved);
    return r;
}

void claim_stored(claim_t *c, bool ok)
{
    if (ok) claim_reset(c);
}

bool claim_code(const claim_t *c, int64_t now_ms, char out[CLAIM_CODE_LEN + 1])
{
    if (c->phase != CLAIM_SHOW || now_ms >= c->expires_ms) return false;
    memcpy(out, c->code, CLAIM_CODE_LEN + 1);
    return true;
}
