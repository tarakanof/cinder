#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bot_raster.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POMO_RING_PTS 180
#define POMO_RING_TILE 32

typedef struct {
    float cx, cy;
    float r;
    float hw;
} pomo_ring_t;

typedef struct {
    int x, y, w, h;
} pomo_rect_t;

/* Working memory (~9 KB): keep it off small task stacks; one per task that renders. */
typedef struct {
    float tx[POMO_RING_PTS + 1], ty[POMO_RING_PTS + 1];
    float ax[POMO_RING_PTS + 2], ay[POMO_RING_PTS + 2];
    int segs[POMO_RING_PTS + 1];
    uint8_t a_track[POMO_RING_TILE * POMO_RING_TILE];
    uint8_t a_arc[POMO_RING_TILE * POMO_RING_TILE];
    bot_raster_scratch_t scratch;
} pomo_ring_work_t;

void pomo_ring_render(const pomo_ring_t *ring, pomo_ring_work_t *wk, uint16_t *buf, int buf_w, int buf_h,
                      pomo_rect_t rect, float frac, uint32_t arc_rgb, uint32_t track_rgb);

bool pomo_ring_dirty(const pomo_ring_t *ring, float f0, float f1, int buf_w, int buf_h, pomo_rect_t *out);

uint16_t pomo_ring_pixel(uint32_t arc_rgb, uint32_t track_rgb, uint8_t a, uint8_t t);

int pomo_ring_points(const pomo_ring_t *ring, pomo_ring_work_t *wk, float frac);

#ifdef __cplusplus
}
#endif
