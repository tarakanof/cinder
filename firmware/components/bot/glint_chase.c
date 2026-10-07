#include "glint_chase.h"

#include <math.h>

static uint32_t next(glint_chase_t *c)
{
    uint32_t x = c->rng ? c->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return c->rng = x;
}

static double uniform(glint_chase_t *c, double lo, double hi)
{
    return lo + (hi - lo) * (double)(next(c) >> 8) / (double)(1u << 24);
}

void gc_init(glint_chase_t *c, uint32_t seed)
{
    *c = (glint_chase_t){.rng = seed, .phase = GC_IDLE, .last_t = -1};
    c->wait_s = uniform(c, GC_WAIT_MIN_S, GC_WAIT_MAX_S);
}

bool gc_start_now(glint_chase_t *c, int style, int laps)
{
    if (c->phase != GC_IDLE) return false;
    c->work_s = c->wait_s;
    c->forced = style + 1;
    c->forced_laps = laps < 0 ? 0 : laps > GC_LAPS_FORCED_MAX ? GC_LAPS_FORCED_MAX : laps;
    return true;
}

void gc_half_reset(gc_half_t *h) { *h = (gc_half_t){.prev = -1}; }

void gc_half_step(gc_half_t *h, double psi, double lead_deg, gc_half_out_t *out)
{
    *out = (gc_half_out_t){0};
    psi = psi - 360.0 * (double)(long)(psi / 360.0);
    if (psi < 0) psi += 360;
    double prev = h->prev < 0 ? psi : h->prev;
    h->prev = psi;
    if (psi < 180) {
        out->follow = true;
        out->target_deg = psi + lead_deg > 180 ? 180 : psi + lead_deg;
        return;
    }
    if (prev < 180) {
        h->pass++;
        h->blinks = 0;
    }
    static const double AT[3] = {205, 255, 300};
    int want = (h->pass & 1) ? 3 : 2;
    if (h->blinks < want && prev < AT[h->blinks] && psi >= AT[h->blinks] && prev <= psi) {
        out->blink = true;
        h->blinks++;
    }
    if (prev < GC_HALF_SNAP_DEG && psi >= GC_HALF_SNAP_DEG) out->snap = true;
}

double gc_orbit(double since_start, double to_end)
{
    double u = fmin(since_start / GC_ORBIT_IN_S, to_end / GC_ORBIT_OUT_S);
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    return u * u * (3 - 2 * u);
}

static void reset_wait(glint_chase_t *c)
{
    c->work_s = 0;
    c->wait_s = uniform(c, GC_WAIT_MIN_S, GC_WAIT_MAX_S);
}

void gc_tick(glint_chase_t *c, double t, bool active, double lap_s, gc_out_t *out)
{
    double dt = c->last_t < 0 || t < c->last_t ? 0 : t - c->last_t;
    c->last_t = t;
    out->chasing = false;
    out->style = GC_STYLE_FULL;
    out->label_opa = 1;
    out->orbit = 0;
    if (!active) {
        if (c->phase != GC_IDLE || c->work_s > 0) {
            c->phase = GC_IDLE;
            reset_wait(c);
        }
        out->shift_x = c->shift_x;
        out->shift_y = c->shift_y;
        return;
    }
    switch (c->phase) {
    case GC_IDLE:
        c->work_s += dt;
        if (c->work_s < c->wait_s) break;
        c->phase = GC_CHASE;
        c->style = c->forced ? c->forced - 1 : (int)(next(c) & 1);
        c->forced = 0;
        c->laps = GC_LAPS_MIN + (int)(next(c) % (GC_LAPS_MAX - GC_LAPS_MIN + 1));
        if (c->forced_laps) c->laps = c->forced_laps;
        c->forced_laps = 0;
        c->start_at = t;
        c->end_at = t + c->laps * lap_s;
        /* fall through */
    case GC_CHASE:
        if (t < c->end_at) {
            out->chasing = true;
            out->style = c->style;
            out->orbit = (float)gc_orbit(t - c->start_at, c->end_at - t);
            break;
        }
        c->phase = GC_REST;
        c->back_at = c->end_at + GC_BACK_AFTER_S;
        {
            int k = (c->shift_x / GC_SHIFT_PX + 1) * 3 + (c->shift_y / GC_SHIFT_PX + 1);
            int n = (k + 1 + (int)(next(c) % 8)) % 9;
            c->shift_x = (n / 3 - 1) * GC_SHIFT_PX;
            c->shift_y = (n % 3 - 1) * GC_SHIFT_PX;
        }
        /* fall through */
    case GC_REST: {
        double u = (t - c->back_at) / GC_FADE_IN_S;
        out->label_opa = u <= 0 ? 0 : u >= 1 ? 1 : (float)u;
        if (u >= 1) {
            c->phase = GC_IDLE;
            reset_wait(c);
        }
        break;
    }
    }
    out->shift_x = c->shift_x;
    out->shift_y = c->shift_y;
}
