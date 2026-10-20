#include "press_route.h"

pr_turn_t pr_turn(bool reset_turn, bool button_down)
{
    if (reset_turn) return PR_TURN_DROP;
    return button_down ? PR_TURN_PAGE : PR_TURN_DETENT;
}

pr_press_t pr_release(bool reset_press, bool turned_while_down, bool hold_seen)
{
    if (reset_press || turned_while_down) return PR_PRESS_NONE;
    return hold_seen ? PR_PRESS_LONG : PR_PRESS_PUSH;
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
