#pragma once

#include "join_label.h"
#include "lvgl.h"

#define JOIN_VIEW_W 240
#define JOIN_VIEW_Y (-40)

/* LVGL task only; call join_view_create before the other two (docs/features.md, bot face orbit). */
void join_view_create(void);
/* Shows l (hidden for JOIN_NONE); ssid is read only for JOIN_CANT_JOIN. */
void join_view_show(join_label_t l, const char *ssid);
/* The bot orbit's offset in px, applied to both lines. */
void join_view_shift(int dx, int dy);
