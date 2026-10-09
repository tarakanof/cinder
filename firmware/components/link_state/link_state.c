#include "link_state.h"

#include <limits.h>

void link_state_init(link_state_t *s, int64_t now_ms)
{
    s->level = LINK_OFFLINE;
    s->fails = LINK_OFFLINE_AFTER_FAILS;
    s->fail_since_ms = now_ms;
    s->offline_since_ms = now_ms;
}

bool link_state_note(link_state_t *s, bool ok, int64_t now_ms)
{
    link_level_t was = s->level;
    if (ok) {
        s->fails = 0;
        s->level = LINK_ONLINE;
        return was != s->level;
    }
    if (s->fails == 0) s->fail_since_ms = now_ms;
    if (s->fails < INT_MAX) s->fails++;
    s->level = s->fails >= LINK_OFFLINE_AFTER_FAILS ? LINK_OFFLINE : LINK_DEGRADED;
    if (s->level == LINK_OFFLINE && was != LINK_OFFLINE) s->offline_since_ms = s->fail_since_ms;
    return was != s->level;
}

int64_t link_state_offline_ms(const link_state_t *s, int64_t now_ms)
{
    if (s->level != LINK_OFFLINE) return 0;
    int64_t d = now_ms - s->offline_since_ms;
    return d > 0 ? d : 0;
}

const char *link_level_name(link_level_t l)
{
    static const char *const N[] = {"online", "degraded", "offline"};
    return (unsigned)l < sizeof N / sizeof N[0] ? N[l] : "offline";
}
