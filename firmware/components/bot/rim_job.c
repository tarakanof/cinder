#include "rim_job.h"

/* Host tests only: runs the rim task's take between the outline's store and the generation bump. */
#ifdef RJ_RACE_POINT
void rj_race_point(rim_job_t *j);
#else
#define RJ_RACE_POINT(j)
#endif

void rim_job_new(rim_job_t *j, int shape, int mood)
{
    atomic_store(&j->shape, shape);
    atomic_store(&j->mood, mood);
    RJ_RACE_POINT(j);
    atomic_fetch_add(&j->gen, 1);
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
    *gen = atomic_load(&j->gen);
    *shape = atomic_load(&j->shape);
    *mood = atomic_load(&j->mood);
    return true;
}

bool rim_job_live(rim_job_t *j, unsigned gen) { return atomic_load(&j->gen) == gen && !atomic_load(&j->held); }

unsigned rim_job_gen(rim_job_t *j) { return atomic_load(&j->gen); }
