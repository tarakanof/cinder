#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMBER_HOST_MAX 10
#define EMBER_PROJECT_SLOTS 10
#define EMBER_LABEL_SLOTS 12
#define EMBER_LABEL_MAX 14
#define EMBER_ELLIPSIS "..."

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
    char text[EMBER_LABEL_MAX + 1];
    char sub[EMBER_HOST_MAX + 1];
    int32_t color;   /* 0xRRGGBB; -1 = use the mood colour */
    uint8_t tool;
    uint32_t lead;   /* 0 for nobody */
    int count;       /* waiting sessions */
} ember_host_info_t;

void ember_host_lead_label(const char *source, const char *lead, int hosts, char *out, size_t cap);

/* Printable ASCII of project only (other bytes, so whole UTF-8 sequences, are dropped); "" without a letter or digit.
   At most EMBER_LABEL_SLOTS slots, EMBER_ELLIPSIS counting as one; others > 0 adds " +N" (N capped at 99). */
void ember_host_project_label(const char *project, int others, char *out, size_t cap);

/* Fills text, sub, lead and count from the view's mood; color and tool are left alone. */
void ember_host_lead_info(const char *source, const char *lead, int hosts, const char *project, int waiting,
                          ember_host_info_t *out);

int32_t ember_host_color(const char *hex);

/* A higher count, or another lead at the same count. */
bool ember_host_lead_new(const ember_host_info_t *was, const ember_host_info_t *now);

ember_tool_t ember_host_tool(const char *s);

#ifdef __cplusplus
}
#endif
