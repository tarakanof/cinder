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
    int64_t cur_px, since_us;
} refr_stats_t;

typedef struct {
    int32_t x1, y1, x2, y2;
} refr_area_t;

void refr_stats_init(refr_stats_t *s, int64_t now_us);
void refr_stats_begin(refr_stats_t *s, int kind);
void refr_stats_render(refr_stats_t *s, const refr_area_t *areas, const uint8_t *joined, int n);
bool refr_stats_end(refr_stats_t *s, int64_t us);
int64_t refr_stats_take(refr_stats_t *s, int64_t now_us, refr_kind_t out[REFR_KINDS], refr_kind_t *total);
