#pragma once

#include "pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defined by the app (main/main.c) on the LVGL task; host tests stub them. */
void page_bot_show(bool on);
void page_bot_input(const page_input_t *in, double t);
void page_bot_frame(const page_frame_t *f);
void page_pomo_show(bool on);
void page_pomo_input(const page_input_t *in, double t);
void page_pomo_frame(const page_frame_t *f);
void page_weather_show(bool on);
void page_np_show(bool on);
void page_np_input(const page_input_t *in, double t);
void page_np_frame(const page_frame_t *f);

#ifdef __cplusplus
}
#endif
