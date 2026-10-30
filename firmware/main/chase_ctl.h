#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bot_behavior.h"
#include "glint_chase.h"

/* The glint chase: its state, the USB request, the eyes' tracking and the orbit table (docs/features.md, main.c split). LVGL task unless noted. */

typedef struct {
    double t;
    bool glint;     /* the page shows the glint; bot_chase_on is applied here */
    bool calm;
    bool scene;     /* a demo scene plays: scene_req replaces the USB request, which waits */
    int scene_req;  /* style, -1 none */
    bot_eyes_t eyes;
} chase_ctl_in_t;

typedef struct {
    int phase, laps, fps;
    double work_s, wait_s;
} chase_ctl_sample_t;

/* Once in app_main, before the first frame. */
void chase_ctl_init(uint32_t seed);
/* Every frame on any page, before bot_pose: starts a requested chase, ticks it and moves the eyes. */
gc_out_t chase_ctl_frame(bot_t *bot, const chase_ctl_in_t *in);
/* Any task. fps 0 unchanged, laps 0 random. Returns 0 started (next frame), 1 busy, 2 not working. */
int chase_ctl_request(int style, int fps, int laps);
/* A demo scene started: requests answer busy before its first frame. */
void chase_ctl_status_busy(void);

/* No chase starts while held; the working time still counts. */
void chase_ctl_hold(bool hold);
/* The chase is not idle: it orbits or the label is on its way back. */
bool chase_ctl_busy(void);
/* Any task: the eye rate, CINDER1 chase fps. */
int chase_ctl_fps(void);
/* Stats task; the phase, laps and times are plain reads, for a log line. */
chase_ctl_sample_t chase_ctl_sample(void);

/* A demo scene's start: copies the chase and the half chase, then stops a running one and keeps its working time. */
void chase_ctl_pause(glint_chase_t *saved, gc_half_chase_t *saved_half);
/* A demo scene's end: the copies carry on at t as if the time since saved_t had not passed. */
void chase_ctl_restore(const glint_chase_t *saved, const gc_half_chase_t *saved_half, double saved_t, double t);

/* Once in app_main, not in setup mode: fills the gaze table on its own task. */
void chase_ctl_orbit_start(void);
/* Stats task: deletes that task once it has finished. */
void chase_ctl_orbit_reap(void);
