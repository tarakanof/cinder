#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_face.h"
#include "wx_rain.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_CAST_SPAN_S 43200
#define WX_CAST_STEP_S 300
#define WX_CAST_SOON_S 3600
#define WX_CAST_KEEP_S 5400
#define WX_CAST_RGB 0x5C9CE0
#define WX_CAST_SIDES 2
#define WX_CAST_FPS 15
#define WX_CAST_SLIDE_S 0.96
#define WX_CAST_DROP_IN_S 0.18
#define WX_CAST_DROP_OUT_S 1.2
#define WX_CAST_GATE_S 0.96
#define WX_CAST_GATE_END_S 1.14
#define WX_CAST_LABEL_END_S 1.32
#define WX_CAST_BREATHE_PX 4
#define WX_CAST_RAMP 0.08f
#define WX_CAST_GLOOM_STEPS 4
#define WX_DROP_BOX 34

typedef struct {
    int a, n;      /* dial minute (0..719) the lit part starts at, minutes long */
    int intensity; /* 1..3 */
} wx_arc_t;

typedef struct {
    bool on;        /* the Rain forecast face */
    bool have;      /* fresh data with the rain block and the offset: dial, marker */
    bool wet;       /* raining now: the countdown runs to the stop */
    bool has_count;
    bool hours;     /* count in half hours, else in 5-min steps */
    int count;
    int variant;    /* 0 no clouds, 1 light (AD4), 2 moderate (AD5), 3 heavy (AD3) */
    bool snow;
    float now_deg;  /* clock degrees from 12 o'clock, clockwise */
    float to_deg;   /* where the drop lands; has_count only */
    int narc;
    wx_arc_t arc[WX_SPELLS];
    char label[8];  /* the dial time at to_deg: "17" or "15:15" */
    float label_deg;
    char sub[40];   /* LVGL recolour markup */
} wx_cast_t;

/* shown: the forecast on screen or NULL; while it stays wet with the same kind a lower intensity keeps the shown one. */
void wx_cast_from(const wx_obs_t *o, const wx_cast_t *shown, wx_cast_t *c);
/* The auto rule: dry now and the next spell starts within WX_CAST_SOON_S (WX_CAST_KEEP_S while shown). */
bool wx_cast_soon(const wx_obs_t *o, bool shown);
bool wx_cast_same_scene(const wx_cast_t *a, const wx_cast_t *b);
bool wx_cast_same_ring(const wx_cast_t *a, const wx_cast_t *b);

/* s seconds ahead: under an hour in 5-min steps (at least one), else in half hours. */
void wx_cast_round(long long s, bool *hours, int *count);
void wx_cast_count_text(bool hours, int count, char *buf, size_t n);
/* Arrival count-up: n steps, step k's text. */
int wx_cast_count_n(const wx_cast_t *c);
void wx_cast_step_text(const wx_cast_t *c, int k, char *buf, size_t n);

/* Dial label anchor at deg: align -1 right edge at x, 0 centred, 1 left edge; y is the text's centre line. */
void wx_cast_label_at(float deg, float *x, float *y, int *align);
void wx_cast_ring_xy(float deg, float *x, float *y);
/* cubic-bezier(0.45, 0, 0.25, 1) at p 0..1. */
double wx_cast_ease(double p);
float wx_cast_drop_deg(const wx_cast_t *c, double t);
/* 0..255 at t s after the arrival started; 0 after WX_CAST_DROP_OUT_S. */
int wx_cast_drop_opa(double t);
/* Opacity of the rain sheets and of the dial label t s after the arrival started. */
int wx_cast_gate_opa(double t);
int wx_cast_label_opa(double t);

typedef struct {
    float x, y, len, op, slant;
} wx_streak_t;

typedef struct {
    wx_box_t box;
    int h; /* tile height; the strip is two tiles, 2 h rows */
    int tile_frames, delay_frames;
    float w;
    int n;
    const wx_streak_t *a, *b;
} wx_cast_sheet_t;

int wx_cast_sides(int variant);
const wx_cast_sheet_t *wx_cast_sheet(int variant, int side);
/* Frame k (WX_CAST_FPS, from the face's show): the strip's offset, 0..2 h - 1 px down. Snow falls at half speed. */
int wx_cast_sheet_off(const wx_cast_sheet_t *s, bool snow, int k);
int wx_cast_breathe(int variant, int side, int k);
/* 255 except the heavy clouds' darker pulse, WX_CAST_GLOOM_STEPS levels each way. */
int wx_cast_gloom(int variant, int k);

#ifdef __cplusplus
}
#endif
