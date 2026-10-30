#pragma once

#include <stdbool.h>

#include "bot_behavior.h"
#include "demo.h"
#include "pages.h"
#include "wx_moment.h"

/* Plays Ember's demo scenes on the knob: page, face, act, and the way back (docs/features.md, demo mode, main.c split). LVGL task only. */

typedef struct {
    bot_t *bot;
    pages_nav_t *nav;
} scene_ctx_t;

/* Once in app_main, before the first frame. */
void scene_init(const scene_ctx_t *ctx);
/* Every frame, before the page's input: starts, ends and resumes scenes. */
void scene_frame(double t);
/* True when input goes to the demo gate, not the page. An input that ends a scene ends it on its next frame. */
bool scene_gate(double t, bool input);
/* Every frame a scene plays, in place of the weather moment. wx NULL: none yet. */
void scene_act(double t, const wx_obs_t *wx, bool bot_page);
void scene_end(double t);
/* Drops the hold on a scene's page: the OTA face ends a scene without the 1.5 s for the next one. */
void scene_resume_cancel(void);

bool scene_on(void);
/* Counts scenes from 1, never 0. */
unsigned scene_n(void);
demo_act_kind_t scene_act_kind(void);
/* An ended scene still holds its page for the next one. */
bool scene_resuming(void);
/* The chase style the act asks for this frame, -1 none. */
int scene_chase_take(void);

/* main.c: the page wipe. */
void wipe_start(int dir, double t);
