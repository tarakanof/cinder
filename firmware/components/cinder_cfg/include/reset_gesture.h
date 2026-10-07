#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RG_HOLD_S 10.0
#define RG_DETENTS 24
#define RG_TIMEOUT_S 5.0

typedef enum { RG_IDLE, RG_ARMED, RG_CONFIRMED } rg_state_t;

/* Not thread-safe: the caller serialises calls (input callbacks and the LVGL task). */
typedef struct {
    rg_state_t state;
    bool down;
    bool turned;
    bool consumed;
    double down_at;
    double active_at;
    int progress;
} reset_gesture_t;

void rg_init(reset_gesture_t *g);
void rg_press(reset_gesture_t *g, double t);

bool rg_release(reset_gesture_t *g, double t);

bool rg_turn(reset_gesture_t *g, int dir, double t);

rg_state_t rg_tick(reset_gesture_t *g, double t);

#ifdef __cplusplus
}
#endif
