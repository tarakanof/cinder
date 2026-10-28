#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PR_TURN_DROP, PR_TURN_PAGE, PR_TURN_DETENT } pr_turn_t;
typedef enum { PR_PRESS_NONE, PR_PRESS_PUSH, PR_PRESS_LONG } pr_press_t;

/* reset_turn: rg_turn consumed the detent. A turn with the button down pages and spends the press. */
pr_turn_t pr_turn(bool reset_turn, bool button_down);

#define PR_HOLD_MS 1500
#define PR_COUNTDOWN_MS 400

/* While held: true once, the moment a press without a turn reaches PR_HOLD_MS; that press is then a long push. */
bool pr_hold_fires(bool down, bool turned_while_down, bool fired, long long held_ms);

/* On release: reset_press from rg_release, hold_seen from BUTTON_LONG_PRESS_HOLD, fired from pr_hold_fires, counting:
   the hold ring was shown (held past PR_COUNTDOWN_MS on a page with a long push). A press already fired is spent;
   hold_seen without fired is the fallback for a frame that came late; a press let go while the ring counted is
   cancelled. */
pr_press_t pr_release(bool reset_press, bool turned_while_down, bool hold_seen, bool fired, bool counting);

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
