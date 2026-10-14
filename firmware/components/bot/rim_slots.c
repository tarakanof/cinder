#include "rim_slots.h"

#include <limits.h>
#include <math.h>

#ifdef RS_RACE_POINT
void rs_race_point(rim_slots_t *s, int at);
#else
#define RS_RACE_POINT(s, at)
#endif

void rim_slots_init(rim_slots_t *s, void *const buf[RIM_SLOTS])
{
    *s = (rim_slots_t){.shown = RIM_SLOT_BASE};
    for (int v = 0; v < RIM_SLOTS; v++) {
        double sx, sy;
        bot_rim_variant(v - RIM_SLOT_BASE, &sx, &sy);
        s->buf[v] = buf[v];
        s->sx[v] = (float)sx;
        s->sy[v] = (float)sy;
        atomic_init(&s->var_gen[v], UINT_MAX);
    }
    for (int d = 1, n = 0; d <= BOT_RIM_STEPS; d++) {
        s->order[n++] = RIM_SLOT_BASE - d;
        s->order[n++] = RIM_SLOT_BASE + d;
    }
    atomic_init(&s->base_gen, UINT_MAX);
    atomic_init(&s->own, false);
    atomic_init(&s->vars, true);
    rim_beat_init(&s->beat);
}

void rim_slots_run(rim_slots_t *s, rim_slots_raster_t raster, void (*yield)(void *ctx), void *ctx)
{
    unsigned gen;
    int shape, tone;
    if (!rim_job_take(&s->job, &gen, &shape, &tone)) return;
    if (!atomic_load(&s->own) && atomic_load(&s->base_gen) != gen) {
        raster(ctx, s->buf[RIM_SLOT_SPARE], shape, tone, 1.0f, 1.0f);
        RS_RACE_POINT(s, 1);
        atomic_store(&s->base_gen, gen);
        RS_RACE_POINT(s, 2);
        yield(ctx);
    }
    for (int k = 0; k < RIM_SLOTS - 1; k++) {
        if (!rim_job_live(&s->job, gen) || !atomic_load(&s->vars)) break;
        int v = s->order[k];
        if (atomic_load(&s->var_gen[v]) == gen || (v == RIM_SLOT_SPARE && !atomic_load(&s->own))) continue;
        RS_RACE_POINT(s, 3);
        raster(ctx, s->buf[v], shape, tone, s->sx[v], s->sy[v]);
        RS_RACE_POINT(s, 4);
        atomic_store(&s->var_gen[v], gen);
        yield(ctx);
    }
}

void rim_slots_show(rim_slots_t *s, int slot, const rim_slots_ops_t *ops)
{
    s->shown = slot;
    ops->show(ops->ctx, s->buf[slot]);
}

static void slots_start(rim_slots_t *s, const rim_slots_ops_t *ops)
{
    if (s->shown != RIM_SLOT_BASE) rim_slots_show(s, RIM_SLOT_BASE, ops);
    atomic_store(&s->own, false);
    RS_RACE_POINT(s, 5);
    rim_job_new(&s->job, s->beat.job & BOT_RIM_TRIANGLE ? 1 : 0, s->beat.job & ~BOT_RIM_TRIANGLE);
    ops->wake(ops->ctx);
}

static void slots_swap(rim_slots_t *s, const rim_slots_ops_t *ops)
{
    void *old = s->buf[RIM_SLOT_BASE];
    s->buf[RIM_SLOT_BASE] = s->buf[RIM_SLOT_SPARE];
    s->buf[RIM_SLOT_SPARE] = old;
    RS_RACE_POINT(s, 7);
    rim_slots_show(s, RIM_SLOT_BASE, ops);
    RS_RACE_POINT(s, 6);
    atomic_store(&s->own, true);
    ops->swapped(ops->ctx, s->beat.shown);
    ops->wake(ops->ctx);
}

static bool base_ready(rim_slots_t *s) { return atomic_load(&s->base_gen) == rim_job_gen(&s->job); }

bool rim_slots_feed(rim_slots_t *s, int key, bool shown, const rim_slots_ops_t *ops)
{
    if (shown && !atomic_exchange(&s->vars, true) && s->beat.job >= 0) ops->wake(ops->ctx);
    if (!shown) atomic_store(&s->vars, false);
    return rim_beat_ready(&s->beat, (int)bot_rim_mood(key), base_ready(s));
}

static bool still(float ratio) { return fabsf(ratio - 1.0f) < 0.015f; }

static int variant_for(rim_slots_t *s, float ratio)
{
    if (still(ratio)) return RIM_SLOT_BASE;
    unsigned gen = rim_job_gen(&s->job);
    int best = RIM_SLOT_BASE;
    float best_d = fabsf(ratio - 1.0f);
    for (int v = 0; v < RIM_SLOTS; v++) {
        if (v != RIM_SLOT_BASE && atomic_load(&s->var_gen[v]) != gen) continue;
        float d = fabsf(ratio - s->sy[v] / s->sx[v]);
        if (d < best_d) {
            best_d = d;
            best = v;
        }
    }
    return best;
}

int rim_slots_sync(rim_slots_t *s, int want, int face, float ratio, double t, const rim_slots_ops_t *ops)
{
    unsigned gen = rim_job_gen(&s->job);
    bool m1 = atomic_load(&s->var_gen[RIM_SLOT_BASE - 1]) == gen, p1 = atomic_load(&s->var_gen[RIM_SLOT_BASE + 1]) == gen;
    rim_beat_out_t o;
    rim_beat_step(&s->beat, want, face, base_ready(s), m1, p1, still(ratio), t, &o);
    if (o.start) slots_start(s, ops);
    if (o.swap) slots_swap(s, ops);
    return !o.own ? RIM_SLOT_BASE : o.settle ? RIM_SLOT_BASE + o.settle : variant_for(s, ratio);
}

void rim_slots_hold(rim_slots_t *s, bool hold, const rim_slots_ops_t *ops)
{
    if (hold) rim_job_hold(&s->job);
    else if (rim_job_release(&s->job) && s->beat.job >= 0) ops->wake(ops->ctx);
}
