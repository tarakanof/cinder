#pragma once

#include "lvgl.h"

/* LVGL task, once: the room page creates its objects under parent on its first show (CONFIG_CINDER_ROOM only). */
void room_page_init(lv_obj_t *parent);
