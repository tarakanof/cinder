#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Levels are CO5300 WRDISBV 0-255; below DIM_FLOOR AMOLED tints (docs/features.md, dimming). */
#define DIM_FLOOR 10
#define DIM_FADE_STEPS 20
#define DIM_STEP_MS 100
#define DIM_POLL_MS 60000

typedef struct {
    int cur;
    int target;
    int step;
    int floor;
    bool retry;
} dim_fade_t;

void dim_fade_init(dim_fade_t *f, int cur);

void dim_fade_set_floor(dim_fade_t *f, int floor);

bool dim_fade_set_target(dim_fade_t *f, int level);

bool dim_fade_tick(dim_fade_t *f, uint8_t *out);

/* The last ticked level was not written: the next tick returns it again. */
void dim_fade_retry(dim_fade_t *f);

bool dim_level_valid(bool present, double level);

#ifdef __cplusplus
}
#endif
