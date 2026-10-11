#pragma once

#include <stdatomic.h>
#include <stdbool.h>

typedef struct {
    atomic_uint gen;
    atomic_bool held;
} rim_job_t;

/* LVGL task: a new outline job; the rim task drops any older one at its next variant. */
void rim_job_new(rim_job_t *j);
/* LVGL task: drops the running job and starts none until rim_job_release. */
void rim_job_hold(rim_job_t *j);
/* LVGL task: true when a hold ended, so the caller wakes the rim task to redo the dropped job. */
bool rim_job_release(rim_job_t *j);
/* rim task, once woken: false while held; else the generation to raster. */
bool rim_job_take(rim_job_t *j, unsigned *gen);
/* rim task, before each variant: false once the job was replaced or held. */
bool rim_job_live(rim_job_t *j, unsigned gen);
unsigned rim_job_gen(rim_job_t *j);
