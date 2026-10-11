#include "rim_job.h"

void rim_job_new(rim_job_t *j) { atomic_fetch_add(&j->gen, 1); }

void rim_job_hold(rim_job_t *j)
{
    atomic_store(&j->held, true);
    atomic_fetch_add(&j->gen, 1);
}

bool rim_job_release(rim_job_t *j) { return atomic_exchange(&j->held, false); }

bool rim_job_take(rim_job_t *j, unsigned *gen)
{
    if (atomic_load(&j->held)) return false;
    *gen = atomic_load(&j->gen);
    return true;
}

bool rim_job_live(rim_job_t *j, unsigned gen) { return atomic_load(&j->gen) == gen && !atomic_load(&j->held); }

unsigned rim_job_gen(rim_job_t *j) { return atomic_load(&j->gen); }
