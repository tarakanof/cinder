#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { BOT_IDLE, BOT_SLEEPY, BOT_WORKING, BOT_WAITING, BOT_ERROR, BOT_DONE } bot_mood_t;
typedef enum { BOT_EYES_DASH, BOT_EYES_ROUND, BOT_EYES_HAPPY, BOT_EYES_ANGRY } bot_eyes_t;

/* Distances in body radii; times are monotonic seconds. */
typedef struct {
    bot_mood_t mood;
    bot_eyes_t eyes;
    double gaze_x, gaze_y;
    double lid_l, lid_r;
    double triangle;
    double slump;
    double badge;
    double scale_x, scale_y;
    double offset_x, offset_y;
    double orbit;   /* 0..1 */
} bot_pose_t;

typedef struct {
    double from, to, start, dur;
    int ease;
} bot_tween_t;

typedef struct {
    uint64_t rng;
    bot_mood_t mood;
    bot_eyes_t eyes;
    bool swap_eyes_on_close;
    double mood_since;
    bool animating;

    double gaze_from_x, gaze_from_y, gaze_to_x, gaze_to_y;
    double gaze_start, gaze_dur;
    int gaze_ease;
    double next_saccade_at;
    double read_x;

    bool blinking;
    double blink_start, blink_lag, blink_speed;
    bool double_blink_pending;
    double next_blink_at, last_blink_at;

    bot_tween_t triangle, slump, badge, lean_x, lean_y;
    bool popping, hopping;
    double pop_start, hop_start, next_hop_at;
    double sleep_after_s;

    bool tracking;
    double trk_x, trk_y, trk_gx, trk_gy, trk_vx, trk_vy, trk_t;
} bot_t;

#define BOT_SLEEP_AFTER_S 300.0
#define BOT_HOP_DURATION_S 1.0
#define BOT_HOP_SQUASH 0.75

void bot_init(bot_t *b, uint64_t seed, double now);
bool bot_set_mood(bot_t *b, bot_mood_t m, double t);
bot_pose_t bot_pose(bot_t *b, double t);
void bot_set_sleep_after(bot_t *b, double s);
void bot_look(bot_t *b, double x, double y, double t);
void bot_track(bot_t *b, double x, double y, double t);
void bot_track_end(bot_t *b, double t);
void bot_blink(bot_t *b, double t);
void bot_look_still(bot_t *b, double x, double y, double t);
void bot_hold_gaze(bot_t *b, double until);
void bot_push(bot_t *b, double t);
bool bot_pose_same(const bot_pose_t *a, const bot_pose_t *b, double radius_px);

#ifdef __cplusplus
}
#endif
