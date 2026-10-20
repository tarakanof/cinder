#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PR_TURN_DROP, PR_TURN_PAGE, PR_TURN_DETENT } pr_turn_t;
typedef enum { PR_PRESS_NONE, PR_PRESS_PUSH, PR_PRESS_LONG } pr_press_t;

/* reset_turn: rg_turn consumed the detent. A turn with the button down pages and spends the press. */
pr_turn_t pr_turn(bool reset_turn, bool button_down);

/* On release only: reset_press from rg_release, hold_seen from BUTTON_LONG_PRESS_HOLD. */
pr_press_t pr_release(bool reset_press, bool turned_while_down, bool hold_seen);

typedef struct {
    volatile bool on;
} pr_wake_t;

/* One event of a press or touch: start opens a gesture, end closes it, woke is the idle clock's answer for this event.
   True when the event is the waking input or part of the gesture it woke, and must not be delivered. */
bool pr_wake_swallow(pr_wake_t *w, bool start, bool end, bool woke);

/* A gesture the waking input opened is still down: turns during it are dropped too. */
bool pr_wake_held(const pr_wake_t *w);

#ifdef __cplusplus
}
#endif
