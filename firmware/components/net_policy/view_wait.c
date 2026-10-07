#include "view_wait.h"

int view_wait_cap(const char *hdr)
{
    if (!hdr || !*hdr) return 0;
    int v = 0;
    for (const char *p = hdr; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        v = v * 10 + (*p - '0');
        if (v > 3600) return 0;
    }
    return v;
}

int view_wait_s(int cap_s, bool have_etag, int64_t until_checkin_ms, bool failing)
{
    if (cap_s <= 0 || !have_etag || failing) return 0;
    int64_t w = cap_s < VIEW_WAIT_MAX_S ? cap_s : VIEW_WAIT_MAX_S;
    int64_t left = until_checkin_ms / 1000;
    if (left < w) w = left;
    return w < VIEW_WAIT_MIN_S ? 0 : (int)w;
}

int view_wait_budget_ms(int wait_s) { return (wait_s > 0 ? wait_s : 0) * 1000 + VIEW_WAIT_SLACK_MS; }

int view_rearm_ms(int status, int waited_s, int64_t elapsed_ms, int poll_ms)
{
    if (waited_s <= 0) return poll_ms;
    if (status == 200) return VIEW_REARM_MIN_MS;
    if (status == 304 && elapsed_ms >= VIEW_WAIT_FAST_MS) return 0;
    return poll_ms;
}

bool view_wait_clock_sample(int waited_s, int64_t elapsed_ms) { return waited_s <= 0 || elapsed_ms < VIEW_WAIT_FAST_MS; }
