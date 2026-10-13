#pragma once

#include <stdatomic.h>
#include <stdbool.h>

#include "bot_behavior.h"
#include "rim_beat.h"
#include "rim_job.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Outline slots: variant steps -BOT_RIM_STEPS..+BOT_RIM_STEPS at 0..RIM_SLOTS-1; the base is RIM_SLOT_BASE, and slot
   RIM_SLOT_SPARE (the -BOT_RIM_STEPS squash) takes a new job's base until the swap. */
#define RIM_SLOTS (2 * BOT_RIM_STEPS + 1)
#define RIM_SLOT_BASE BOT_RIM_STEPS
#define RIM_SLOT_SPARE 0

typedef struct {
    void *ctx;
    void (*show)(void *ctx, void *buf); /* point the outline image at buf */
    void (*wake)(void *ctx);            /* notify the rim task */
    void (*swapped)(void *ctx, int key); /* the outline key (bot_rim_key) shows now: badge colour, label */
} rim_slots_ops_t;

typedef struct {
    rim_job_t job;
    void *buf[RIM_SLOTS];
    float sx[RIM_SLOTS], sy[RIM_SLOTS];
    int order[RIM_SLOTS - 1];
    atomic_uint var_gen[RIM_SLOTS];
    atomic_uint base_gen;
    atomic_bool own;
    rim_beat_t beat;
    int shown;
} rim_slots_t;

typedef void (*rim_slots_raster_t)(void *ctx, void *buf, int shape, int tone, float sx, float sy);

void rim_slots_init(rim_slots_t *s, void *const buf[RIM_SLOTS]);
/* rim task, once woken: the job's base into the spare slot while it is not swapped, then its missing variants, until
   the job is replaced or held; yield after each raster. */
void rim_slots_run(rim_slots_t *s, rim_slots_raster_t raster, void (*yield)(void *ctx), void *ctx);
/* LVGL task, every frame on any page, before bot_pose: starts the job for key (bot_rim_key) when it changes; returns
   bot_rim_ready's input for key's mood. */
bool rim_slots_feed(rim_slots_t *s, int key, const rim_slots_ops_t *ops);
/* LVGL task, on the bot page: the pose's outline (want), its face's (face), height/width ratio and time; swaps when the
   face asks and the base is ready. Returns the slot to show (rim_slots_show). */
int rim_slots_sync(rim_slots_t *s, int want, int face, float ratio, double t, const rim_slots_ops_t *ops);
void rim_slots_show(rim_slots_t *s, int slot, const rim_slots_ops_t *ops);
/* LVGL task: true drops the job until false, which redoes it. */
void rim_slots_hold(rim_slots_t *s, bool hold, const rim_slots_ops_t *ops);

#ifdef __cplusplus
}
#endif
