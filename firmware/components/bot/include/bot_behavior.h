#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { BOT_IDLE, BOT_SLEEPY, BOT_WORKING, BOT_WAITING, BOT_ERROR, BOT_DONE } bot_mood_t;
typedef enum { BOT_ANIM_WAIT_HOP, BOT_ANIM_MOOD_POP, BOT_ANIM_GLINT_CHASE, BOT_ANIM_USER_INPUT, BOT_ANIM_DONE_REWARD } bot_anim_t;
typedef enum { BOT_EYES_DASH, BOT_EYES_ROUND, BOT_EYES_HAPPY, BOT_EYES_ANGRY } bot_eyes_t;

/* Distances in body radii; times are monotonic seconds. */
typedef struct {
    bot_mood_t mood;
    bot_mood_t face; /* the mood the outline, badge and label show: mood from the beat behind the lids */
    bot_eyes_t eyes;
    double gaze_x, gaze_y;
    double lid_l, lid_r;
    double triangle;
    double slump;
    double badge;
    double scale_x, scale_y;
    double offset_x, offset_y;
    double hop_dy;  /* the hop's lift alone, also in offset_y */
    double orbit;   /* 0..1 */
    double breath;  /* -1..1: eye height and centre awake, the centre's bob asleep */
    bool asleep;
    bool lap;       /* done's glint lap runs */
    double lap_at;  /* while lap: when it started, on bot_pose's clock */
} bot_pose_t;

typedef struct {
    double from, to, start, dur;
    int ease;
} bot_tween_t;

typedef struct {
    uint64_t rng;
    bot_mood_t mood, face;
    bot_eyes_t eyes;
    bool swap_eyes_on_close;
    double mood_since;
    bool animating;

    double gaze_from_x, gaze_from_y, gaze_to_x, gaze_to_y;
    double gaze_start, gaze_dur;
    int gaze_ease;
    double next_saccade_at;
    double fix_x, fix_y, next_micro_at;
    double read_x;
    int rd_line, rd_lines, rd_steps;
    bool rd_turn, rd_moment;

    bool blinking, blink_heavy;
    double blink_start, blink_lag, blink_speed, blink_hold;
    bool double_blink_pending;
    double next_blink_at, last_blink_at;

    bot_tween_t triangle, slump, badge, lean_x, lean_y;
    bool popping, hopping;
    double pop_start, hop_start, next_hop_at;
    double hop_h, hop_len;
    int w_phase;
    bool w_pending;
    unsigned w_scene;
    double w_t0, burst_hop_at, burst_h, next_peek_at, next_nudge_at, next_rem_at, nudge_at, gaze_hold;
    double sleep_after_s;
    bool calm;
    bool rim_ready, ring;
    double done_hop_at, giggle_at, lap_at, beat_by;

    double breath, breath_ph, breath_per, breath_t;
    bool asleep;
    double asleep_at, sleep_left;
    bot_tween_t sleep_lid;

    bool tracking;
    double trk_x, trk_y, trk_gx, trk_gy, trk_vx, trk_vy, trk_t;

    bool acting;
    int act_eyes;
    bot_tween_t act_lid;
} bot_t;

#define BOT_SLEEP_AFTER_S 300.0
#define BOT_ASLEEP_AFTER_S 600.0
/* bot_pose_same's half pixel, body radii per unit: breath's eye centre (edges ~2x), the asleep bob, an eye tip per lid. */
#define BOT_BREATH_REACH 0.006
#define BOT_BREATH_BOB 0.0077
#define BOT_LID_REACH 0.2
/* bot_rim_tone's flag on top of the mood, and bot_rim_key's for the triangle outline. */
#define BOT_RIM_ASLEEP 0x100
#define BOT_RIM_TRIANGLE 0x200
#define BOT_HOP_DURATION_S 1.0
#define BOT_HOP_SQUASH 0.75
/* The view's outline variants: steps -BOT_RIM_STEPS (squash) .. +BOT_RIM_STEPS (stretch), 0 the base. */
#define BOT_RIM_STEPS 5
/* Seconds since waiting began or restarted; the _DEMO_ values apply while a demo scene plays. */
#define BOT_WAIT_B_S 20.0
#define BOT_WAIT_C_S 180.0
#define BOT_WAIT_REMIND_S 300.0
#define BOT_WAIT_DEMO_C_S 35.0
#define BOT_WAIT_DEMO_REMIND_S 50.0
#define BOT_NUDGE_S 0.35
/* A mood beat waits for the view's outline at most this long after the mood change, seconds. */
#define BOT_BEAT_MAX_S 0.45
/* A frame gap longer than this while a beat waits (off-page, OTA pause) gives the outline another BOT_BEAT_MAX_S. */
#define BOT_BEAT_GAP_S 0.1
/* Done's reward, seconds after the beat. */
#define BOT_DONE_HOP_S 0.35
#define BOT_DONE_HOP_H 0.7
#define BOT_GIGGLE_S 1.3
#define BOT_GIGGLE_LID 0.45
#define BOT_GIGGLE_SQUEEZE_S 0.12
#define BOT_GIGGLE_GAP_S 0.15
#define BOT_LAP_AT_S 1.8
#define BOT_LAP_S 3.0

void bot_init(bot_t *b, uint64_t seed, double now);
bool bot_set_mood(bot_t *b, bot_mood_t m, double t);
/* Before bot_pose: whether the view holds the base outline of b->mood ready (true when it draws none). A mood beat (the
   new eyes, badge and outline together) waits behind closed lids for it, at most BOT_BEAT_MAX_S after the change. */
void bot_rim_ready(bot_t *b, bool ready);
/* The working ring setting: off, done's reward runs no glint lap. */
void bot_set_ring(bot_t *b, bool on);
bot_pose_t bot_pose(bot_t *b, double t);
void bot_set_sleep_after(bot_t *b, double s);
bool bot_calm(bool quiet, bool calm_setting);
bool bot_anim_allowed(bot_anim_t a, bool calm);
void bot_set_calm(bot_t *b, bool calm, double t);
bool bot_set_mood_user(bot_t *b, bot_mood_t m, double t);
bool bot_chase_on(bool glint, bool calm, bool chase_busy);
void bot_look(bot_t *b, double x, double y, double t);
void bot_track(bot_t *b, double x, double y, double t);
void bot_track_end(bot_t *b, double t);
void bot_blink(bot_t *b, double t);
void bot_look_still(bot_t *b, double x, double y, double t);
void bot_hold_gaze(bot_t *b, double until);
/* A quiet weather moment on: working's gaze stays in y -0.19..0.15 from this frame (lines 0-2, no mark, turns at the viewer). */
void bot_read_moment(bot_t *b, bool on, double t);
void bot_push(bot_t *b, double t);
/* Waiting back to phase A once a reminder period has passed since the last start, asked earlier: when it has; nothing in other moods. */
void bot_wait_restart(bot_t *b, double t);
/* scene: the playing demo scene's number, 0 none; nonzero takes the demo timings, a change restarts waiting at A. */
void bot_wait_scene(bot_t *b, unsigned scene, double t);
/* A demo's hop or pop alone, whatever the calm or an act; done once hopping or popping is false again. */
void bot_hop(bot_t *b, double t);
void bot_pop(bot_t *b, double t);
/* A demo's sleepy mood undone: sleepy goes back to idle, which bot_set_mood never does. */
void bot_wake(bot_t *b, double t);
/* A demo's asleep: sleepy and asleep now. */
void bot_sleep(bot_t *b, double t);
/* Sleepy only: in <= 0 asleep now, else awake and asleep in s (INFINITY: paused); bot_asleep_in reads it back, 0 asleep. */
void bot_sleep_in(bot_t *b, double in, double t);
double bot_asleep_in(const bot_t *b, double t);
/* Weather-moment acting. Begin glides to the viewer and leans as for it; acting gazes keep that lean, the bot makes
   no saccades of its own and ignores bot_look (a knob turn) and bot_push (its hop). act_eyes: a bot_eyes_t costume,
   or -1 for the mood's eyes; it swaps in at the close of the blink it starts. The lid floor eases to lid over dur s
   and stays after the act ends until set again. Begin wakes an asleep bot and pauses a sleepy one's countdown to
   asleep; bot_act_end and bot_act_drop resume it. */
void bot_act_begin(bot_t *b, double t);
void bot_act_gaze(bot_t *b, double x, double y, bool glide, double t);
void bot_act_lid(bot_t *b, double lid, double dur, double t);
void bot_act_eyes(bot_t *b, int eyes, double t);
void bot_act_blink(bot_t *b, double speed, double t);
void bot_act_hop(bot_t *b, double t);
/* Back to the viewer (a glide, else a saccade), the mood's eyes and no lid floor; own saccades again after it. */
void bot_act_end(bot_t *b, bool glide, double lid_dur, double t);
/* Ends the act where the gaze is (a new mood's own glance stays), with the mood's eyes and no lid floor. */
void bot_act_drop(bot_t *b, double t);
bool bot_pose_same(const bot_pose_t *a, const bot_pose_t *b, double radius_px);
/* Screen px the outline moves for p's hop alone: the nearest even px, ties away from 0, so a mirrored hop gives -n. */
int bot_rim_dy(const bot_pose_t *p, double radius_px, double hop_scale);
void bot_rim_variant(int step, double *sx, double *sy);
bool bot_label_mark(bot_mood_t m);
/* The outline's colour and opacity key: the mood, | BOT_RIM_ASLEEP while asleep. */
int bot_rim_tone(bot_mood_t m, bool asleep);
/* The whole outline: bot_rim_tone | BOT_RIM_TRIANGLE in error. */
int bot_rim_key(bot_mood_t m, bool asleep);
/* The glint lap's step at t for a lap from lap_at (bot_pose_t.lap_at), steps of step_s seconds; -1 outside the lap. */
int bot_lap_step(double lap_at, double t, double step_s, int steps);
bot_mood_t bot_rim_mood(int tone);
float bot_rim_alpha(int tone);

#ifdef __cplusplus
}
#endif
