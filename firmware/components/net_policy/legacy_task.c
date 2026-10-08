#include "legacy_task.h"

#define LT_BACKOFF_MIN_MS 5000u
#define LT_BACKOFF_MAX_MS 300000u

void lt_init(legacy_task_t *t)
{
    atomic_init(&t->state, LT_GONE);
    t->next_try_ms = 0;
    t->backoff_ms = 0;
}

int lt_want(legacy_task_t *t, bool on, int64_t now_ms)
{
    int act = 0;
    if (atomic_load(&t->state) == LT_PARKED) {
        atomic_store(&t->state, LT_GONE);
        act |= LT_REAP;
    }
    if (!on) {
        int run = LT_RUN;
        atomic_compare_exchange_strong(&t->state, &run, LT_STOP);
        return act;
    }
    int st = LT_STOP;
    if (atomic_compare_exchange_strong(&t->state, &st, LT_RUN) || st == LT_RUN) return act;
    if (now_ms < t->next_try_ms) return act;
    atomic_store(&t->state, LT_RUN);
    return act | LT_CREATE;
}

uint32_t lt_created(legacy_task_t *t, bool ok, int64_t now_ms)
{
    if (ok) {
        t->backoff_ms = 0;
        return 0;
    }
    atomic_store(&t->state, LT_GONE);
    t->backoff_ms = !t->backoff_ms ? LT_BACKOFF_MIN_MS
                    : t->backoff_ms * 2 > LT_BACKOFF_MAX_MS ? LT_BACKOFF_MAX_MS
                                                            : t->backoff_ms * 2;
    t->next_try_ms = now_ms + t->backoff_ms;
    return t->backoff_ms;
}

bool lt_on(legacy_task_t *t) { return atomic_load(&t->state) == LT_RUN; }

bool lt_park(legacy_task_t *t)
{
    int stop = LT_STOP;
    return atomic_compare_exchange_strong(&t->state, &stop, LT_PARKED);
}
