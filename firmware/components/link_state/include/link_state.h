#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_OFFLINE_AFTER_FAILS 3

typedef enum { LINK_BOOT, LINK_ONLINE, LINK_DEGRADED, LINK_OFFLINE } link_level_t;

typedef enum { LINK_OK, LINK_FAIL, LINK_SKIP } link_outcome_t;

/* Times are monotonic milliseconds. gen counts entries into OFFLINE. */
typedef struct {
    link_level_t level;
    int fails;
    uint32_t gen;
    int64_t fail_since_ms;
    int64_t offline_since_ms;
    int64_t ok_ms;
} link_state_t;

/* status: HTTP status, -1 transport error (connect, DNS, timeout, reset). parsed: the body of a 200 was understood. */
link_outcome_t link_counts(link_level_t level, bool wifi, bool aborted, int status, bool parsed);

/* Starts in BOOT: not offline until the first answer or LINK_OFFLINE_AFTER_FAILS failures. */
void link_state_init(link_state_t *s, int64_t now_ms);
/* True when the level changed. */
bool link_state_note(link_state_t *s, link_outcome_t o, int64_t now_ms);
/* 0 unless OFFLINE. */
int64_t link_state_offline_ms(const link_state_t *s, int64_t now_ms);
const char *link_level_name(link_level_t l);

/* Time of the last LINK_OK; 0 when there has not been one (BOOT). */
int64_t link_state_ok_ms(const link_state_t *s);
/* The age of an answer at ok_ms, worded like the weather page ("just now", "5 min ago", "2 h ago", "3 d ago").
   Empty string and 0 when ok_ms <= 0 (never answered). Returns the full length like snprintf. */
int link_age_text(int64_t ok_ms, int64_t now_ms, char *buf, size_t n);

/* Level and gen in one word, for one atomic load on another task. Zero is BOOT, gen 0. */
uint32_t link_state_word(const link_state_t *s);
bool link_word_offline(uint32_t w);
/* A press stamped with word `at` may be sent at `now`: neither is OFFLINE and no OFFLINE came between. */
bool link_press_ok(uint32_t at, uint32_t now);

#ifdef __cplusplus
}
#endif
