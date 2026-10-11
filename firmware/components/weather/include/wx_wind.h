#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "weather_face.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_WIND_FPS 15
#define WX_WIND_LEVELS 3
#define WX_WIND_POSES 3
#define WX_WIND_QUADS 5
#define WX_WIND_ROWS 4
#define WX_WIND_LEAVES 2
#define WX_WIND_GAUGE_KMH 50
#define WX_WIND_MAX_KMH 999
#define WX_WIND_HYST 3
#define WX_WIND_AUTO_KMH 39
#define WX_WIND_KEEP_KMH 34
#define WX_WIND_BASE_Y 205.0f
#define WX_WIND_ARM 22.0f
#define WX_WIND_POLE_TOP 26.0f
#define WX_WIND_POLE_RGB 0x9AA4B0
#define WX_WIND_STREAK_RGB 0x9AA4B0
#define WX_WIND_LEAF_RGB 0x8ACF7A
#define WX_WIND_UNIT_RGB 0x9A9A9A
#define WX_STREAK_PAD 3
#define WX_STREAK_H 6
#define WX_LEAF_BOX 20
#define WX_LEAF_ROTS 12

typedef struct {
    bool on;   /* the Wind face */
    bool have; /* fresh data with now.wind_kmh */
    int kmh;   /* 0..WX_WIND_MAX_KMH */
    bool has_dir;
    int dir;   /* 0..359, where the wind comes from */
    int level; /* 0 limp, 1 gentle, 2 straight out */
    float frac;
    char sub[40]; /* LVGL recolour markup */
} wx_wind_t;

/* Beaufort band colour and name of a speed in km/h (Calm only below 1). */
uint32_t wx_wind_rgb(int kmh);
const char *wx_wind_band(int kmh);
/* The sock's darker stripes: each channel at 55 %. */
uint32_t wx_wind_dark(uint32_t rgb);
/* 8-point compass for a direction in degrees, any integer (wraps). */
const char *wx_wind_compass(int dir);
int wx_wind_level(int kmh);
/* shown: the level on screen or -1; a speed within WX_WIND_HYST km/h past a level edge keeps it. */
int wx_wind_level_keep(int kmh, int shown);
/* shown: the wind on screen or NULL (its level is kept by wx_wind_level_keep). */
void wx_wind_from(const wx_obs_t *o, const wx_wind_t *shown, wx_wind_t *w);
/* The auto rule: fresh data and a sustained wind of WX_WIND_AUTO_KMH (WX_WIND_KEEP_KMH while shown). */
bool wx_wind_strong(const wx_obs_t *o, bool shown);
bool wx_wind_same_scene(const wx_wind_t *a, const wx_wind_t *b);

typedef struct {
    float x, y;
} wx_pt_t;

typedef struct {
    float dy, x0, len, period, phase; /* from the hoop; phase: the canvas's animation delay, s */
    int steps;                        /* per half period */
} wx_wind_row_t;

typedef struct {
    float dy, period, phase;
} wx_wind_leaf_t;

typedef struct {
    wx_pt_t mount; /* the hoop, face coordinates */
    float droop;   /* degrees below horizontal */
    int pose_frames;
    float ang, stretch; /* pose A: rotate -ang, scale (1 - stretch, 1.02); C: +ang, (1 + stretch, 0.96) */
    wx_pt_t quad[WX_WIND_QUADS][4];
    int nrows, nleaves;
    wx_wind_row_t row[WX_WIND_ROWS];
    wx_wind_leaf_t leaf[WX_WIND_LEAVES];
} wx_wind_geo_t;

const wx_wind_geo_t *wx_wind_geo(int level);
/* Quad q of pose p (0 A, 1 B, 2 C) in face coordinates. */
void wx_wind_quad(int level, int pose, int q, wx_pt_t out[4]);
/* Face x of streak row r's sprite before its frame offset. */
int wx_wind_streak_x(int level, int r);
int wx_wind_streak_y(int level, int r);

typedef struct {
    int pose; /* 0 A, 1 B, 2 C */
    int streak_dx[WX_WIND_ROWS], streak_opa[WX_WIND_ROWS];
    int leaf_x[WX_WIND_LEAVES], leaf_y[WX_WIND_LEAVES], leaf_rot[WX_WIND_LEAVES], leaf_opa[WX_WIND_LEAVES];
} wx_wind_frame_t;

/* Frame k (WX_WIND_FPS, from the face's show). k < 0: still, pose B, no streaks or leaves. Opacities 0..255, 0 hidden;
   leaf_x/y: the leaf sprite's top left; leaf_rot: 0..WX_LEAF_ROTS-1, 180 / WX_LEAF_ROTS degrees each. */
void wx_wind_frame(int level, int k, wx_wind_frame_t *f);

#ifdef __cplusplus
}
#endif
