#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle of a legacy poll task: the controller (one task) turns it on and off; the poll task ends itself once off.
   Lock-free between the two. Create failures back off 5 s, doubling to 5 min. */

typedef enum { LT_GONE, LT_RUN, LT_STOP } lt_state_t;

typedef struct {
    atomic_int state;
    int64_t next_try_ms;
    uint32_t backoff_ms;
} legacy_task_t;

void lt_init(legacy_task_t *t);

/* Controller. True: create the poll task now, then call lt_created(). */
bool lt_want(legacy_task_t *t, bool on, int64_t now_ms);

/* Controller. Returns the retry delay after a failed create (0 after a good one). */
uint32_t lt_created(legacy_task_t *t, bool ok, int64_t now_ms);

/* Poll task: true while it should poll. */
bool lt_on(legacy_task_t *t);

/* Poll task, once lt_on() is false and it has released what other instances share: true = it is gone for the
   controller, free its own memory and delete itself; false = turned on again, keep polling. */
bool lt_exit(legacy_task_t *t);

#ifdef __cplusplus
}
#endif
