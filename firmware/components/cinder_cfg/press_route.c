#include "press_route.h"

pr_turn_t pr_turn(bool reset_turn, bool button_down)
{
    if (reset_turn) return PR_TURN_DROP;
    return button_down ? PR_TURN_PAGE : PR_TURN_DETENT;
}

bool pr_hold_fires(bool down, bool turned_while_down, bool fired, long long held_ms)
{
    return down && !turned_while_down && !fired && held_ms >= PR_HOLD_MS;
}

pr_press_t pr_release(bool reset_press, bool turned_while_down, bool hold_seen, bool fired, bool counting)
{
    if (reset_press || turned_while_down || fired) return PR_PRESS_NONE;
    if (hold_seen) return PR_PRESS_LONG;
    return counting ? PR_PRESS_NONE : PR_PRESS_PUSH;
}

bool pr_wake_swallow(pr_wake_t *w, bool start, bool end, bool woke)
{
    if (start) w->on = woke;
    else if (woke) return true;
    if (!w->on) return false;
    if (end) w->on = false;
    return true;
}

bool pr_wake_held(const pr_wake_t *w) { return w->on; }
