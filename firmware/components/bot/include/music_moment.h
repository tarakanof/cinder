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
#define MM_BOB_STEP 2
#define MM_SETTLE_STEPS 3
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

/* 0 without a title (nothing or metadata still loading). */
uint32_t mm_track_key(const char *title, const char *artist, const char *album, const char *source);

typedef struct {
    bool base;
    uint32_t cur, pending;
    double last_at, stop_at, steady_at;
    int mood;
    bool was_shown;
} mm_trigger_t;

typedef struct {
    double t;
    bool have;
    bool playing;
    uint32_t track;
    int bot_mood;
    bool rest;
    bool shown;
    bool blocked;
    double other_at;
} mm_in_t;

void mm_init(mm_trigger_t *tr);
/* True when due now; it counts only once mm_shown records it. */
bool mm_tick(mm_trigger_t *tr, const mm_in_t *in);
void mm_shown(mm_trigger_t *tr, double t);
bool mm_recent(const mm_trigger_t *tr, double t);

typedef struct {
    int x, y, opa;
} mm_note_t;

/* Screen px; phones_dy negative is up, eye_px positive is down. */
typedef struct {
    int phones_dy;
    int phones_opa;
    int cup_dy;
    float eye_px;
    mm_note_t note[MM_NOTES];
} mm_frame_t;

/* j: MM_FPS ticks after the pre-roll; outside 0..MM_TICKS-1 nothing is shown. */
void mm_frame(int j, mm_frame_t *f);

typedef struct {
    int x0, y0, x1, y1;
} mm_box_t;

/* a NULL: nothing shown. The box is inclusive and 2 px aligned. */
bool mm_diff(const mm_frame_t *a, const mm_frame_t *b, mm_box_t *box);
void mm_extent(mm_box_t *box);
int mm_abort_tick(int j);
/* The eyes' bob ticks after an abort that cut it at from px. */
float mm_eye_settle(float from, int ticks);

typedef enum { MM_HAPPY, MM_END } mm_op_t;
typedef struct {
    float t;
    mm_op_t op;
} mm_cue_t;
const mm_cue_t *mm_script(int *n);

/* arena: MM_ARENA bytes. */
void mm_paint(uint8_t *arena);

#ifdef __cplusplus
}
#endif
