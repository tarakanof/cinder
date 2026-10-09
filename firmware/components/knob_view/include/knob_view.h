#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bot_behavior.h"
#include "np.h"
#include "pomo.h"
#include "weather_face.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KNOB_VIEW_SOURCE_MAX 64
#define KNOB_VIEW_V_MIN 1
#define KNOB_VIEW_V_MAX 1
#define KNOB_VIEW_BUF (16 * 1024) /* view response buffer, NUL included */

typedef struct {
    int v;
    unsigned long long epoch;
    unsigned long config_version;

    bool has_mood;
    int waiting, errors, running, done;
    char source[KNOB_VIEW_SOURCE_MAX + 1];
    char lead[KNOB_VIEW_SOURCE_MAX + 1];
    int hosts;
    char lead_color[16];
    char tool[16];

    bool has_pomo;
    pomo_state_t pomo;
    bool pomo_counting;
    /* Server Unix seconds; set while counting, then pomo.remaining_sec is 0. */
    long long ends_at;

    bool has_weather;
    wx_obs_t weather;

    bool has_brightness;
    int level;
    bool bright_night;

    bool quiet;

    bool has_np;
    np_info_t np;

    long long diag_live_until;   /* server Unix s; 0 = absent */
} knob_view_t;

typedef enum { KNOB_VIEW_OK, KNOB_VIEW_BAD, KNOB_VIEW_TOO_OLD, KNOB_VIEW_TOO_NEW } knob_view_res_t;

/* BAD (not an object; v missing, not a number or not a whole number) zeroes *out. TOO_OLD/TOO_NEW: whole v below/above [KNOB_VIEW_V_MIN, KNOB_VIEW_V_MAX]; *out is untouched and no other key is read. major gets v (clamped to +-1e9) unless BAD; may be NULL. */
knob_view_res_t knob_view_read(const char *json, knob_view_t *out, int *major);
/* knob_view_read(json, out, NULL) == KNOB_VIEW_OK. */
bool knob_view_parse(const char *json, knob_view_t *out);

typedef enum { KNOB_COMPAT_OK, KNOB_COMPAT_UPDATE_EMBER, KNOB_COMPAT_UPDATE_KNOB } knob_compat_t;

/* One poller's view state. view is the last good view; unsupported: the last answer had a major outside the range. */
typedef struct {
    knob_view_t view;
    bool have_view;
    bool unsupported;
    knob_compat_t compat;
    int major;
} knob_view_state_t;

/* APPLY: feed the pages from view. RESTORE: only mood and host label from the kept view, nothing else fed. */
typedef enum { KNOB_STEP_NONE, KNOB_STEP_APPLY, KNOB_STEP_RESTORE, KNOB_STEP_REFETCH, KNOB_STEP_UNPARSED } knob_step_act_t;

typedef struct {
    knob_step_act_t act;
    int mood; /* bot_mood_t of the kept view for APPLY and RESTORE when it has a mood block, else -1 */
    bool etag_set, etag_clear;
} knob_step_t;

/* Send If-None-Match with the stored ETag. */
bool knob_view_conditional(const knob_view_state_t *s);
/* status: HTTP status of the view GET (-1 transport error); body: the 200 body. */
knob_step_t knob_view_step(knob_view_state_t *s, int status, const char *body);
/* Token change or legacy fallback: forget the view and the compat state. */
void knob_view_state_reset(knob_view_state_t *s);

bot_mood_t knob_view_mood_from(int waiting, int errors, int running, int done);
bot_mood_t knob_view_mood(const knob_view_t *v);

typedef enum { KNOB_QUIET_VIEW, KNOB_QUIET_LEGACY } knob_quiet_src_t;

bool knob_quiet_next(bool cur, knob_quiet_src_t src, bool view_quiet);

void knob_view_epoch_key(const knob_view_t *v, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
