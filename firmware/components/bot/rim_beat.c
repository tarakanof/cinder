#include "rim_beat.h"

#include <math.h>

#include "bot_behavior.h"

void rim_beat_init(rim_beat_t *r) { *r = (rim_beat_t){.job = -1, .shown = -1, .settle_t = -INFINITY}; }

bool rim_beat_want(rim_beat_t *r, int want)
{
    if (want == r->job) return false;
    r->job = want;
    r->own = r->settle_due = false;
    r->settle_t = -INFINITY;
    return true;
}

void rim_beat_step(rim_beat_t *r, int want, int show, bool ready, bool m1, bool p1, bool still, double t, rim_beat_out_t *out)
{
    *out = (rim_beat_out_t){0};
    if (rim_beat_want(r, want)) {
        out->start = true;
        ready = m1 = p1 = false;
    }
    if (!r->own && show == r->job && ready) {
        r->settle_due = r->shown >= 0 && bot_rim_mood(r->shown) != bot_rim_mood(r->job);
        r->shown = r->job;
        r->own = true;
        r->swap_t = t;
        out->swap = true;
    }
    if (r->settle_due && m1 && p1) {
        r->settle_due = false;
        if (t - r->swap_t <= RIM_SETTLE_LATE_S) r->settle_t = t;
    }
    double s = t - r->settle_t;
    if (still && s >= 0 && s < RIM_SETTLE_S) out->settle = s < RIM_SETTLE_M1_S ? -1 : 1;
    out->own = r->own;
}

bool rim_beat_ready(const rim_beat_t *r, int mood, bool ready)
{
    return r->job >= 0 && (int)bot_rim_mood(r->job) == mood && (r->own || ready || r->shown == r->job);
}
