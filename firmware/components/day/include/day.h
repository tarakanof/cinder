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

#define DAY_TURN_DEBOUNCE_S 1.5
#define DAY_STOP_HOLD_S 5.0
#define DAY_SET_IDLE_S 60.0
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
#define DAY_MARK 0x3A3A3A

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
/* Ember's day-clock UTC offset in minutes, a multiple of 15, from an on or break block seen at Unix utc_s
   (started + on_min + breaks is Ember-local now, each floored to the minute). False without such a block, and on a
   break with DAY_BREAKS finished breaks (Ember sends only the latest, so the history may be cut). */
bool day_offset_from_block(const day_block_t *b, long long utc_s, int *offset_min);
/* rgb with each channel times k, rounded. */
uint32_t day_scale(uint32_t rgb, float k);

typedef enum {
    DAY_FACE_WAIT,
    DAY_FACE_OFF,
    DAY_FACE_SET,
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

typedef struct {
    const day_block_t *b;   /* NULL: no block */
    bool fed;               /* a view came; with b NULL, Ember sent no day block */
    double age_s;           /* since the block came */
    int now_min;            /* local time; DAY_NONE when the knob does not know it */
    bool now_exact;         /* now_min is in Ember's day-clock zone (else the weather's: display only) */
    bool busy;              /* an action is on its way or not yet in a view */
} day_in_t;

typedef struct {
    bool set;
    int set_stop;
    double set_at;
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
/* Every frame, before input: Set closes once the day runs or after DAY_SET_IDLE_S, the offer and summary follow the view. */
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

typedef struct {
    day_face_t face;
    char hero[16];
    bool hero_digits;   /* the 96 px digit font; else the 48 px text font */
    uint32_t hero_rgb;
    char line2[40];
    uint32_t line2_rgb;
    char line3[64];
    uint32_t line3_rgb;
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

/* note (offline, an action's error) replaces the third line; NULL none. */
void day_model(const day_ui_t *ui, const day_in_t *in, double t, const char *note, day_model_t *out);

#ifdef __cplusplus
}
#endif
