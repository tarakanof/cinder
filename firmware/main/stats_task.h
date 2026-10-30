#pragma once

#include "lvgl.h"

/* The 30 s log line, the refresh counters and the LVGL stall check (docs/features.md, main.c split). */

typedef struct {
    int frames, mood, page;
    int chase_phase, chase_laps, chase_fps;
    double chase_work_s, chase_wait_s;
} stats_sample_t;

/* Once in app_main, before the ember task starts: opens the refresh window and hands it to diag. */
void stats_refr_init(void);
/* LVGL task: the display's REFR_START, RENDER_START and REFR_READY events. */
void stats_refr_cb(lv_event_t *e);
/* LVGL task, every frame_cb tick: the stall check's liveness count. */
void stats_note_loop(void);
/* LVGL task: bits for the next refresh's kind (1 pose, 2 glint). */
void stats_note_frame_kind(int kind);
/* Once in app_main: the first window of the pose redraw rate starts now. */
void stats_fps_start(void);
void stats_task_start(void);

/* main.c, called on the stats task: takes the pose redraw count, reads the rest. */
stats_sample_t stats_app_sample(void);
/* main.c, called on the stats task. */
void orbit_reap(void);
