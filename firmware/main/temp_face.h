/* All functions: LVGL task only (or with the LVGL lock held). */
#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "weather_face.h"

/* Allocates the face images in PSRAM (~0.7 MB). */
void temp_face_create(lv_obj_t *parent);

/* Showing replays the arrival. */
void temp_face_show(bool on);

/* dt in seconds; obs may be NULL (no data yet). offline: Ember is OFFLINE, show the observation's age. */
void temp_face_update(const wx_obs_t *obs, bool offline, double dt);
