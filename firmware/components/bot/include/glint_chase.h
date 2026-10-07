#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GC_WAIT_MIN_S
#define GC_WAIT_MIN_S 300.0
#endif
#ifndef GC_WAIT_MAX_S
#define GC_WAIT_MAX_S 900.0
#endif
#define GC_LAPS_MIN 2
#define GC_LAPS_MAX 5
#define GC_ORBIT_IN_S 1.0
#define GC_ORBIT_OUT_S 1.0
#define GC_LAPS_FORCED_MAX 20
#define GC_BACK_AFTER_S 3.0
#define GC_FADE_IN_S 1.0
#define GC_SHIFT_PX 2

typedef enum { GC_IDLE, GC_CHASE, GC_REST } gc_phase_t;

typedef struct {
    uint32_t rng;
    gc_phase_t phase;
    double last_t;
    double work_s;
    double wait_s;
    double start_at, end_at, back_at;
    int laps;
    int shift_x, shift_y;
    int style;
    int forced;
    int forced_laps;
} glint_chase_t;

#define GC_STYLE_FULL 0
#define GC_STYLE_HALF 1

typedef struct {
    bool chasing;
    int style;
    float label_opa;
    float orbit;
    int shift_x, shift_y;
} gc_out_t;

void gc_init(glint_chase_t *c, uint32_t seed);

/* t in seconds; psi in screen degrees (0 = 3 o'clock, clockwise). */
void gc_tick(glint_chase_t *c, double t, bool active, double lap_s, gc_out_t *out);

double gc_orbit(double since_start, double to_end);

bool gc_start_now(glint_chase_t *c, int style, int laps);

typedef struct {
    double prev;
    int pass;
    int blinks;
} gc_half_t;

typedef struct {
    bool follow;
    double target_deg;
    bool blink;
    bool snap;
} gc_half_out_t;

#define GC_HALF_SNAP_DEG 345.0
void gc_half_reset(gc_half_t *h);
void gc_half_step(gc_half_t *h, double psi, double lead_deg, gc_half_out_t *out);

#ifdef __cplusplus
}
#endif
