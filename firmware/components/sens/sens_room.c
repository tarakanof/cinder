#include "sens_room.h"

#include <math.h>
#include <string.h>

void room_presence_init(room_presence_t *p, uint32_t on_ms, uint32_t off_ms, int64_t now_ms)
{
    memset(p, 0, sizeof *p);
    p->on_ms = on_ms;
    p->off_ms = off_ms;
    p->raw_since_ms = now_ms;
}

bool room_presence_feed(room_presence_t *p, bool raw, int64_t now_ms)
{
    if (raw != p->raw) {
        p->raw = raw;
        p->raw_since_ms = now_ms;
    }
    if (p->raw == p->present) return false;
    int64_t held = now_ms - p->raw_since_ms;
    if (held < (int64_t)(p->raw ? p->on_ms : p->off_ms)) return false;
    p->present = p->raw;
    return true;
}

bool room_comp_due(const room_comp_t *c, float temp_c, float rh_pct, int64_t now_ms)
{
    if (!c->sent) return true;
    if (now_ms - c->at_ms >= ROOM_COMP_MAX_AGE_MS) return true;
    return fabsf(temp_c - c->temp_c) >= ROOM_COMP_TEMP_STEP || fabsf(rh_pct - c->rh_pct) >= ROOM_COMP_RH_STEP;
}

void room_comp_sent(room_comp_t *c, float temp_c, float rh_pct, int64_t now_ms)
{
    c->temp_c = temp_c;
    c->rh_pct = rh_pct;
    c->at_ms = now_ms;
    c->sent = true;
}
