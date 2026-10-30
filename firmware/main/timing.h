#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_timer.h"
#include "span_stats.h"

/* Safe from any task. Never logs; the critical section covers a few stores. Read and reset by the USB "timing" op. */
void timing_add(int span, int64_t us);
void timing_read(span_stats_t *out, bool reset);

static inline int64_t timing_now(void)
{
    return esp_timer_get_time();
}
