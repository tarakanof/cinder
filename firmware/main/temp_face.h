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

/* OTA face: call with true on every OTA frame (parks a queued bake, frees the hidden face once a running one ends). */
void temp_face_hold(bool hold);

/* dt in seconds; obs may be NULL (no data yet). offline: Ember is OFFLINE, show the observation's age. */
void temp_face_update(const wx_obs_t *obs, bool offline, double dt);
