#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMBER_HOST_MAX 10

typedef struct {
    const char *source;
    const char *state;
    const char *updated_at;
} ember_host_session_t;

int ember_host_state_priority(const char *state);

bool ember_host_parse_time(const char *s, int64_t *sec, int32_t *nsec);

int ember_host_pick_winning(const ember_host_session_t *s, int n);

/* Label functions: out is "" for no host; cap must be > 0. */
void ember_host_label(const char *render_source, bool has_render_source, const ember_host_session_t *s, int n,
                      char *out, size_t cap);

typedef enum { EMBER_TOOL_NONE = 0, EMBER_TOOL_CLAUDE, EMBER_TOOL_CODEX, EMBER_TOOL_T3 } ember_tool_t;

typedef struct {
    char text[EMBER_HOST_MAX + 1];
    int32_t color;   /* 0xRRGGBB; -1 = use the mood colour */
    uint8_t tool;
} ember_host_info_t;

void ember_host_lead_label(const char *source, const char *lead, int hosts, char *out, size_t cap);

int32_t ember_host_color(const char *hex);

ember_tool_t ember_host_tool(const char *s);

#ifdef __cplusplus
}
#endif
