#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy poll task: one controller task switches it, the task frees and parks when off, the controller deletes it (no self-delete). */

typedef enum { LT_GONE, LT_RUN, LT_STOP, LT_PARKED } lt_state_t;

#define LT_REAP 1
#define LT_CREATE 2

typedef struct {
    atomic_int state;
    int64_t next_try_ms;
    uint32_t backoff_ms;
} legacy_task_t;

void lt_init(legacy_task_t *t);

/* Controller, every pass: on LT_REAP delete the parked task first, on LT_CREATE allocate+create, then lt_created(). */
int lt_want(legacy_task_t *t, bool on, int64_t now_ms);

/* Controller: retry delay in ms after a failed create or allocation (0 after success). */
uint32_t lt_created(legacy_task_t *t, bool ok, int64_t now_ms);

/* Poll task: true while it should poll. */
bool lt_on(legacy_task_t *t);

/* Poll task, once lt_on() is false and its memory is freed: true = parked (suspend), false = on again (poll). */
bool lt_park(legacy_task_t *t);

#ifdef __cplusplus
}
#endif
