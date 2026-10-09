#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_OFFLINE_AFTER_FAILS 3

typedef enum { LINK_ONLINE, LINK_DEGRADED, LINK_OFFLINE } link_level_t;

/* Times are monotonic milliseconds. */
typedef struct {
    link_level_t level;
    int fails;
    int64_t fail_since_ms;
    int64_t offline_since_ms;
} link_state_t;

/* Starts OFFLINE: nothing has been heard from Ember yet. */
void link_state_init(link_state_t *s, int64_t now_ms);
/* One request's outcome. True when the level changed. */
bool link_state_note(link_state_t *s, bool ok, int64_t now_ms);
/* 0 unless OFFLINE. */
int64_t link_state_offline_ms(const link_state_t *s, int64_t now_ms);
const char *link_level_name(link_level_t l);

#ifdef __cplusplus
}
#endif
