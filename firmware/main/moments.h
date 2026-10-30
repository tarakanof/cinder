#pragma once

#include <stdbool.h>

#include "bot_behavior.h"
#include "knob_settings.h"
#include "wx_moment.h"

/* The bot's weather and music moments: their triggers, the eye acting and the chase hold around moment_view and music_view (docs/features.md, main.c split). LVGL task only. */

typedef struct {
    bot_t *bot;
    const knob_settings_t *ks; /* read every frame: bot.weather_moments, bot.weather_every_min */
} moments_ctx_t;

/* Once in app_main, before the first frame. */
void moments_init(const moments_ctx_t *ctx);
/* The weather moment, every frame without a demo. wx NULL: none yet. */
void moments_tick(double t, const wx_obs_t *wx, bool calm, bool bot_page);
/* The music moment, every frame. on: no demo scene or view. */
void moments_music_tick(double t, bool calm, bool offline, bool bot_page, bool on);
/* Ends the eye acting of a weather or music moment cut short. */
void moment_unact(double t);
void music_unact(double t);
/* A weather moment just started: its eye script runs from here on, off moments_cue_poll. */
void moments_cue_start(const wxm_moment_t *m, double t);
/* Every frame a moment may play: polls moment_view and runs the cues its time has reached. */
void moments_cue_poll(double t);
