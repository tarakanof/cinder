#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TONE_AMP_MAX 16384
#define TONE_FADE_MS 5

typedef struct {
    uint32_t rate;
    uint32_t hz;
    uint32_t total;
    uint32_t done;
    uint32_t fade;
    int16_t amp;
} tone_t;

/* A sine of hz for ms at vol_pct of TONE_AMP_MAX (half of full scale), with TONE_FADE_MS linear ramps at both ends. */
void tone_init(tone_t *t, uint32_t rate, uint32_t hz, uint32_t ms, uint8_t vol_pct);
/* Writes up to n mono 16-bit samples; returns how many, 0 once the tone is over. */
size_t tone_fill(tone_t *t, int16_t *buf, size_t n);
bool tone_done(const tone_t *t);
