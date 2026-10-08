#pragma once

#include "bot_behavior.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOT_RING_POINTS 192
#define BOT_STROKE_MAX 16

typedef struct {
    int n;
    double x[BOT_STROKE_MAX], y[BOT_STROKE_MAX];
    double width;
} bot_stroke_t;

/* Builds the shared triangle-body table; call once from one task before any bot_body_ring. */
void bot_shape_init(void);

/* Needs bot_shape_init; read-only afterwards, so safe from any task. */
void bot_body_ring(double k, double xs[BOT_RING_POINTS], double ys[BOT_RING_POINTS]);

void bot_eye_strokes(const bot_pose_t *p, double eye_scale, bot_stroke_t out[2]);

/* Farthest point of both eyes from the body centre, stroke width included (body radii). */
double bot_eyes_extent(const bot_stroke_t eyes[2]);

/* Gaze magnitude that puts the eyes' farthest point at outer (body radii) when they look
 * toward screen_deg (0 = 3 o'clock, clockwise), for p with orbit 1. */
double bot_orbit_gaze(const bot_pose_t *p, double eye_scale, double screen_deg, double outer);

#ifdef __cplusplus
}
#endif
