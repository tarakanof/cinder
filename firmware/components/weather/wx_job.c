#include "wx_job.h"

#include <math.h>

#ifdef WJ_RACE_POINT
void wj_race_point(wx_job_t *j, int at);
#else
#define WJ_RACE_POINT(j, at)
#endif

void wx_job_init(wx_job_t *j, wx_job_io_t *io)
{
    atomic_store(&j->state, WX_JOB_IDLE);
    j->visible = j->bufs = j->baked = j->held = false;
    j->retry = 0;
    j->io = io;
}

static int park(wx_job_t *j)
{
    int queued = WX_JOB_QUEUED;
    atomic_compare_exchange_strong(&j->state, &queued, WX_JOB_IDLE);
    if (j->visible || !j->bufs || atomic_load(&j->state) == WX_JOB_RUNNING) return 0;
    return WX_DO_RELEASE;
}

int wx_job_show(wx_job_t *j, bool on)
{
    if (on == j->visible) return 0;
    j->visible = on;
    if (!on) return park(j);
    j->retry = 0;
    return j->bufs || j->held ? 0 : WX_DO_ACQUIRE;
}

int wx_job_hold(wx_job_t *j, bool hold)
{
    j->held = hold;
    return hold ? park(j) : 0;
}

int wx_job_step(wx_job_t *j, const wx_temp_t *t, double dt)
{
    if (j->held || !j->visible) return park(j);
    if (!j->bufs) {
        if (isfinite(dt) && dt > 0) j->retry += dt;
        if (j->retry < WX_JOB_RETRY_S) return 0;
        j->retry = 0;
        return WX_DO_ACQUIRE;
    }
    int s = atomic_load(&j->state);
    if (s == WX_JOB_DONE) {
        j->io->key = j->io->in;
        j->baked = true;
        atomic_store(&j->state, WX_JOB_IDLE);
        return WX_DO_APPLY | WX_DO_PAINT;
    }
    if (s == WX_JOB_IDLE && (!j->baked || !wx_temp_same_bake(t, &j->io->key))) {
        j->io->in = *t;
        WJ_RACE_POINT(j, 1);
        atomic_store(&j->state, WX_JOB_QUEUED);
        WJ_RACE_POINT(j, 2);
        return WX_DO_BAKE | (j->baked ? WX_DO_DETACH : 0);
    }
    if (!j->baked || s != WX_JOB_IDLE) return 0;
    return WX_DO_PAINT;
}

void wx_job_acquired(wx_job_t *j, bool ok)
{
    j->bufs = ok;
    j->retry = 0;
}

void wx_job_released(wx_job_t *j)
{
    j->bufs = j->baked = false;
    j->retry = 0;
    atomic_store(&j->state, WX_JOB_IDLE);
}

const wx_temp_t *wx_job_take(wx_job_t *j)
{
    int queued = WX_JOB_QUEUED;
    if (!atomic_compare_exchange_strong(&j->state, &queued, WX_JOB_RUNNING)) return NULL;
    return &j->io->in;
}

void wx_job_done(wx_job_t *j, int ms)
{
    j->io->ms = ms;
    WJ_RACE_POINT(j, 3);
    atomic_store(&j->state, WX_JOB_DONE);
    WJ_RACE_POINT(j, 4);
}
