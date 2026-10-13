#include "refr_stats.h"

#include <string.h>

void refr_stats_start(refr_stats_t *s, int kind, int areas, int64_t px)
{
    s->cur = kind < 0 || kind >= REFR_KINDS ? 0 : kind;
    s->cur_areas = px > 0 && areas > 0 ? areas : 0;
    s->cur_px = s->cur_areas ? px : 0;
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

void refr_stats_take(refr_stats_t *s, refr_kind_t out[REFR_KINDS], refr_kind_t *total)
{
    memcpy(out, s->kind, sizeof s->kind);
    memset(s->kind, 0, sizeof s->kind);
    memset(total, 0, sizeof *total);
    for (int i = 0; i < REFR_KINDS; i++) {
        total->n += out[i].n;
        total->areas += out[i].areas;
        total->px += out[i].px;
        total->us += out[i].us;
        if (out[i].us_max > total->us_max) total->us_max = out[i].us_max;
        if (out[i].areas_max > total->areas_max) total->areas_max = out[i].areas_max;
    }
}
