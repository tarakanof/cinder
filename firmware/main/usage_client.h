#pragma once

#include <stdbool.h>

#include "usage.h"

typedef struct {
    bool have_srv;
    double srv_offset;   /* server Unix s = pomo_client_now() + srv_offset */
    bool has_utc_offset;
    int utc_offset;
    long long fed_at;    /* server Unix s of the last applied view, 0 unknown; not a change by itself */
} usage_clock_t;

/* Once, before the ember task starts. */
void usage_client_init(void);
/* Ember task, every applied view: b NULL or empty clears. */
void usage_client_feed(const usage_block_t *b, bool has_utc_offset, int utc_offset);
/* Any task: copies the block when it changed since *gen and returns true. */
bool usage_client_take(unsigned *gen, usage_block_t *out, usage_clock_t *clk);
