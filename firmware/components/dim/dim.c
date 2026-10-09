#include "dim.h"

void dim_fade_init(dim_fade_t *f, int cur)
{
    f->cur = f->target = cur;
    f->step = 0;
    f->floor = DIM_FLOOR;
}

void dim_fade_set_floor(dim_fade_t *f, int floor) { f->floor = floor < 1 ? 1 : floor > 255 ? 255 : floor; }

bool dim_fade_set_target(dim_fade_t *f, int level)
{
    int t = level < f->floor ? f->floor : level > 255 ? 255 : level;
    if (t == f->target) return false;
    f->target = t;
    int d = t > f->cur ? t - f->cur : f->cur - t;
    f->step = (d + DIM_FADE_STEPS - 1) / DIM_FADE_STEPS;
    return f->step > 0;
}

bool dim_fade_tick(dim_fade_t *f, uint8_t *out)
{
    if (f->cur == f->target) {
        f->step = 0;
        return false;
    }
    if (f->step < 1) f->step = 1;
    if (f->cur < f->target) {
        f->cur += f->step;
        if (f->cur > f->target) f->cur = f->target;
    } else {
        f->cur -= f->step;
        if (f->cur < f->target) f->cur = f->target;
    }
    *out = (uint8_t)f->cur;
    return true;
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
