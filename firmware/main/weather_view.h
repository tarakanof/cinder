/* All functions: LVGL task only (or with the LVGL lock held). */
#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "weather_face.h"

typedef enum {
    WEATHER_VIEW_PAGE,
    WEATHER_VIEW_OVERLAY,
} weather_view_mode_t;

/* Pre-renders sprites into PSRAM (~80 KB, plus an 84 KB canvas). */
lv_obj_t *weather_view_create(lv_obj_t *parent);

void weather_view_set_mode(weather_view_mode_t mode);

void weather_view_set_temp_visible(bool on);

void weather_view_show(bool on);
bool weather_view_visible(void);

/* dt in seconds; obs may be NULL (no data yet). offline: Ember is OFFLINE, show the observation's age. */
void weather_view_update(const wx_obs_t *obs, bool offline, double dt);

wx_look_t weather_view_look(void);
bool weather_view_flash(void);
