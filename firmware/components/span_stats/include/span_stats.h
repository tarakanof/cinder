#pragma once

#include <stdint.h>

typedef enum {
    SPAN_UPDATE,
    SPAN_FADE,
    SPAN_LABEL,
    SPAN_V15_RENDER,
    SPAN_ARC_TEXT,
    SPAN_RING,
    SPAN_M8,
    SPAN_COVER_FETCH,
    SPAN_COVER_DECODE,
    SPAN_RIM,
    SPAN_COUNT
} span_id_t;

typedef struct {
    uint32_t n;
    int64_t total_us, max_us;
} span_t;

typedef struct {
    span_t span[SPAN_COUNT];
    int64_t since_us;
} span_stats_t;

const char *span_name(int id);
void span_stats_init(span_stats_t *s, int64_t now_us);
void span_stats_add(span_stats_t *s, int id, int64_t us);
int64_t span_avg_us(const span_t *sp);
