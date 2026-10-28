#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Times of day are minutes of the local day, 0-1439; DAY_NONE: absent or unknown. */
#define DAY_NONE (-1)
#define DAY_MINUTES 1440
#define DAY_STOP_STEP 15
#define DAY_OFFER_STEP 5
#define DAY_COUNT_MAX 100000
#define DAY_BREAKS 16
#define DAY_UTC_OFFSET_MAX (18 * 3600)

#define DAY_TURN_DEBOUNCE_S 1.5
#define DAY_STOP_HOLD_S 5.0
#define DAY_SUMMARY_S 5.0
#define DAY_BUSY_S 3.0
#define DAY_NOTE_S 4.0

#define DAY_ACCENT 0xFF6A3D
#define DAY_BREAK_RGB 0x4FA9FF
#define DAY_TEXT 0xF4F4F2
#define DAY_DIM 0x5A5A5A
#define DAY_NOTE 0x9A9A9A
#define DAY_OFF_TEXT 0x8A8A8A
#define DAY_TRACK 0x1E1E1E
#define DAY_OFF_RING 0x141414
#define DAY_MARK 0x6A6A6A
#define DAY_FAR 0xA8A8A8

typedef enum { DAY_OFF, DAY_ON, DAY_BREAK } day_state_t;

typedef struct {
    bool has;
    int on_min;
    bool in_range;
    int week_in_range, week_days;
} day_summary_t;

typedef struct {
    day_state_t state;
    int started, stop_at, offer_start;
    int on_min, break_min, over_min;
    int replies_off_last_night;
    int agents;
    bool break_hint;
    int n_breaks;
    int breaks[DAY_BREAKS][2];   /* finished breaks, start and end, oldest first */
    bool has_utc_offset;
    int utc_offset;              /* s, the day clock's zone at the answer's time: the zone of every HH:MM here */
    day_summary_t summary;
} day_block_t;

struct cJSON;
/* The view's day block. False with *out zeroed (times DAY_NONE) when o is not an object or its state is not off, on or break. */
bool day_parse(const struct cJSON *o, day_block_t *out);

/* Exactly "HH:MM", 00:00-23:59, else DAY_NONE. */
int day_hhmm_parse(const char *s);
void day_hhmm(int min, char *buf, size_t n);
/* Minutes from a forward to b around the dial, 0-1439. */
int day_fwd(int a, int b);
/* m moved by steps on a grid of step minutes, wrapping at midnight; an off-grid m snaps to the grid in the turn's direction first. */
int day_step(int m, int steps, int step);
/* rgb with each channel times k, rounded. */
uint32_t day_scale(uint32_t rgb, float k);

typedef enum {
    DAY_FACE_WAIT,
    DAY_FACE_OFF,
    DAY_FACE_ON,
    DAY_FACE_BREAK,
    DAY_FACE_PAST,
    DAY_FACE_OFFER,
    DAY_FACE_SUMMARY,
} day_face_t;

typedef enum {
    DAY_ACT_NONE,
    DAY_ACT_START,
    DAY_ACT_BREAK,
    DAY_ACT_RESUME,
    DAY_ACT_STOP,
    DAY_ACT_STOP_AT,
    DAY_ACT_DISMISS,
} day_act_kind_t;

typedef struct {
    day_act_kind_t kind;
    int stop_at;   /* START, STOP_AT */
    int at;        /* START: back-dated start; DAY_NONE starts now */
} day_act_t;

#define DAY_ACTS 2

/* Path under /v1/day/, or NULL for DAY_ACT_NONE. */
const char *day_act_path(day_act_kind_t k);
/* PUT, else POST. */
bool day_act_put(day_act_kind_t k);
/* JSON body; length, or -1 when it does not fit n. */
int day_act_body(const day_act_t *a, char *buf, size_t n);
const char *day_face_name(day_face_t f);
/* What a long push does on face f, for the hold ring; NULL: nothing. */
const char *day_hold_label(day_face_t f);

typedef struct {
    const day_block_t *b;   /* NULL: no block */
    bool fed;               /* a view came; with b NULL, Ember sent no day block */
    double age_s;           /* since the block came */
    int now_min;            /* local time; DAY_NONE when the knob does not know it */
    bool now_exact;         /* now_min is in the day clock's zone (else the weather's: display only) */
    bool busy;              /* an action is on its way or not yet in a view */
} day_in_t;

typedef struct {
    int stop;               /* the stop time turned on On or Past; DAY_NONE: the view's */
    int stop_base;          /* the view's stop time when the turning began */
    int stop_delta;         /* minutes the turns moved it, signed, from stop_base */
    bool stop_dirty;        /* turned, not sent */
    double stop_turn_at, stop_sent_at;
    int offer_base, offer_at;
    bool summary_seen, summary_done;
    double summary_at;
} day_ui_t;

void day_ui_init(day_ui_t *ui);
/* Every frame, before input: the offer, the summary and a turned stop time follow the view. */
void day_ui_sync(day_ui_t *ui, const day_in_t *in, double t);
day_face_t day_ui_face(const day_ui_t *ui, const day_in_t *in, double t);
/* Each returns the actions to send, in order, in out (at most DAY_ACTS). */
int day_ui_turn(day_ui_t *ui, const day_in_t *in, int detents, double t, day_act_t out[DAY_ACTS]);
int day_ui_push(day_ui_t *ui, const day_in_t *in, double t, day_act_t out[DAY_ACTS]);
int day_ui_long(day_ui_t *ui, const day_in_t *in, double t, day_act_t out[DAY_ACTS]);
/* The turned stop time once DAY_TURN_DEBOUNCE_S has passed since the last detent. */
int day_ui_tick(day_ui_t *ui, const day_in_t *in, double t, day_act_t out[DAY_ACTS]);
/* The page hides: the turned stop time goes now, not when the page shows again. */
int day_ui_leave(day_ui_t *ui, double t, day_act_t out[DAY_ACTS]);

#define DAY_SEGS (DAY_BREAKS + 4)

/* An arc from one time of day to another, clockwise; to < from crosses midnight. */
typedef struct {
    int from, to;
    uint32_t rgb;
} day_seg_t;

/* A value over its caption: one panel on the text drum. */
typedef struct {
    char val[32];
    char cap[40];
    uint8_t big;        /* value size in the centre slot: 96, or 48 for a word ("off"); 0 = 96 */
    uint32_t rgb;       /* 0: the drum's white, greying with distance */
} day_panel_t;

typedef struct {
    day_face_t face;
    bool turning;       /* On or Past stop with a turned stop time not yet sent: the drum shows it in the centre */
    day_panel_t top, mid, low;   /* top only while turning */
    char hint[48];      /* the face's controls, above the panels; empty while turning */
    bool marks;         /* 00 06 12 18 */
    bool off_ring;      /* the thin dark ring of the Off face instead of the track */
    int n_segs;
    day_seg_t seg[DAY_SEGS];
    int tick;           /* stop mark, DAY_NONE none */
    uint32_t tick_rgb;
    int dot;            /* head, DAY_NONE none */
    uint32_t dot_rgb;
    bool glow, moon, check;
} day_model_t;

/* note (offline, an action's error) replaces the low panel; NULL none. */
void day_model(const day_ui_t *ui, const day_in_t *in, double t, const char *note, day_model_t *out);

/* The text drum: panels sit on a vertical drum seen from the front. Slot u = 0 is the centre (value 96 px over a
   30 px caption), u = +1 the slot below and u = -1 the one above (30 / 24 px, greyer; above also dimmer). */
#define DAY_DRUM_Y 255          /* value baseline in the centre slot, px */
#define DAY_DRUM_SLOT 105       /* value baseline of slot +1 below the centre, px */
#define DAY_DRUM_DEG 60.0f      /* drum angle between slots */
#define DAY_ROLL_S 0.6
#define DAY_DRUM_SLOTS 5

typedef struct {
    int val_px, cap_px;         /* a compiled size: values 30 36 48 96 (digits) or 24 30 48 (words), captions 24 30 */
    int val_y, cap_y;           /* baselines, px from the top of the 466 frame */
    uint32_t rgb;
    uint8_t opa;
} day_pose_t;

/* u: slot position (fractional while rolling); fade: extra opacity 0-1. */
void day_panel_pose(const day_panel_t *p, float u, float fade, day_pose_t *out);

typedef enum { DAY_ROLL_NONE, DAY_ROLL_TURN, DAY_ROLL_BACK, DAY_ROLL_FACE } day_roll_kind_t;

typedef struct {
    day_roll_kind_t kind;
    double t0;
    day_panel_t top, mid, low;  /* the model the roll leaves */
    bool had_top;
} day_roll_t;

typedef struct {
    day_panel_t p;
    float u, fade;
} day_slot_t;

/* Starts the roll from prev to next, if their faces differ (On and Past stop count as one while turning) or the turn
   starts or ends; else leaves r as it is. first: nothing was drawn yet, so no roll. */
void day_roll_start(day_roll_t *r, const day_model_t *prev, const day_model_t *next, bool first, double t);
/* The panels to draw at t for model m, at most DAY_DRUM_SLOTS; ends the roll after DAY_ROLL_S. */
int day_roll_slots(day_roll_t *r, const day_model_t *m, double t, day_slot_t out[DAY_DRUM_SLOTS]);

#ifdef __cplusplus
}
#endif
