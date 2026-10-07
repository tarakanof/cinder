#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Angles are screen degrees: 0 = 3 o'clock, clockwise (y down). */
typedef struct {
    float cx, cy;
    float r;
    float hw;
    float tail_deg;
} ring_glint_t;

/* Screen px; x, y, w, h even: the panel driver rounds areas to 2 px (docs/features.md, glint). */
typedef struct {
    int x, y, w, h;
} ring_glint_box_t;

void ring_glint_box(const ring_glint_t *g, float head_deg, ring_glint_box_t *out);

void ring_glint_span_box(const ring_glint_t *g, float end_deg, float span_deg, ring_glint_box_t *out);

void ring_glint_raster(const ring_glint_t *g, float head_deg, const ring_glint_box_t *b, uint8_t *alpha);

uint32_t ring_glint_color(uint32_t rgb, float mix);

#ifdef __cplusplus
}
#endif
