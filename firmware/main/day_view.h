#pragma once

#include <stdbool.h>

#include "day.h"
#include "lvgl.h"

/* LVGL lock held. */
void day_view_create(lv_obj_t *parent);
void day_view_show(bool show);
/* LVGL task; touches only what changed since the last model. */
void day_view_update(const day_model_t *m);
