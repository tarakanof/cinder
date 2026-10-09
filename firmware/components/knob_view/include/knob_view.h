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

/* BAD (not an object, or v not a whole number) zeroes *out. TOO_OLD/TOO_NEW: v below/above [KNOB_VIEW_V_MIN, KNOB_VIEW_V_MAX]; *out is untouched and no other key is read. major gets v when it is a number; may be NULL. */
knob_view_res_t knob_view_read(const char *json, knob_view_t *out, int *major);
/* knob_view_read(json, out, NULL) == KNOB_VIEW_OK. */
bool knob_view_parse(const char *json, knob_view_t *out);

bot_mood_t knob_view_mood_from(int waiting, int errors, int running, int done);
bot_mood_t knob_view_mood(const knob_view_t *v);

typedef enum { KNOB_QUIET_VIEW, KNOB_QUIET_LEGACY } knob_quiet_src_t;

bool knob_quiet_next(bool cur, knob_quiet_src_t src, bool view_quiet);

void knob_view_epoch_key(const knob_view_t *v, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
