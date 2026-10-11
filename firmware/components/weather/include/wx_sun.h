#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_face.h"
#include "wx_rain.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_SUN_CX 233
#define WX_SUN_CY 178
#define WX_SUN_R 42.0f
#define WX_SUN_GLOW 74.0f
#define WX_SUN_CLIP_Y 194
#define WX_SUN_HZ_Y 196
#define WX_SUN_RISE_PX 64
#define WX_SUN_FPS 15
#define WX_SUN_RAYS_ON_S 0.72
#define WX_SUN_RAYS_IN_S 1.44
#define WX_SUN_RAYS 13
#define WX_SUN_RAYS_X0 68
#define WX_SUN_RAYS_Y0 38
#define WX_SUN_RAYS_X1 398
#define WX_SUN_BLEND_S 1020
#define WX_SUN_SPAN_MAX_S 43200
#define WX_SUN_VALUE_RGB 0xE0A030
#define WX_SUN_GOLD_RGB 0xFFB547
#define WX_SUN_HZN_RGB 0xFF9A3D
#define WX_SUN_UNIT_RGB 0x9A9A9A
#define WX_SUN_GREY_RGB 0xBDBDBD

/* The day's crossings in time order: Ember's twilight around its sunrise and sunset. */
enum { WX_SUN_DAWN, WX_SUN_BLUE_END, WX_SUN_RISE, WX_SUN_GOLD_END, WX_SUN_GOLD_START, WX_SUN_SET, WX_SUN_BLUE_START, WX_SUN_DUSK, WX_SUN_N };

typedef enum { WX_SUN_BEFORE, WX_SUN_MORNING, WX_SUN_DAY, WX_SUN_EVENING, WX_SUN_AFTER } wx_sun_phase_t;
typedef enum { WX_SUN_NOT_POLAR, WX_SUN_POLAR_DAY, WX_SUN_POLAR_NIGHT } wx_sun_polar_t;

typedef struct {
    bool on;          /* the Sun face */
    bool have;        /* sunrise, sunset and the offset: the gauge */
    int polar;        /* wx_sun_polar_t, without sun times: a location known (moon.up) and Ember's night flag */
    long long t[WX_SUN_N]; /* the crossings moved to the knob's local day, Unix s; 0 none; sunrise and sunset with have */
    int utc_offset;
    bool has_now;     /* the knob's clock known: marker and value */
    int phase;        /* wx_sun_phase_t */
    float frac;       /* now on the gauge, 0..1 */
    bool hours;       /* the value in half hours, else in minutes */
    int count;
    const char *unit;
    char rise[8], set[8]; /* "07:30" */
    char sub[32];
    uint32_t sub_rgb;
} wx_sun_t;

/* Fills s from the observation and the knob's clock; the Sun is never the auto face. */
void wx_sun_from(const wx_obs_t *o, wx_sun_t *s);
/* Same picture: have, polar, the crossings and the offset (the ring, the ticks and the end labels). */
bool wx_sun_same_scene(const wx_sun_t *a, const wx_sun_t *b);
/* The gauge's ends: the first and the last crossing known. */
long long wx_sun_start(const wx_sun_t *s);
long long wx_sun_end(const wx_sun_t *s);
/* Where a time sits on the gauge, 0..1 (clamped). */
float wx_sun_frac(const wx_sun_t *s, long long t);
/* The ring colour at a time: blue hour, the sunrise blush, golden hour, day; the evening mirrors the morning. */
uint32_t wx_sun_rgb(const wx_sun_t *s, long long t);
/* s seconds as the value: from an hour in half hours (floored), below it in whole minutes (rounded up, at least 1). */
void wx_sun_round(long long s, bool *hours, int *count);
/* The value at count-up step k into buf; returns the unit, or NULL with "--" (no clock or no sun times). */
const char *wx_sun_value(const wx_sun_t *s, int k, char *buf, size_t n);
int wx_sun_count_n(const wx_sun_t *s);

typedef struct {
    int dy;       /* the sun sprite, px below its rest */
    int rays_opa; /* 0..255 */
} wx_sun_frame_t;

/* The arrival t s after the face showed: the sun rises WX_SUN_RISE_PX on the arrival's ease, the rays fade in at
   WX_SUN_FPS from WX_SUN_RAYS_ON_S to WX_SUN_RAYS_IN_S. Still after that. */
void wx_sun_frame(double t, wx_sun_frame_t *f);

typedef struct {
    wx_box_t rays, sun; /* face boxes: the A8 fan, the RGB565A8 sun (its clip: nothing below WX_SUN_CLIP_Y) */
    int rays_at, sun_at, total; /* byte offsets in the still-cloud buffer */
} wx_sun_layout_t;

void wx_sun_layout(wx_sun_layout_t *l);

#ifdef __cplusplus
}
#endif
