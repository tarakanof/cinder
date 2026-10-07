#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POMO_PHASE_IDLE,
    POMO_PHASE_FOCUS,
    POMO_PHASE_SHORT_BREAK,
    POMO_PHASE_LONG_BREAK,
    POMO_PHASE_UNKNOWN,
} pomo_phase_t;

typedef struct {
    pomo_phase_t phase;
    bool running;
    bool paused;
    int remaining_sec;
    int planned_sec;
    int round;
} pomo_state_t;

typedef enum { POMO_MODE_IDLE, POMO_MODE_RUNNING, POMO_MODE_PAUSED, POMO_MODE_PARKED } pomo_mode_t;

typedef enum {
    POMO_ACT_NONE,
    POMO_ACT_START,
    POMO_ACT_PAUSE,
    POMO_ACT_RESUME,
    POMO_ACT_STOP,
    POMO_ACT_SKIP,
} pomo_action_t;

typedef enum { POMO_INPUT_PUSH, POMO_INPUT_LONG_PUSH } pomo_input_t;

pomo_phase_t pomo_phase_from_wire(const char *s);
bool pomo_phase_is_break(pomo_phase_t p);
pomo_mode_t pomo_mode(const pomo_state_t *s);

pomo_action_t pomo_action_for(const pomo_state_t *s, pomo_input_t in);
const char *pomo_action_path(pomo_action_t a);

float pomo_fraction(const pomo_state_t *s);

int pomo_format_mmss(int seconds, char *buf, size_t n);

/* Times are in seconds on one monotonic clock. */
typedef struct {
    bool valid;
    pomo_state_t state;
    double t_base;
    int rem_base;
} pomo_clock_t;

#define POMO_SRV_DRIFT_PPM 50.0
typedef struct {
    bool valid;
    double lo, hi;   /* server seconds = local seconds + offset, offset in [lo, hi] */
    double t_last;
    int resets;
} pomo_srv_clock_t;

void pomo_srv_clock_init(pomo_srv_clock_t *c);
void pomo_srv_clock_note(pomo_srv_clock_t *c, long long server_now, double sent, double received);
double pomo_srv_clock_offset(const pomo_srv_clock_t *c);
double pomo_secs_left(long long ends_at, double offset, double now);

void pomo_clock_init(pomo_clock_t *c);
void pomo_clock_sync(pomo_clock_t *c, const pomo_state_t *polled, double now);
pomo_state_t pomo_clock_at(const pomo_clock_t *c, double now);
void pomo_clock_sync_end(pomo_clock_t *c, const pomo_state_t *polled, long long ends_at, double offset, double now);

#ifdef __cplusplus
}
#endif
