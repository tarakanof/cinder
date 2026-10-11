#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "weather_face.h"
#include "wx_rain.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_FACE 466
#define WX_FACE_C 233.0f
#define WX_RING_R 222.0f
#define WX_RING_HW 6.0f
#define WX_ARRIVE_S 1.32
#define WX_SCALE_LO (-20)
#define WX_SCALE_HI 40
#define WX_COUNT_MAX 30
#define WX_MERC_BOTTOM 184.0f
#define WX_THERMO_DY (-14.0f)
#define WX_SHIFT_S 120.0
#define WX_SHIFT_PX 2
#define WX_BAND_HYST 5
#define WX_ILLUM_STEP 5

typedef enum { WX_HERO_NONE, WX_HERO_SUN, WX_HERO_MOON, WX_HERO_MOON_SMALL } wx_hero_t;
typedef enum { WX_TONE_WHITE, WX_TONE_LIGHT, WX_TONE_DARK, WX_TONE_NIGHT } wx_tone_t;

typedef struct {
    float x, y, r;
} wx_circle_t;

typedef struct {
    bool back;
    float base;
    int n;
    wx_circle_t c[4];
} wx_cloud_t;

typedef struct {
    int n;
    wx_tone_t tone;
    float fade_from;
    wx_cloud_t cl[4];
} wx_clouds_t;

typedef struct {
    bool have;
    bool still;
    int value;
    bool has_range;
    int lo, hi;
    float frac;
    bool night;
    int cloud_band; /* 0 none, 1 one cloud, 2 two, 3 light grey, 4 overcast */
    wx_hero_t hero;
    int moon_phase, moon_illum;
    bool stars;
    char sub[24];
    uint32_t sub_rgb;
    wx_rain_t rain; /* rain.on: the Rain now face instead */
} wx_temp_t;

uint32_t wx_temp_rgb(float c);
uint32_t wx_uv_rgb(int uv);
/* NULL below UV 3. */
const char *wx_uv_advice(int uv);

enum { WX_BAKE_RING = 1, WX_BAKE_HERO = 2, WX_BAKE_SCENE = 4, WX_BAKE_FULL = 7 };

/* obs NULL or without a temperature: have false. */
void wx_temp_from(const wx_obs_t *obs, wx_temp_t *out);
/* shown: the face on screen or NULL; a cloud cover within WX_BAND_HYST % of its band keeps that band. */
void wx_temp_from_shown(const wx_obs_t *obs, const wx_temp_t *shown, wx_temp_t *out);
/* WX_BAKE_* parts that differ from shown: RING the range, HERO the hero sprite, SCENE background and clouds (or the rain
   layers); 0 none. A change between the faces is FULL. */
int wx_temp_bake_kind(const wx_temp_t *t, const wx_temp_t *shown);
bool wx_temp_same_bake(const wx_temp_t *a, const wx_temp_t *b);

/* Count-up: n steps (0..n), value of step k, step shown t s after the page showed. */
int wx_count_n(int v);
int wx_count_value(int v, int k);
int wx_count_step(int n, double t);
/* Ease-out over the arrival: 0 at t 0, 1 from WX_ARRIVE_S. */
double wx_arrive(double t);

/* Gauge point at frac 0..1 (225 to 495 degrees clockwise from 12 o'clock), face coordinates. */
void wx_gauge_xy(float frac, float *x, float *y);
/* Mercury top for a temperature, before WX_THERMO_DY. */
float wx_merc_top(float c);

void wx_clouds_for(int band, bool night, wx_clouds_t *out);
int wx_cloud_band(int cloud);
/* shown: the band on screen or -1. */
int wx_cloud_band_keep(int cloud, int shown);

typedef struct {
    double t;
    int i;
} wx_shift_t;

/* Burn-in: after WX_SHIFT_S s on screen the face steps to the next corner of a WX_SHIFT_PX square; true on a step. A non-finite or negative dt counts as 0. */
bool wx_shift_tick(wx_shift_t *s, double dt);
void wx_shift_xy(const wx_shift_t *s, int *dx, int *dy);

#ifdef __cplusplus
}
#endif
