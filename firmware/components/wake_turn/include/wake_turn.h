#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAKE_SLICE_MAX_MS 1000
#define WAKE_SLICE_NP_MS 50

typedef struct {
    /* An item now (wait_ms 0) or within wait_ms; true when *item holds one. */
    bool (*take)(void *ctx, int wait_ms, void *item);
    /* Up to ms for a wake, drained: 1 woken, 0 timed out, -1 could not wait. */
    int (*wait)(void *ctx, int ms);
    void *ctx;
} wake_ops_t;

/* One turn of a wait between plain polls: true when an item was taken. Never blocks past ms; returns early on a wake. */
bool wake_turn(const wake_ops_t *o, int ms, void *item);

/* left_ms cut to due_ms (> 0), WAKE_SLICE_MAX_MS, fade_ms (>= 0) and WAKE_SLICE_NP_MS while now playing shows. */
int wake_slice_ms(int left_ms, int due_ms, int fade_ms, bool np_visible);

#ifdef __cplusplus
}
#endif
