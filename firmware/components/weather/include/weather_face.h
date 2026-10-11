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
#define WX_SPELLS 4
#define WX_DAYS 3
#define WX_DAY_MAX_S 90000
#define WX_DAY_END_MIN_S 79200
#define WX_DAY_END_MAX_S 93600
#define WX_TWILIGHT 6

typedef struct {
    long long start, end; /* Unix s */
    int intensity;        /* 1..3 */
    int kind;             /* 0 rain, 1 snow or sleet */
} wx_spell_t;

typedef struct {
    long long t;          /* Unix s of the day's local midnight */
    long long end;        /* Unix s of the next local midnight (Ember's end, else the next day's t), 0 unknown */
    signed char face;     /* wx_face_t of the day's condition, never night */
    signed char pop;      /* chance of precipitation, %, -1 unknown */
    short min_c, max_c;   /* whole degrees, min_c <= max_c */
} wx_day_t;

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
    long long observed_at; /* Unix s from the view, 0 unknown */
    bool has_obs_age;
    double obs_age_s;      /* the observation's age when the knob received it */
    bool has_cloud, has_uv, has_today, has_moon;
    int cloud, uv;         /* %, UV index */
    int min_c, max_c;      /* today's range, whole degrees */
    int moon_phase, moon_illum;
    int moon_up;           /* with has_moon: 1 up, 0 down, -1 unknown */
    bool has_now_rain, has_pop, has_utc_offset, has_rain;
    int now_rain;          /* wx_intensity_t of the step containing now */
    int pop;               /* today's chance of precipitation, % */
    int utc_offset;        /* s */
    bool has_wind, has_wind_dir;
    int wind_kmh;          /* now.wind_kmh */
    int wind_dir;          /* now.wind_dir, 0..359, where the wind comes from */
    long long rain_until;  /* Unix s, 0 unknown */
    int rain_n;
    wx_spell_t rain_spell[WX_SPELLS];
    int day_n;
    wx_day_t day[WX_DAYS]; /* Ember's next days after today, t rising */
    long long sunrise, sunset;       /* Unix s, 0 unknown (Ember sends null without a location and in polar day or night) */
    long long twilight[WX_TWILIGHT]; /* Unix s: dawn, blue_end, golden_end, golden_start, blue_start, dusk; 0 none */
    bool has_sun_down, sun_down;     /* Ember's night flag, read even without sun times */
    bool has_aqi;
    int aqi;                         /* European AQI */
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

/* An age in seconds -> "2 h ago"; under 60 s or NaN -> "just now". */
int wx_age_text(double age_s, char *buf, size_t n);
/* observed_at and server_now in Unix seconds; false when either is unknown. Clamped at 0. */
bool wx_obs_age(long long observed_at, double server_now, double *age_s);
/* Offline label: the observation's own age (obs_age_s + age_s) when known, but "stale" when Ember flagged it stale
   and that age is under WX_MAX_AGE_S; without it "stale" when flagged, else the time since Ember confirmed it. */
int wx_age_label(const wx_obs_t *o, char *buf, size_t n);

typedef struct {
    bool have;
    wx_obs_t obs;
    double at_s;
} wx_store_t;

/* obs NULL clears. server_now: Unix s at receipt, 0 unknown. now_s: the knob's monotonic s. */
void wx_store_put(wx_store_t *s, const wx_obs_t *obs, double server_now, double now_s);
/* Fills age_s with now_s minus the last put. */
bool wx_store_get(const wx_store_t *s, double now_s, wx_obs_t *out);
const char *wx_face_name(wx_face_t f);

#ifdef __cplusplus
}
#endif
