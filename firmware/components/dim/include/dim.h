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
    int64_t next_ms;
    bool off;
} dim_fade_t;

void dim_fade_init(dim_fade_t *f, int cur);

void dim_fade_set_floor(dim_fade_t *f, int floor);

bool dim_fade_set_target(dim_fade_t *f, int level);

bool dim_fade_tick(dim_fade_t *f, uint8_t *out);

/* dim_fade_tick once per DIM_STEP_MS of now_ms (1 ms early counts: tick rounding); *set when *out is to be written. ms to the next step, -1 settled. */
int dim_fade_step(dim_fade_t *f, int64_t now_ms, uint8_t *out, bool *set);

/* While off the fade goes to 0 in one step, below the floor; leaving off it jumps back to target in one step. set_target keeps the level for then. */
bool dim_fade_set_off(dim_fade_t *f, bool off);

/* The owner's fade learns that another task already wrote level to the panel: cur follows, the goal is unchanged. */
void dim_fade_sync(dim_fade_t *f, int level);

bool dim_level_valid(bool present, double level);

/* Quiet caps the level at dim_level, below the floor too (docs/features.md, quiet hours). */
int dim_quiet_level(int level, int floor, bool quiet, int dim_level);

int dim_quiet_floor(int floor, bool quiet, int dim_level);

/* Idle rules from the knob config (docs/features.md, idle dim and off): 0 disables dim or off; seconds. */
typedef struct {
    int dim_after_s;
    int dim_level;
    int off_after_s;
} dim_idle_rules_t;

typedef enum { DIM_IDLE_AWAKE, DIM_IDLE_DIMMED, DIM_IDLE_OFF } dim_idle_state_t;

/* last_ms is the time of the last input or wake source on the caller's clock. Callers serialise access. */
typedef struct {
    int64_t last_ms;
} dim_idle_t;

void dim_idle_init(dim_idle_t *s, int64_t now_ms);

dim_idle_state_t dim_idle_state(const dim_idle_rules_t *r, const dim_idle_t *s, int64_t now_ms);

/* A wake source that is not input (mood to waiting or error, the OTA face, a demo): restarts the clock, nothing is swallowed. */
void dim_idle_wake(dim_idle_t *s, int64_t now_ms);

/* An input: true when the panel was off, so the input only wakes it and must not reach a page. Restarts the clock either way. */
bool dim_idle_input(dim_idle_t *s, const dim_idle_rules_t *r, int64_t now_ms);

/* level is already floored and quiet-capped: the idle cap lowers it below the floor too, 0 while off. */
int dim_idle_level(int level, dim_idle_state_t st, const dim_idle_rules_t *r);

/* The level a task other than the fade's owner writes to the panel at once when an input or wake ends dim or off; -1 when there is nothing to write. */
/* shown is the level the fade owner last published for the panel, -1 unknown: a panel already at or above awake_level is left to the owner's fade down instead of jumping to the end level. */
int dim_idle_light(dim_idle_state_t was, int awake_level, int shown);

/* The owner's take of the lit mailbox: a level posted before the knob went idle again is dropped (-1), not synced. */
int dim_idle_lit_take(int lit, dim_idle_state_t now);

int dim_idle_floor(int floor, dim_idle_state_t st, const dim_idle_rules_t *r);

#ifdef __cplusplus
}
#endif
