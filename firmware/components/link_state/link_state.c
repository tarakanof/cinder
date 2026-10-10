#include "link_state.h"

#include <limits.h>
#include <string.h>

link_outcome_t link_counts(link_level_t level, bool wifi, bool aborted, int status, bool parsed)
{
    if (aborted) return LINK_SKIP;
    if (!wifi) return level == LINK_BOOT ? LINK_SKIP : LINK_FAIL;
    if (status == 200 || status == 304) return parsed ? LINK_OK : LINK_FAIL;
    if (status == -1 || (status >= 300 && status <= 399) || (status >= 500 && status <= 599)) return LINK_FAIL;
    return LINK_SKIP;
}

link_outcome_t link_counts_body(link_level_t level, bool wifi, bool aborted, int status, bool parsed, int body_len,
                                int cap)
{
    if (wifi && !aborted && status == 200 && !parsed && body_len > cap - 1) return LINK_SKIP;
    return link_counts(level, wifi, aborted, status, parsed);
}

void link_state_init(link_state_t *s, int64_t now_ms)
{
    memset(s, 0, sizeof *s);
    s->level = LINK_BOOT;
    s->fail_since_ms = s->offline_since_ms = now_ms;
}

bool link_state_note(link_state_t *s, link_outcome_t o, int64_t now_ms)
{
    link_level_t was = s->level;
    if (o == LINK_SKIP) return false;
    if (o == LINK_OK) {
        s->fails = 0;
        s->level = LINK_ONLINE;
        return was != s->level;
    }
    if (s->fails == 0) s->fail_since_ms = now_ms;
    if (s->fails < INT_MAX) s->fails++;
    if (s->fails >= LINK_OFFLINE_AFTER_FAILS) s->level = LINK_OFFLINE;
    else if (was != LINK_BOOT) s->level = LINK_DEGRADED;
    if (s->level == LINK_OFFLINE && was != LINK_OFFLINE) {
        s->offline_since_ms = s->fail_since_ms;
        s->gen = (s->gen + 1) & 0x3FFFFFFFu;
    }
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
    static const char *const N[] = {"boot", "online", "degraded", "offline"};
    return (unsigned)l < sizeof N / sizeof N[0] ? N[l] : "offline";
}

uint32_t link_state_word(const link_state_t *s) { return (s->gen << 2) | ((uint32_t)s->level & 3u); }

bool link_word_offline(uint32_t w) { return (w & 3u) == LINK_OFFLINE; }

bool link_press_ok(uint32_t at, uint32_t now)
{
    return !link_word_offline(at) && !link_word_offline(now) && (at >> 2) == (now >> 2);
}
