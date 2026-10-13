#pragma once

#include <stdbool.h>
#include <stdint.h>

#define REFR_KINDS 4

typedef struct {
    int n, areas_max;
    int64_t areas, px, us, us_max;
} refr_kind_t;

typedef struct {
    refr_kind_t kind[REFR_KINDS];
    int cur, cur_areas;
    int64_t cur_px;
} refr_stats_t;

void refr_stats_start(refr_stats_t *s, int kind, int areas, int64_t px);
bool refr_stats_end(refr_stats_t *s, int64_t us);
void refr_stats_take(refr_stats_t *s, refr_kind_t out[REFR_KINDS], refr_kind_t *total);
