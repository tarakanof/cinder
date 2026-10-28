#pragma once

#include <stdbool.h>

#include "day.h"
#include "lvgl.h"

/* LVGL lock held. */
void day_view_create(lv_obj_t *parent);
void day_view_show(bool show);
/* LVGL task, every frame while shown; t in seconds: rolls the text drum when the face changes. */
void day_view_update(const day_model_t *m, double t);
