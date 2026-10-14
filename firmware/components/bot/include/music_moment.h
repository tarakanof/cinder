#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MM_FPS 30
#define MM_PRE_S 0.5
#define MM_IN 16
#define MM_BEAT 18
#define MM_BEATS 10
#define MM_OUT (MM_IN + MM_BEAT * MM_BEATS)
#define MM_OUT_LEN 12
#define MM_TICKS (MM_OUT + MM_OUT_LEN)
#define MM_DROP 24
#define MM_NOTES 4
#define MM_NOTE_CYCLE 72
#define MM_NOTE_RISE 70
#define MM_SETTLE_S 6.0
#define MM_GAP_S 300.0
#define MM_SAME_S 1200.0
#define MM_RESUME_S 600.0
#define MM_RGB 0xF2A07E

#define MM_BAND_CX 236
#define MM_BAND_CY 236
#define MM_BAND_R 121
#define MM_BAND_HW 6
#define MM_BAND_X 108
#define MM_BAND_Y 108
#define MM_BAND_W 256
#define MM_BAND_H 136
#define MM_CUP_W 36
#define MM_CUP_H 72
#define MM_CUP_L_X 99
#define MM_CUP_R_X 337
#define MM_CUP_Y 214
#define MM_CUP_BOB 2
#define MM_NOTE_W 24
#define MM_NOTE_H 32
#define MM_NOTE_OX 8
#define MM_NOTE_OY 25
#define MM_BOB_X0 98
#define MM_BOB_Y0 174
#define MM_BOB_X1 373
#define MM_BOB_Y1 293

#define MM_BAND_AT 0
#define MM_CUP_AT(i) (MM_BAND_W * MM_BAND_H + (i) * MM_CUP_W * MM_CUP_H * 3)
#define MM_NOTE_AT MM_CUP_AT(2)
#define MM_ARENA (MM_NOTE_AT + MM_NOTE_W * MM_NOTE_H)

/* Track identity as np_same_track sees it (title, artist, album, source); never 0. */
uint32_t mm_track_key(const char *title, const char *artist, const char *album, const char *source);

typedef struct {
    bool base;
    uint32_t seen;
    double last_at, stop_at, steady_at;
    int mood;
    bool was_shown;
} mm_trigger_t;

typedef struct {
    double t;         /* monotonic s */
    bool have;        /* the view carries a nowplaying block */
    bool playing;
    uint32_t track;   /* mm_track_key */
    int bot_mood;     /* any change restarts the settle */
    bool rest;        /* idle, sleepy or done */
    bool shown;       /* the bot page is on screen and the bot awake */
    bool blocked;     /* offline, quiet hours, a glint chase, a demo or a weather moment */
    double other_at;  /* the last weather moment's start */
} mm_in_t;

void mm_init(mm_trigger_t *tr);
/* Every frame. True when the listening moment is due now; it counts only once mm_shown records it. */
bool mm_tick(mm_trigger_t *tr, const mm_in_t *in);
void mm_shown(mm_trigger_t *tr, uint32_t track, double t);
/* Within MM_GAP_S of the last music moment: a weather moment waits. */
bool mm_recent(const mm_trigger_t *tr, double t);

typedef struct {
    int x, y, opa;
} mm_note_t;

typedef struct {
    int phones_dy;  /* band and cups, screen px from rest, negative above */
    int phones_opa; /* 0..255 */
    int cup_dy;     /* the cups' bob on top, screen px */
    float eye_px;   /* the eyes' bob, screen px, positive down */
    mm_note_t note[MM_NOTES]; /* top-left of the note image, screen px */
} mm_frame_t;

/* Tick j at MM_FPS from the start (after the pre-roll); j < 0 or >= MM_TICKS: nothing shown. */
void mm_frame(int j, mm_frame_t *f);

typedef struct {
    int x0, y0, x1, y1;
} mm_box_t;

/* The one box to redraw from the frame on screen a (NULL: nothing shown) to b, inclusive and 2 px aligned; false: none. */
bool mm_diff(const mm_frame_t *a, const mm_frame_t *b, mm_box_t *box);
/* Every pixel any frame draws: the draw object's coordinates. */
void mm_extent(mm_box_t *box);
/* A cut at tick j goes on from the returned tick: the phones leave from no brighter than shown, notes stop. */
int mm_abort_tick(int j);

typedef enum { MM_HAPPY, MM_END } mm_op_t;
typedef struct {
    float t;
    mm_op_t op;
} mm_cue_t;
/* The eye acting after bot_act_begin, in time order relative to the first tick. */
const mm_cue_t *mm_script(int *n);

/* The band (A8), the two cups (RGB565A8) and the note (A8) into arena (MM_ARENA bytes). Pure. */
void mm_paint(uint8_t *arena);

#ifdef __cplusplus
}
#endif
