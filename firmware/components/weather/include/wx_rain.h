#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "weather_face.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_RAIN_FPS 15
#define WX_SHEETS 3
#define WX_SHEET_H 150
#define WX_SHEET_PERIOD (2 * WX_SHEET_H)
#define WX_SPLASHES 3
#define WX_DRIPS 2
#define WX_DRIP_POSES 3
#define WX_STORM_FRAMES 90

typedef struct {
    int x0, y0, x1, y1; /* face coordinates, x1/y1 exclusive */
} wx_box_t;

typedef enum { WX_PRECIP_RAIN, WX_PRECIP_SNOW, WX_PRECIP_STORM } wx_precip_t;

typedef struct {
    bool on;
    wx_precip_t kind;
    int intensity; /* 1 light, 2 moderate, 3 heavy */
    bool has_pop;
    int pop;
    bool has_until;
    int until_min; /* local minute of day */
    char sub[24];
} wx_rain_t;

/* The Rain now face: on while it rains, snows or storms now and the data is fresh. */
void wx_rain_from(const wx_obs_t *o, wx_rain_t *r);
uint32_t wx_rain_rgb(int intensity);
/* Same pictures: kind, intensity and has_pop (the ring); pop, until and sub only move the marker and the text. */
bool wx_rain_same_scene(const wx_rain_t *a, const wx_rain_t *b);
/* shown: the rain on screen or NULL. While it stays wet with the same kind a lower intensity keeps the shown one (and its word). */
void wx_rain_keep(wx_rain_t *r, const wx_rain_t *shown);

/* A drop of a sheet tile: a streak from (x, y) to (x - slant, y + len) in tile A (0..WX_SHEET_H) or B (one tile lower);
   level 1 shows from light rain, 2 from moderate, 3 only in heavy. */
typedef struct {
    float x, y, len, op, slant;
    uint8_t tile, level;
} wx_drop_t;

typedef struct {
    wx_box_t box;
    int tile_frames, delay_frames, n;
    const wx_drop_t *drop;
} wx_sheet_t;

const wx_sheet_t *wx_sheet(int s);
/* Canopy surface (top of the umbrella) at x. */
float wx_canopy_y(float x);
bool wx_drop_shown(const wx_drop_t *d, const wx_rain_t *r);

typedef struct {
    int x, y;  /* face coordinates of the sprite's top left */
    int sheet; /* the sheet it falls in */
    int drop;  /* index into that sheet's drops */
} wx_splash_at_t;

bool wx_splash_at(int n, wx_splash_at_t *at);
/* Sheet frame (0..frames-1) on which splash n's drop reaches the canopy. */
int wx_splash_hit(int n);

typedef struct {
    int off[WX_SHEETS]; /* the sheet's strip offset, 0..WX_SHEET_PERIOD-1 px down */
    int drip_pose[WX_DRIPS], drip_dy[WX_DRIPS], drip_opa[WX_DRIPS]; /* pose -1: hidden */
    int splash_dy[WX_SPLASHES], splash_opa[WX_SPLASHES];            /* opa 0: hidden */
    bool bolt;
} wx_rain_frame_t;

/* Frames per strip cycle of sheet s for this kind (snow falls at half speed). */
int wx_sheet_frames(const wx_rain_t *r, int s);
/* Frame k (WX_RAIN_FPS, from the face's show) of the rain layers. */
void wx_rain_frame(const wx_rain_t *r, int k, wx_rain_frame_t *f);
/* The LVGL calls from frame p shown at p_opa to frame f at opa, and the values they pass. After an attach p is all 0xFF
   and p_opa -1: every object hidden at an unknown place, so one that appears gets its buffer, y and opacity whatever p
   holds. y: in the object's sheet; pose: index into the layout's drip buffers. */
typedef struct {
    bool strip_y[WX_SHEETS], opa;
    bool drip_buf[WX_DRIPS], drip_y[WX_DRIPS], drip_opa[WX_DRIPS];
    bool splash_y[WX_SPLASHES], splash_opa[WX_SPLASHES];
    int strip_top[WX_SHEETS][2], layer_opa;
    int drip_pose[WX_DRIPS], drip_top[WX_DRIPS], drip_alpha[WX_DRIPS];
    int splash_top[WX_SPLASHES], splash_alpha[WX_SPLASHES];
    bool drip_on[WX_DRIPS], splash_on[WX_SPLASHES], bolt_on;
} wx_rain_ops_t;

void wx_rain_ops(const wx_rain_frame_t *p, int p_opa, const wx_rain_frame_t *f, int opa, wx_rain_ops_t *ops);

#ifdef __cplusplus
}
#endif
