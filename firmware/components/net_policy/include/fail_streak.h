#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FS_REMIND_MS 60000

typedef enum {
    FS_QUIET,
    FS_STARTED,
    FS_STILL,
    FS_RECOVERED,
} fs_log_t;

typedef struct {
    int fails;
    int64_t first_ms;
    int64_t logged_ms;
    int ended_fails;
    int64_t ended_ms;
} fail_streak_t;

void fs_init(fail_streak_t *s);

fs_log_t fs_note(fail_streak_t *s, bool ok, int64_t now_ms);

static inline int fs_fails(const fail_streak_t *s) { return s->fails; }

#ifdef __cplusplus
}
#endif
