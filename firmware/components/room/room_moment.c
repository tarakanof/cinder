#include "room_moment.h"

#include <math.h>
#include <string.h>

static void forget(rm_trigger_t *tr)
{
    tr->stale = tr->owed = tr->fresh = false;
    tr->stale_at = -INFINITY;
}

void rm_init(rm_trigger_t *tr)
{
    memset(tr, 0, sizeof *tr);
    forget(tr);
    tr->mood_was = tr->bot_mood_was = -1;
    tr->steady_at = -INFINITY;
}

bool rm_tick(rm_trigger_t *tr, const rm_in_t *in, rm_out_t *out)
{
    double t = in->t;
    memset(out, 0, sizeof *out);
    if (in->shown != tr->shown_was || (int)in->mood != tr->mood_was || in->bot_mood != tr->bot_mood_was) {
        tr->shown_was = in->shown;
        tr->mood_was = (int)in->mood;
        tr->bot_mood_was = in->bot_mood;
        tr->steady_at = t;
    }
    if (!in->on) {
        forget(tr);
        return false;
    }
    if (in->valid && !tr->stale && in->aqi >= ROOM_AQI_POOR) {
        tr->stale = true;
        tr->fresh = false;
    } else if (in->valid && tr->stale && in->aqi <= ROOM_AQI_CLEAR) {
        tr->stale = false;
        if (tr->owed) {
            tr->fresh = true;
            tr->fresh_at = t;
        }
        tr->owed = false;
    }
    if (tr->fresh && t - tr->fresh_at > WXM_WAIT_S) tr->fresh = false;
    if (!in->valid || in->blocked || in->weather_first || !in->shown || in->mood == WXM_BUSY || t - tr->steady_at < WXM_SETTLE_S ||
        t - in->last_at < WXM_GAP_S)
        return false;
    if (tr->stale && in->aqi >= ROOM_AQI_POOR && t - tr->stale_at >= WXM_SAME_S) out->kind = WXM_ROOM_STALE;
    else if (tr->fresh && in->aqi <= ROOM_AQI_CLEAR) out->kind = WXM_ROOM_FRESH;
    else return false;
    out->quiet = in->mood == WXM_WORKING;
    out->aqi = in->aqi;
    return true;
}

void rm_shown(rm_trigger_t *tr, const rm_out_t *out, double t)
{
    if (out->kind == WXM_ROOM_STALE) {
        tr->stale_at = t;
        tr->owed = true;
    } else if (out->kind == WXM_ROOM_FRESH) {
        tr->fresh = false;
    }
}
