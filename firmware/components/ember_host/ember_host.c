#include "ember_host.h"

#include <stdio.h>
#include <string.h>

int ember_host_state_priority(const char *state)
{
    if (!state) return 4;
    if (strcmp(state, "waiting") == 0) return 0;
    if (strcmp(state, "error") == 0) return 1;
    if (strcmp(state, "running") == 0) return 2;
    if (strcmp(state, "done") == 0) return 3;
    return 4;
}

static bool digits(const char **p, int n, int *out)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        char c = (*p)[i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    *p += n;
    *out = v;
    return true;
}

static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool ember_host_parse_time(const char *s, int64_t *sec, int32_t *nsec)
{
    if (!s) return false;
    const char *p = s;
    int y, mo, d, h, mi, se;
    if (!digits(&p, 4, &y) || *p++ != '-' || !digits(&p, 2, &mo) || *p++ != '-' || !digits(&p, 2, &d)) return false;
    if (*p != 'T' && *p != 't' && *p != ' ') return false;
    p++;
    if (!digits(&p, 2, &h) || *p++ != ':' || !digits(&p, 2, &mi) || *p++ != ':' || !digits(&p, 2, &se)) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return false;
    int32_t ns = 0;
    if (*p == '.') {
        p++;
        int n = 0;
        while (*p >= '0' && *p <= '9') {
            if (n < 9) { ns = ns * 10 + (*p - '0'); n++; }
            p++;
        }
        if (n == 0) return false;
        for (; n < 9; n++) ns *= 10;
    }
    int off = 0;
    if (*p == 'Z' || *p == 'z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        int sign = *p++ == '-' ? -1 : 1, oh, om;
        if (!digits(&p, 2, &oh) || *p++ != ':' || !digits(&p, 2, &om)) return false;
        off = sign * (oh * 3600 + om * 60);
    } else {
        return false;
    }
    if (*p) return false;
    *sec = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se - off;
    *nsec = ns;
    return true;
}

int ember_host_pick_winning(const ember_host_session_t *s, int n)
{
    int win = -1, best = 4;
    int64_t win_sec = 0;
    int32_t win_ns = 0;
    bool win_time = false;
    for (int i = 0; i < n; i++) {
        int prio = ember_host_state_priority(s[i].state);
        if (prio >= 4) continue;
        int64_t sec = 0;
        int32_t ns = 0;
        bool has_time = ember_host_parse_time(s[i].updated_at, &sec, &ns);
        bool newer = has_time && (!win_time || sec > win_sec || (sec == win_sec && ns > win_ns));
        if (win < 0 || prio < best || (prio == best && newer)) {
            win = i;
            best = prio;
            win_sec = sec;
            win_ns = ns;
            win_time = has_time;
        }
    }
    return win;
}

static size_t clean(const char *src, size_t max, char *out, size_t cap)
{
    size_t k = 0;
    for (const char *p = src ? src : ""; *p && k + 1 < cap && k < max; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c < 0x20 || c > 0x7e) continue;
        out[k++] = c;
    }
    out[k] = 0;
    return k;
}

static void strlcat_(char *dst, const char *src, size_t cap)
{
    size_t n = strlen(dst);
    while (*src && n + 1 < cap) dst[n++] = *src++;
    dst[n] = 0;
}

void ember_host_label(const char *render_source, bool has_render_source, const ember_host_session_t *s, int n,
                      char *out, size_t cap)
{
    const char *src = render_source;
    if (!has_render_source) {
        int w = ember_host_pick_winning(s, n);
        src = w >= 0 ? s[w].source : NULL;
        int wp = w >= 0 ? ember_host_state_priority(s[w].state) : -1;
        for (int i = 0; w >= 0 && i < n && src; i++) {
            if (ember_host_state_priority(s[i].state) != wp) continue;
            if (strcmp(s[i].source ? s[i].source : "", src) != 0) src = NULL;
        }
    }
    clean(src, EMBER_HOST_MAX, out, cap);
}

void ember_host_lead_label(const char *source, const char *lead, int hosts, char *out, size_t cap)
{
    const char *name = lead && *lead ? lead : source;
    char suffix[16] = "";
    if (hosts > 1) snprintf(suffix, sizeof suffix, " +%d", hosts - 1);
    size_t room = EMBER_HOST_MAX > strlen(suffix) ? EMBER_HOST_MAX - strlen(suffix) : 0;
    size_t k = clean(name, room, out, cap);
    if (k == 0) return;
    strlcat_(out, suffix, cap);
}

int32_t ember_host_color(const char *hex)
{
    if (!hex || hex[0] != '#' || strlen(hex) != 7) return -1;
    int32_t v = 0;
    for (int i = 1; i < 7; i++) {
        char c = hex[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        v = v * 16 + d;
    }
    return v;
}

ember_tool_t ember_host_tool(const char *s)
{
    if (!s) return EMBER_TOOL_NONE;
    if (strcmp(s, "claude") == 0) return EMBER_TOOL_CLAUDE;
    if (strcmp(s, "codex") == 0) return EMBER_TOOL_CODEX;
    if (strcmp(s, "t3") == 0) return EMBER_TOOL_T3;
    return EMBER_TOOL_NONE;
}
