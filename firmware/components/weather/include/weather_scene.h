#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "weather_face.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_SKY_W 200
#define WX_SKY_H 140

typedef enum {
    WX_SPR_RAIN,
    WX_SPR_FLAKE_S,
    WX_SPR_FLAKE_L,
    WX_SPR_CLOUD_L,
    WX_SPR_CLOUD_S,
    WX_SPR_BOLT,
    WX_SPR_MOON,
    WX_SPR_STAR,
    WX_SPR_SUN,
    WX_SPR_RAYS,
    WX_SPR_FOG,
    WX_SPR_COUNT
} wx_sprite_id_t;

#define WX_RAY_STEPS 16
#define WX_CLOUD_OUTLINE 0
#define WX_CLOUD_FILL 1

typedef struct {
    int w, h, frames;
} wx_sprite_size_t;

wx_sprite_size_t wx_sprite_size(int id);

#define WX_STROKE_PTS 80
#define WX_STROKES_MAX 10
typedef struct {
    int n;
    float x[WX_STROKE_PTS], y[WX_STROKE_PTS];
    float hw;
} wx_stroke_t;

int wx_sprite_strokes(int id, int frame, wx_stroke_t out[WX_STROKES_MAX]);

void wx_sprite_fill(int id, int frame, uint8_t *mask);

typedef struct {
    uint8_t sprite, frame, alpha;
    int16_t x, y;
    uint32_t rgb;
} wx_draw_t;

#define WX_MAX_PARTICLES 20
#define WX_STARS 6
#define WX_MAX_DRAW 40

typedef struct {
    float x, y;
    float vx, vy;     /* px/s */
    float phase, freq, amp;
    uint8_t sprite;
} wx_particle_t;

typedef struct {
    wx_look_t look;
    uint32_t rng;
    double t;
    double acc;
    double since;
    bool dirty;
    int np;
    wx_particle_t p[WX_MAX_PARTICLES];
    int star_x[WX_STARS], star_y[WX_STARS];
    int twinkle;
    double twinkle_start, next_twinkle;
    double next_bolt;
    double bolt_on, bolt_off, bolt_on2, bolt_off2;
    int bolt_x;
} wx_scene_t;

void wx_scene_init(wx_scene_t *s, uint32_t seed);
bool wx_scene_set_look(wx_scene_t *s, const wx_look_t *look);
double wx_scene_period(const wx_look_t *look);
bool wx_scene_tick(wx_scene_t *s, double dt);
void wx_scene_invalidate(wx_scene_t *s);
int wx_scene_draw(const wx_scene_t *s, wx_draw_t *out, int max);
bool wx_scene_flash(const wx_scene_t *s);

#ifdef __cplusplus
}
#endif
