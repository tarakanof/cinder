/* All functions: LVGL task only (or with the LVGL lock held). */
#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "weather_face.h"

/* Starts the bake task; the face images live in PSRAM only while the page shows (allocated by temp_face_show). */
void temp_face_create(lv_obj_t *parent);

/* Showing allocates the images and replays the arrival; hiding frees them. No PSRAM: black until the next show. */
void temp_face_show(bool on);

/* dt in seconds; obs may be NULL (no data yet). offline: Ember is OFFLINE, show the observation's age. */
void temp_face_update(const wx_obs_t *obs, bool offline, double dt);
