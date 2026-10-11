#pragma once

#include <stdatomic.h>
#include <stdbool.h>

#include "wx_paint.h"
#include "wx_temp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_JOB_RETRY_S 5.0

enum { WX_JOB_IDLE, WX_JOB_QUEUED, WX_JOB_RUNNING, WX_JOB_DONE };

enum {
    WX_DO_RELEASE = 1, /* free the buffers, then wx_job_released */
    WX_DO_ACQUIRE = 2, /* allocate the buffers, then wx_job_acquired */
    WX_DO_DETACH = 4,  /* hide the face and point the baked canvases away from the buffers the bake rewrites */
    WX_DO_BAKE = 8,    /* wake the bake task */
    WX_DO_APPLY = 16,  /* attach the buffers and place the sprites for io->in and io->out */
    WX_DO_PAINT = 32,  /* buffers hold a finished bake and none runs: animate and show the face */
};

typedef struct {
    wx_temp_t in, key;
    wx_bake_out_t out;
    int ms;
} wx_job_io_t;

/* state: internal RAM (atomic). io: any RAM; the bake task owns in/out while QUEUED or RUNNING. */
typedef struct {
    atomic_int state;
    bool visible, bufs, baked, held;
    double retry;
    wx_job_io_t *io;
} wx_job_t;

/* LVGL task: every call below except wx_job_take and wx_job_done. Each returns WX_DO_* bits to act on in order. */
void wx_job_init(wx_job_t *j, wx_job_io_t *io);
int wx_job_show(wx_job_t *j, bool on);
int wx_job_hold(wx_job_t *j, bool hold);
int wx_job_step(wx_job_t *j, const wx_temp_t *t, double dt);
void wx_job_acquired(wx_job_t *j, bool ok);
void wx_job_released(wx_job_t *j);

/* Bake task, once woken: the key to bake, or NULL when no job is queued. */
const wx_temp_t *wx_job_take(wx_job_t *j);
/* Bake task, after writing io->out: publishes the bake that took ms. */
void wx_job_done(wx_job_t *j, int ms);

#ifdef __cplusplus
}
#endif
