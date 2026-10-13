#include "refr_stats.h"

#include <string.h>

void refr_stats_init(refr_stats_t *s, int64_t now_us)
{
    memset(s, 0, sizeof *s);
    s->since_us = now_us;
}

void refr_stats_begin(refr_stats_t *s, int kind)
{
    s->cur = kind < 0 || kind >= REFR_KINDS ? 0 : kind;
    s->cur_areas = 0;
    s->cur_px = 0;
}

void refr_stats_render(refr_stats_t *s, const refr_area_t *areas, const uint8_t *joined, int n)
{
    for (int i = 0; i < n; i++) {
        if (joined && joined[i]) continue;
        int64_t w = (int64_t)areas[i].x2 - areas[i].x1 + 1, h = (int64_t)areas[i].y2 - areas[i].y1 + 1;
        if (w <= 0 || h <= 0) continue;
        s->cur_areas++;
        s->cur_px += w * h;
    }
}

bool refr_stats_end(refr_stats_t *s, int64_t us)
{
    if (!s->cur_px) return false;
    refr_kind_t *k = &s->kind[s->cur];
    k->n++;
    k->areas += s->cur_areas;
    k->px += s->cur_px;
    k->us += us;
    if (us > k->us_max) k->us_max = us;
    if (s->cur_areas > k->areas_max) k->areas_max = s->cur_areas;
    s->cur_areas = 0;
    s->cur_px = 0;
    return true;
}

int64_t refr_stats_take(refr_stats_t *s, int64_t now_us, refr_kind_t out[REFR_KINDS], refr_kind_t *total)
{
    memcpy(out, s->kind, sizeof s->kind);
    memset(s->kind, 0, sizeof s->kind);
    int64_t win = now_us - s->since_us;
    s->since_us = now_us;
    memset(total, 0, sizeof *total);
    for (int i = 0; i < REFR_KINDS; i++) {
        total->n += out[i].n;
        total->areas += out[i].areas;
        total->px += out[i].px;
        total->us += out[i].us;
        if (out[i].us_max > total->us_max) total->us_max = out[i].us_max;
        if (out[i].areas_max > total->areas_max) total->areas_max = out[i].areas_max;
    }
    return win;
}
