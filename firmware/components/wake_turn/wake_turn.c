#include "wake_turn.h"

bool wake_turn(const wake_ops_t *o, int ms, void *item)
{
    if (o->take(o->ctx, 0, item)) return true;
    int woke = o->wait(o->ctx, ms);
    if (woke < 0) return o->take(o->ctx, ms, item);
    return woke > 0 && o->take(o->ctx, 0, item);
}

int wake_slice_ms(int left_ms, int due_ms, bool np_visible)
{
    int ms = left_ms;
    if (due_ms > 0 && ms > due_ms) ms = due_ms;
    if (ms > WAKE_SLICE_MAX_MS) ms = WAKE_SLICE_MAX_MS;
    if (np_visible && ms > WAKE_SLICE_NP_MS) ms = WAKE_SLICE_NP_MS;
    return ms;
}
