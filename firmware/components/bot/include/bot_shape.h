#pragma once

#include "bot_behavior.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOT_RING_POINTS 192
#define BOT_STROKE_MAX 16
#define BOT_LID_KEEP 0.3
#define BOT_LID_DROOP 0.015

typedef struct {
    int n;
    double x[BOT_STROKE_MAX], y[BOT_STROKE_MAX];
    double width;
    double lid; /* 0..1: how far the lid line has come down (bot_lid_line_y); 0 for an eye no lid clips */
} bot_stroke_t;

/* Builds the shared triangle-body table; call once from one task before any bot_body_ring. */
void bot_shape_init(void);

/* Needs bot_shape_init; read-only afterwards, so safe from any task. */
void bot_body_ring(double k, double xs[BOT_RING_POINTS], double ys[BOT_RING_POINTS]);

void bot_eye_strokes(const bot_pose_t *p, double eye_scale, bot_stroke_t out[2]);

/* Screen y (px, down) of the lid line for a stroke at screen ys and width px: the eye shows only below it.
 * -INFINITY with no lid; at lid 1 a bottom strip of BOT_LID_KEEP x width stays. */
double bot_lid_line_y(const double *ys, int n, double width, double lid);

/* Farthest point of both eyes from the body centre, stroke width included (body radii). */
double bot_eyes_extent(const bot_stroke_t eyes[2]);

/* Gaze magnitude that puts the eyes' farthest point at outer (body radii) when they look
 * toward screen_deg (0 = 3 o'clock, clockwise), for p with orbit 1. */
double bot_orbit_gaze(const bot_pose_t *p, double eye_scale, double screen_deg, double outer);

#ifdef __cplusplus
}
#endif
