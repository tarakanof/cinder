/* All functions: LVGL task only (or with the LVGL lock held). */
#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "weather_face.h"

/* Starts the bake task; the face images live in PSRAM only while the page shows (allocated by temp_face_show). */
void temp_face_create(lv_obj_t *parent);

/* Showing allocates the images and replays the arrival; hiding frees them. No PSRAM: black, retried every 5 s. */
void temp_face_show(bool on);

/* Plain knob detents on the weather page: the next or previous face, kept until the page shows again. */
void temp_face_turn(int detents);

/* A demo's face: the pick (WXF_AUTO: the page's own choice) while the page shows; temp_face_picked to put it back. */
int temp_face_picked(void);
void temp_face_pick(int face);

/* The face the page resolved last frame (WXF_AUTO as what it chose), -1 while hidden or before the first frame. */
int temp_face_resolved(void);

/* OTA face: call with true on every OTA frame (parks a queued bake, frees the hidden face once a running one ends). */
void temp_face_hold(bool hold);

/* dt in seconds; obs may be NULL (no data yet). offline: Ember is OFFLINE, show the observation's age. */
void temp_face_update(const wx_obs_t *obs, bool offline, double dt);
