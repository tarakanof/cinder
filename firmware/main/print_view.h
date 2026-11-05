#pragma once

#include "lvgl.h"

/* LVGL task. The page's show, input and frame hooks are page_print_* (page_ops.h). */
void print_view_create(lv_obj_t *parent);
