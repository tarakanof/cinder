#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WX_CLEAR_DAY,
    WX_CLEAR_NIGHT,
    WX_PARTLY_CLOUDY,
    WX_OVERCAST,
    WX_FOG,
    WX_RAIN,
    WX_SNOW,
    WX_STORM,
    WX_FACE_COUNT
} wx_face_t;

typedef enum { WX_INT_NONE, WX_INT_LIGHT, WX_INT_MODERATE, WX_INT_HEAVY } wx_intensity_t;

#define WX_MAX_AGE_S 1800.0

typedef struct {
    bool valid;
    bool enabled;
    bool stale;
    bool severe;
    bool has_temp;
    float temp_c;
    char provider[16];
    char condition[12];
    char code[40];
    int now_min;           /* minute of day, -1 unknown: generated_at; sunrise, sunset */
    int rise_min, set_min;
    bool has_night;
    bool night;
    double age_s;
} wx_obs_t;

typedef struct {
    wx_face_t face;
    wx_intensity_t intensity;
    bool night;
    bool rime;
    bool severe;
    bool still;
} wx_look_t;

int wx_minute_of_day(const char *iso8601);

int wx_is_night(int now_min, int rise_min, int set_min);

wx_look_t wx_look_from(const char *provider, const char *condition, const char *code, int night);

wx_look_t wx_look_from_obs(const wx_obs_t *obs);

bool wx_look_equal(const wx_look_t *a, const wx_look_t *b);

/* Seconds since the observation was last confirmed by Ember -> "2 h ago". */
int wx_age_text(double age_s, char *buf, size_t n);
/* Offline label: "stale" when Ember flagged the observation stale, else its age. The view carries no observation
   time, so the age is the time since Ember last confirmed it (age_s). */
int wx_age_label(const wx_obs_t *o, char *buf, size_t n);
const char *wx_face_name(wx_face_t f);

#ifdef __cplusplus
}
#endif
