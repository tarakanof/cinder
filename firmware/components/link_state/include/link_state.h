#pragma once

#include <stdbool.h>
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
} link_state_t;

/* status: HTTP status, -1 transport error (connect, DNS, timeout, reset). parsed: the body of a 200 was understood. */
link_outcome_t link_counts(link_level_t level, bool wifi, bool aborted, int status, bool parsed);
/* As link_counts, but an unparsed 200 whose body_len (bytes sent) did not fit cap (NUL included) is a skip. */
link_outcome_t link_counts_body(link_level_t level, bool wifi, bool aborted, int status, bool parsed, int body_len,
                                int cap);

/* Starts in BOOT: not offline until the first answer or LINK_OFFLINE_AFTER_FAILS failures. */
void link_state_init(link_state_t *s, int64_t now_ms);
/* True when the level changed. */
bool link_state_note(link_state_t *s, link_outcome_t o, int64_t now_ms);
/* 0 unless OFFLINE. */
int64_t link_state_offline_ms(const link_state_t *s, int64_t now_ms);
const char *link_level_name(link_level_t l);

/* Level and gen in one word, for one atomic load on another task. Zero is BOOT, gen 0. */
uint32_t link_state_word(const link_state_t *s);
bool link_word_offline(uint32_t w);
/* A press stamped with word `at` may be sent at `now`: neither is OFFLINE and no OFFLINE came between. */
bool link_press_ok(uint32_t at, uint32_t now);

#ifdef __cplusplus
}
#endif
