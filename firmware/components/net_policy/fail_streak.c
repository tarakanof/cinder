#include "fail_streak.h"

#include <string.h>

void fs_init(fail_streak_t *s) { memset(s, 0, sizeof *s); }

fs_log_t fs_note(fail_streak_t *s, bool ok, int64_t now_ms)
{
    if (ok) {
        if (s->fails == 0) return FS_QUIET;
        s->ended_fails = s->fails;
        s->ended_ms = now_ms - s->first_ms;
        s->fails = 0;
        return FS_RECOVERED;
    }
    if (s->fails++ == 0) {
        s->first_ms = s->logged_ms = now_ms;
        return FS_STARTED;
    }
    if (now_ms - s->logged_ms >= FS_REMIND_MS) {
        s->logged_ms = now_ms;
        return FS_STILL;
    }
    return FS_QUIET;
}
