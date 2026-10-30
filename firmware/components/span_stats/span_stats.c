#include "span_stats.h"

#include <string.h>

static const char *const NAMES[SPAN_COUNT] = {"update", "fade", "label", "v15_render", "arc_text",
                                              "ring", "m8", "cover_fetch", "cover_decode", "rim"};

const char *span_name(int id)
{
    return id >= 0 && id < SPAN_COUNT ? NAMES[id] : "";
}

void span_stats_init(span_stats_t *s, int64_t now_us)
{
    memset(s, 0, sizeof *s);
    s->since_us = now_us;
}

void span_stats_add(span_stats_t *s, int id, int64_t us)
{
    if (id < 0 || id >= SPAN_COUNT) return;
    if (us < 0) us = 0;
    span_t *sp = &s->span[id];
    sp->n++;
    sp->total_us += us;
    if (us > sp->max_us) sp->max_us = us;
}

int64_t span_avg_us(const span_t *sp)
{
    return sp->n ? (sp->total_us + (int64_t)sp->n / 2) / sp->n : 0;
}
