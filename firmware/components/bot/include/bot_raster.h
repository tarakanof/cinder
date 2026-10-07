#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOT_RASTER_MAX_W 512
#define BOT_RASTER_MAX_SEGS 192

/* One per task that rasterises; tasks must not share one. */
typedef struct {
    int row_segs[BOT_RASTER_MAX_SEGS];
    uint8_t state[BOT_RASTER_MAX_W];
} bot_raster_scratch_t;

/* alpha != NULL: buf is the colour plane and may be NULL (already filled). */
void bot_raster_stroke(bot_raster_scratch_t *scratch, uint16_t *buf, uint8_t *alpha, int w, int h, int ox,
                       int oy, const float *xs, const float *ys, int nseg, const int *segs, float hw, uint32_t rgb,
                       float gain);

void bot_raster_stroke_ref(uint16_t *buf, uint8_t *alpha, int w, int h, int ox, int oy, const float *xs,
                           const float *ys, int nseg, const int *segs, float hw, uint32_t rgb, float gain);

#ifdef __cplusplus
}
#endif
