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

typedef struct {
    int v;
    unsigned long long epoch;
    unsigned long config_version;

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

bool knob_view_parse(const char *json, knob_view_t *out);

bot_mood_t knob_view_mood_from(int waiting, int errors, int running, int done);
bot_mood_t knob_view_mood(const knob_view_t *v);

void knob_view_epoch_key(const knob_view_t *v, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
