#include "view_policy.h"

#include <string.h>

void view_policy_init(view_policy_t *p, bool has_device)
{
    memset(p, 0, sizeof *p);
    p->has_device = has_device;
    p->legacy = !has_device;
    p->why = has_device ? VIEW_WHY_OK : VIEW_WHY_NO_DEVICE;
    p->probe_ms = 0;
}

bool view_policy_try_view(const view_policy_t *p, int64_t now_ms)
{
    if (!p->has_device) return false;
    return !p->legacy || now_ms >= p->probe_ms;
}

static bool go_legacy(view_policy_t *p, view_why_t why, int64_t next_ms)
{
    bool changed = !p->legacy || p->why != why;
    p->legacy = true;
    p->why = why;
    p->probe_ms = next_ms;
    return changed;
}

bool view_policy_result(view_policy_t *p, int status, int64_t now_ms)
{
    if (!p->has_device) return false;
    if (status == 200 || status == 304) {
        bool changed = p->legacy;
        p->legacy = false;
        p->why = VIEW_WHY_OK;
        p->server_errors = 0;
        return changed;
    }
    if (status == 404 || status == 405) return go_legacy(p, VIEW_WHY_OLD_SERVER, now_ms + VIEW_REPROBE_OLD_MS);
    if (status == 401 || status == 403) return go_legacy(p, VIEW_WHY_REJECTED, now_ms + VIEW_REPROBE_MS);
    if (status >= 500) {
        if (++p->server_errors >= VIEW_SERVER_ERRORS) {
            p->server_errors = 0;
            return go_legacy(p, VIEW_WHY_SERVER_ERROR, now_ms + VIEW_REPROBE_MS);
        }
        if (p->legacy) p->probe_ms = now_ms + VIEW_REPROBE_MS;
        return false;
    }
    if (p->legacy) p->probe_ms = now_ms + VIEW_REPROBE_MS;
    return false;
}

void view_policy_device_changed(view_policy_t *p, bool has_device, int64_t now_ms)
{
    if (!has_device) {
        view_policy_init(p, false);
        return;
    }
    p->has_device = true;
    p->server_errors = 0;
    if (p->legacy) {
        if (p->why == VIEW_WHY_NO_DEVICE) p->why = VIEW_WHY_REJECTED;
        p->probe_ms = now_ms;
    }
}

const char *view_why_name(view_why_t w)
{
    switch (w) {
    case VIEW_WHY_OK: return "view";
    case VIEW_WHY_NO_DEVICE: return "no device token";
    case VIEW_WHY_OLD_SERVER: return "server has no view";
    case VIEW_WHY_REJECTED: return "token rejected";
    case VIEW_WHY_SERVER_ERROR: return "server error";
    }
    return "?";
}

view_answer_t view_answer(int status, bool have_view)
{
    if (status == 200) return VIEW_ANS_NEW;
    if (status == 304) return have_view ? VIEW_ANS_SAME : VIEW_ANS_REFETCH;
    return VIEW_ANS_FAILED;
}

void view_etag_clear(view_etag_t *e) { e->tag[0] = 0; }

bool view_etag_set(view_etag_t *e, const char *hdr)
{
    view_etag_clear(e);
    if (!hdr) return false;
    size_t n = strlen(hdr);
    if (n < 2 || n >= VIEW_ETAG_MAX) return false;
    const char *q = hdr;
    if (q[0] == 'W' && q[1] == '/') q += 2;
    size_t qn = strlen(q);
    if (qn < 2 || q[0] != '"' || q[qn - 1] != '"') return false;
    for (size_t i = 1; i + 1 < qn; i++) {
        unsigned char c = (unsigned char)q[i];
        if (c < 0x21 || c == '"' || c == 0x7f) return false;
    }
    memcpy(e->tag, hdr, n + 1);
    return true;
}

const char *view_etag_get(const view_etag_t *e) { return e->tag[0] ? e->tag : NULL; }

bool view_parse_now(const char *hdr, long long *out)
{
    if (!hdr || !*hdr) return false;
    long long v = 0;
    for (const char *p = hdr; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        if (v > 1000000000000LL) return false;
        v = v * 10 + (*p - '0');
    }
    if (v < 1577836800LL) return false;
    *out = v;
    return true;
}
