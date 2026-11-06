#pragma once

#include <stdbool.h>

#include "bot_behavior.h"
#include "knob_settings.h"
#include "wx_moment.h"

/* The bot's weather and music moments: their triggers, the eye acting and the chase hold around moment_view and music_view (docs/features.md, main.c split). LVGL task only. */

typedef struct {
    bot_t *bot;
    const knob_settings_t *ks; /* read every frame: bot.weather_moments, bot.weather_every_min, bot.print_moments */
} moments_ctx_t;

/* Once in app_main, before the first frame. */
void moments_init(const moments_ctx_t *ctx);
/* The weather moment, every frame without a demo. wx NULL: none yet. */
void moments_tick(double t, const wx_obs_t *wx, bool calm, bool bot_page);
/* The music moment, every frame. on: no demo scene or view. */
void moments_music_tick(double t, bool calm, bool offline, bool bot_page, bool on);
/* The print moments (B3 done, B4 needs you), every frame unless a scene has an act, also during an act-less scene: they
   share moment_view with the weather moments. calm: quiet hours. real_ok: no scene, demo view or gap between scenes, so
   real events may play and B4 repeats (not offline either); without it only a demo event does, and only while scene (a
   demo scene runs). */
void moments_print_tick(double t, bool calm, bool bot_page, bool real_ok, bool scene);
/* The jump to the print page when a print starts (cinder#324), every frame. page_on: the print page is in the page list.
   real_ok: as for moments_print_tick. blocked: an agent waits, the bot sleeps or the usage screen is open. True once per
   `started` event id when the page should switch now; the event store is the print moments' (print_moment.h). */
bool moments_print_jump(double t, bool page_on, bool real_ok, bool blocked);
/* Ends the eye acting of a weather or music moment cut short. */
void moment_unact(double t);
void music_unact(double t);
/* A weather moment just started: its eye script runs from here on, off moments_cue_poll. */
void moments_cue_start(const wxm_moment_t *m, double t);
/* Every frame a moment may play: polls moment_view and runs the cues its time has reached. */
void moments_cue_poll(double t);
