#include "reset_gesture.h"

#include <string.h>

void rg_init(reset_gesture_t *g) { memset(g, 0, sizeof *g); }

static void cancel(reset_gesture_t *g)
{
    if (g->state == RG_ARMED) g->state = RG_IDLE;
    g->progress = 0;
}

void rg_press(reset_gesture_t *g, double t)
{
    if (g->state == RG_CONFIRMED) return;
    g->down = true;
    g->turned = false;
    g->consumed = false;
    g->down_at = t;
}

bool rg_release(reset_gesture_t *g, double t)
{
    (void)t;
    g->down = false;
    if (g->state == RG_CONFIRMED) return true;
    cancel(g);
    bool consumed = g->consumed;
    g->consumed = false;
    return consumed;
}

bool rg_turn(reset_gesture_t *g, int dir, double t)
{
    if (g->state == RG_CONFIRMED) return true;
    if (g->state == RG_ARMED) {
        if (dir > 0) {
            g->active_at = t;
            if (++g->progress >= RG_DETENTS) g->state = RG_CONFIRMED;
        } else {
            cancel(g);
        }
        return true;
    }
    if (g->consumed) return true;
    if (g->down) g->turned = true;
    return false;
}

rg_state_t rg_tick(reset_gesture_t *g, double t)
{
    if (g->state == RG_IDLE && g->down && !g->turned && !g->consumed && t - g->down_at >= RG_HOLD_S) {
        g->state = RG_ARMED;
        g->consumed = true;
        g->progress = 0;
        g->active_at = t;
    } else if (g->state == RG_ARMED && t - g->active_at >= RG_TIMEOUT_S) {
        cancel(g);
    }
    return g->state;
}
