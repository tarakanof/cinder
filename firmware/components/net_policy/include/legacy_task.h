#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy poll task: the controller turns it on/off, the task parks when off and the controller deletes it. */

typedef enum { LT_GONE, LT_RUN, LT_STOP, LT_PARKED } lt_state_t;

#define LT_REAP 1
#define LT_CREATE 2

typedef struct {
    atomic_int state;
    int64_t next_try_ms;
    uint32_t backoff_ms;
} legacy_task_t;

void lt_init(legacy_task_t *t);

/* Controller, every pass: LT_REAP deletes the parked task, LT_CREATE creates it then calls lt_created(). */
int lt_want(legacy_task_t *t, bool on, int64_t now_ms);

/* Controller: retry delay in ms after a failed create (0 after success). */
uint32_t lt_created(legacy_task_t *t, bool ok, int64_t now_ms);

/* Poll task: true while it should poll. */
bool lt_on(legacy_task_t *t);

/* Poll task, after lt_on() is false: true = parked (suspend), false = turned on again (keep polling). */
bool lt_park(legacy_task_t *t);

#ifdef __cplusplus
}
#endif
