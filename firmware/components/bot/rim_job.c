#include "rim_job.h"

#ifdef RJ_RACE_POINT
void rj_race_point(rim_job_t *j, int at);
#else
#define RJ_RACE_POINT(j, at)
#endif

void rim_job_new(rim_job_t *j, int shape, int mood)
{
    atomic_store(&j->shape, shape);
    RJ_RACE_POINT(j, 1);
    atomic_store(&j->mood, mood);
    RJ_RACE_POINT(j, 2);
    atomic_fetch_add(&j->gen, 1);
    RJ_RACE_POINT(j, 3);
}

void rim_job_hold(rim_job_t *j)
{
    atomic_store(&j->held, true);
    atomic_fetch_add(&j->gen, 1);
}

bool rim_job_release(rim_job_t *j) { return atomic_exchange(&j->held, false); }

bool rim_job_take(rim_job_t *j, unsigned *gen, int *shape, int *mood)
{
    if (atomic_load(&j->held)) return false;
    RJ_RACE_POINT(j, 4);
    *gen = atomic_load(&j->gen);
    RJ_RACE_POINT(j, 5);
    *shape = atomic_load(&j->shape);
    RJ_RACE_POINT(j, 6);
    *mood = atomic_load(&j->mood);
    RJ_RACE_POINT(j, 7);
    return true;
}

bool rim_job_live(rim_job_t *j, unsigned gen) { return atomic_load(&j->gen) == gen && !atomic_load(&j->held); }

unsigned rim_job_gen(rim_job_t *j) { return atomic_load(&j->gen); }
