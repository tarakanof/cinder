#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USAGE_MAX 8
#define USAGE_ID_MAX 40
#define USAGE_NAME_MAX 40
#define USAGE_MODELS 2
#define USAGE_MODEL_MAX 16
#define USAGE_IDLE_S 20.0
#define USAGE_HINT_PCT 90
#define USAGE_FIVE_H_S (5 * 3600)
#define USAGE_FORECAST_MIN_S (15 * 60)

/* tool_mark's codes; any other tool string is USAGE_TOOL_OTHER (a neutral mark). */
enum { USAGE_TOOL_OTHER = 0, USAGE_TOOL_CLAUDE = 1, USAGE_TOOL_CODEX = 2 };

#define USAGE_GREEN 0x2EE85E
#define USAGE_AMBER 0xFFC14D
#define USAGE_RED 0xFF3A3A
#define USAGE_GREY 0x5A5A5A
#define USAGE_TEXT 0xF4F4F2
#define USAGE_DIM 0x8A8F8B
#define USAGE_TRACK 0x262626
#define USAGE_TRACK2 0x1B1D1C
#define USAGE_DOT_ON 0xFFFFFF
#define USAGE_DOT_OFF 0x4A4A4A

#define USAGE_CX 236.0f
#define USAGE_CY 233.0f
#define USAGE_R5 222.0f
#define USAGE_HW5 6.0f
#define USAGE_R7 200.0f
#define USAGE_HW7 2.5f
#define USAGE_DOT_R 4.0f
#define USAGE_DOT_ARC 182.0f
#define USAGE_DOT_PITCH 16.0f
#define USAGE_HINT_A0 300.0f
#define USAGE_HINT_A1 340.0f

/* Unix seconds; 0 = absent. */
typedef struct {
    bool have;
    int pct;
    long long resets_at, starts_at;
} usage_win_t;

typedef struct {
    char name[USAGE_MODEL_MAX + 1];
    int pct;
} usage_model_t;

typedef struct {
    char id[USAGE_ID_MAX + 1];
    int tool;
    char name[USAGE_NAME_MAX + 1];   /* folded to ASCII */
    usage_win_t five, week;
    int n_models;
    usage_model_t models[USAGE_MODELS];
    bool stale;
    long long updated_at;
} usage_acct_t;

typedef struct {
    int n;
    usage_acct_t a[USAGE_MAX];
} usage_block_t;

struct cJSON;
/* The view's usage block; zeroes out first. An account without a string id, or with one over USAGE_ID_MAX bytes, is skipped; at most USAGE_MAX. */
bool usage_parse(const struct cJSON *o, usage_block_t *out);

/* Index of the highest 5-hour %, the first on a tie; -1 with no accounts. */
int usage_highest(const usage_block_t *b);
/* Any non-stale account's 5-hour % at USAGE_HINT_PCT or more, its window not yet reset at now (server Unix s, 0 unknown). */
bool usage_hint(const usage_block_t *b, double now);
/* a as the knob shows it while Ember is unreachable: stale, updated_at the last view (fed_at, Unix s) unless Ember set one. */
usage_acct_t usage_as_stale(const usage_acct_t *a, long long fed_at);
uint32_t usage_scale_rgb(int pct);

typedef struct {
    bool open;
    int idx;
    char id[USAGE_ID_MAX + 1];
    double since;
} usage_nav_t;

typedef struct {
    int taps, pushes, longs, detents, hswipes, wakes;
} usage_in_t;

typedef enum { USAGE_ACT_NONE, USAGE_ACT_OPEN, USAGE_ACT_STEP, USAGE_ACT_CLOSE } usage_act_t;

/* Closed: a tap with accounts opens the highest, anything else is the bot's (NONE). Open: every input restarts the idle
   time; a tap, push or long push closes, turns and horizontal swipes (> 0 left = next) step with wrap. */
usage_act_t usage_input(usage_nav_t *nav, const usage_block_t *b, const usage_in_t *in, double t);
/* After new data: the shown account by id, else the same slot clamped; no accounts closes. True when it closed. */
bool usage_nav_sync(usage_nav_t *nav, const usage_block_t *b);
/* True when USAGE_IDLE_S passed since the last input and it closed. */
bool usage_nav_tick(usage_nav_t *nav, double t);
void usage_nav_close(usage_nav_t *nav);

typedef struct {
    int tool;
    uint32_t mark_rgb;
    float five_frac, week_frac;
    uint32_t five_rgb, week_rgb, value_rgb;
    bool tick;
    float tick_frac;
    char value[8];
    char reset[48];
    uint32_t reset_rgb;
    char week_line[48];
    char week_short[24]; /* the models by their first two letters, for a week_line too wide for its label; empty without models */
} usage_face_t;

/* now: server Unix seconds (0 unknown). has_offset/utc_offset: weather.utc_offset for clock labels, else relative. */
void usage_face(const usage_acct_t *a, double now, bool has_offset, int utc_offset, usage_face_t *out);
/* "48 min", "1h 48m", "2d 4h"; "<1 min" under a minute. */
void usage_dur(long long s, char *out, size_t cap);

/* Coverage 0..1 of a pixel at (dx, dy) from the centre by an arc of radius r, half width hw, from a0 to a1 degrees
   clockwise from 12 o'clock, round caps; a1 - a0 >= 360 is the whole ring. */
float usage_arc_cover(float dx, float dy, float r, float hw, float a0, float a1);
/* The arc's box, x and w even (the panel rounds areas to 2 px). */
void usage_arc_box(float cx, float cy, float r, float hw, float a0, float a1, int *x, int *y, int *w, int *h);
/* A8 coverage of the arc over the box (stride w). */
void usage_arc_mask(float cx, float cy, float r, float hw, float a0, float a1, int x, int y, int w, int h, uint8_t *out);
/* Dot k of n on the r USAGE_DOT_ARC arc at the bottom, USAGE_DOT_PITCH apart. */
void usage_dot_xy(int k, int n, float *x, float *y);
/* The box of every pixel usage_dots_render can draw or clear (2 to USAGE_MAX dots), x and w even. */
void usage_dots_box(int *x, int *y, int *w, int *h);
/* Each redraws only its own band of the RGB565 w x h face (the rest untouched): the 5-hour ring, the 7-day ring, the n
   dots (none under 2) with the shown one lit, inside usage_dots_box. */
void usage_ring5_render(uint16_t *buf, int w, int h, const usage_face_t *f);
void usage_ring7_render(uint16_t *buf, int w, int h, const usage_face_t *f);
void usage_dots_render(uint16_t *buf, int w, int h, int n, int shown);
/* The whole face background: black, then all three. */
void usage_rings_render(uint16_t *buf, int w, int h, const usage_face_t *f, int n, int shown);

#ifdef __cplusplus
}
#endif
