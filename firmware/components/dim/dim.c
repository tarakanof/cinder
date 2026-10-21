#include "dim.h"

void dim_fade_init(dim_fade_t *f, int cur)
{
    f->cur = f->target = cur;
    f->step = 0;
    f->floor = DIM_FLOOR;
    f->next_ms = 0;
    f->off = false;
}

static int fade_goal(const dim_fade_t *f) { return f->off ? 0 : f->target; }

static void fade_retarget(dim_fade_t *f, bool one_step)
{
    int g = fade_goal(f);
    int d = g > f->cur ? g - f->cur : f->cur - g;
    f->step = one_step ? d : (d + DIM_FADE_STEPS - 1) / DIM_FADE_STEPS;
}

void dim_fade_sync(dim_fade_t *f, int level)
{
    f->cur = level < 0 ? 0 : level > 255 ? 255 : level;
    fade_retarget(f, false);
}

bool dim_fade_set_off(dim_fade_t *f, bool off)
{
    if (f->off == off) return false;
    f->off = off;
    fade_retarget(f, true);
    return f->step > 0;
}

void dim_fade_set_floor(dim_fade_t *f, int floor) { f->floor = floor < 1 ? 1 : floor > 255 ? 255 : floor; }

bool dim_fade_set_target(dim_fade_t *f, int level)
{
    int t = level < f->floor ? f->floor : level > 255 ? 255 : level;
    if (t == f->target) return false;
    f->target = t;
    if (f->off) return false;
    fade_retarget(f, false);
    return f->step > 0;
}

bool dim_fade_tick(dim_fade_t *f, uint8_t *out)
{
    int g = fade_goal(f);
    if (f->cur == g) {
        f->step = 0;
        return false;
    }
    if (f->step < 1) f->step = 1;
    if (f->cur < g) {
        f->cur += f->step;
        if (f->cur > g) f->cur = g;
    } else {
        f->cur -= f->step;
        if (f->cur < g) f->cur = g;
    }
    *out = (uint8_t)f->cur;
    return true;
}

int dim_fade_step(dim_fade_t *f, int64_t now_ms, uint8_t *out, bool *set)
{
    *set = false;
    if (f->cur == fade_goal(f)) {
        f->step = 0;
        f->next_ms = 0;
        return -1;
    }
    if (now_ms + 1 < f->next_ms) return (int)(f->next_ms - now_ms);
    *set = dim_fade_tick(f, out);
    f->next_ms = now_ms + DIM_STEP_MS;
    return DIM_STEP_MS;
}

bool dim_level_valid(bool present, double level)
{
    return present && level >= 0 && level <= 255;
}

static int clamp_level(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

int dim_quiet_level(int level, int floor, bool quiet, int dim_level)
{
    int v = clamp_level(level < floor ? floor : level);
    if (quiet && v > dim_level) v = clamp_level(dim_level);
    return v;
}

int dim_quiet_floor(int floor, bool quiet, int dim_level) { return quiet && dim_level < floor ? dim_level : floor; }

void dim_idle_init(dim_idle_t *s, int64_t now_ms) { s->last_ms = now_ms; }

dim_idle_state_t dim_idle_state(const dim_idle_rules_t *r, const dim_idle_t *s, int64_t now_ms)
{
    int64_t idle = now_ms - s->last_ms;
    if (r->off_after_s > 0 && idle >= (int64_t)r->off_after_s * 1000) return DIM_IDLE_OFF;
    if (r->dim_after_s > 0 && idle >= (int64_t)r->dim_after_s * 1000) return DIM_IDLE_DIMMED;
    return DIM_IDLE_AWAKE;
}

void dim_idle_wake(dim_idle_t *s, int64_t now_ms)
{
    if (now_ms > s->last_ms) s->last_ms = now_ms;
}

bool dim_idle_input(dim_idle_t *s, const dim_idle_rules_t *r, int64_t now_ms)
{
    bool was_off = dim_idle_state(r, s, now_ms) == DIM_IDLE_OFF;
    dim_idle_wake(s, now_ms);
    return was_off;
}

int dim_idle_level(int level, dim_idle_state_t st, const dim_idle_rules_t *r)
{
    if (st == DIM_IDLE_OFF) return 0;
    if (st == DIM_IDLE_DIMMED && level > r->dim_level) return clamp_level(r->dim_level);
    return level;
}

int dim_idle_floor(int floor, dim_idle_state_t st, const dim_idle_rules_t *r)
{
    return st == DIM_IDLE_DIMMED && r->dim_level < floor ? r->dim_level : floor;
}

int dim_idle_light(dim_idle_state_t was, int awake_level, int shown)
{
    if (was == DIM_IDLE_AWAKE || awake_level <= 0) return -1;
    if (shown >= awake_level) return -1;
    return clamp_level(awake_level);
}

int dim_fade_take(dim_fade_t *f, int lit, dim_idle_state_t now)
{
    if (lit < 0) return -1;
    dim_fade_sync(f, lit);
    return now == DIM_IDLE_AWAKE ? lit : -1;
}
