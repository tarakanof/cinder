#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle of a legacy poll task: the controller (one task) turns it on and off; once off the poll task frees what it
   holds and parks, and the controller deletes it by handle (no self-delete). Lock-free between the two. Create failures
   back off 5 s, doubling to 5 min. */

typedef enum { LT_GONE, LT_RUN, LT_STOP, LT_PARKED } lt_state_t;

#define LT_REAP 1
#define LT_CREATE 2

typedef struct {
    atomic_int state;
    int64_t next_try_ms;
    uint32_t backoff_ms;
} legacy_task_t;

void lt_init(legacy_task_t *t);

/* Controller, every pass. LT_REAP: delete the parked task by its handle first. LT_CREATE: create the poll task now
   (allocations included), then call lt_created(). */
int lt_want(legacy_task_t *t, bool on, int64_t now_ms);

/* Controller. Returns the retry delay after a failed create or allocation (0 after a good one). */
uint32_t lt_created(legacy_task_t *t, bool ok, int64_t now_ms);

/* Poll task: true while it should poll. */
bool lt_on(legacy_task_t *t);

/* Poll task, once lt_on() is false and it has freed what it holds: true = parked, suspend yourself until the controller
   deletes you; false = turned on again, keep polling. */
bool lt_park(legacy_task_t *t);

#ifdef __cplusplus
}
#endif
