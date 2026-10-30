#pragma once

#include "pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defined by the app (main/) on the LVGL task; host tests stub them. */
void page_bot_show(bool on);
void page_bot_input(const page_input_t *in, double t);
void page_bot_frame(const page_frame_t *f);
void page_pomo_show(bool on);
void page_pomo_input(const page_input_t *in, double t);
void page_pomo_frame(const page_frame_t *f);
void page_weather_show(bool on);
void page_weather_input(const page_input_t *in, double t);
void page_np_show(bool on);
void page_np_input(const page_input_t *in, double t);
void page_np_frame(const page_frame_t *f);
void page_day_show(bool on);
void page_day_input(const page_input_t *in, double t);
void page_day_frame(const page_frame_t *f);
void page_controls_show(bool on);
void page_controls_input(const page_input_t *in, double t);
void page_controls_frame(const page_frame_t *f);

#ifdef __cplusplus
}
#endif
