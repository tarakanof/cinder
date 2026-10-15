#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TS_W 472
#define TS_H 466
#define TS_CX (TS_W / 2)
#define TS_CY (TS_H / 2)
#define TS_R (TS_H / 2)
#define TS_TAP_SLOP_PX 20
#define TS_SWIPE_MIN_PX 60
#define TS_SWIPE_MAX_S 0.8
#define TS_STALE_S 0.15
#define TS_JUMP_PX 120
#define TS_JUMP_PX_PER_MS 8
#define TS_MISS_S 0.008
#define TS_WIPE_S 0.18
#define TS_WIPE_BAND_PX 32
#define TS_DOTS_S 1.0
#define TS_CTL_PX 88
#define TS_CTL_GAP 40
#define TS_CTL_Y TS_CY
#define TS_CTL_DX (TS_CTL_PX + TS_CTL_GAP)
#define TS_CTL_SPLIT (TS_CTL_DX / 2)
#define TS_CTL_BAND (TS_CTL_PX / 2)
#define TS_ZONES 4
#define TS_TAP_BITS 8

/* TS_NEXT / TS_PREV: swipe up / down. TS_LEFT / TS_RIGHT: swipe left / right. */
typedef enum { TS_NONE, TS_TAP, TS_NEXT, TS_PREV, TS_LEFT, TS_RIGHT } ts_result_t;
typedef enum { TS_ZONE_NONE, TS_ZONE_PREV, TS_ZONE_PLAY, TS_ZONE_NEXT } ts_zone_t;

/* Coordinates in LVGL screen px (472x466 frame), t in seconds. Not thread-safe: the LVGL task only. */
typedef struct {
    bool down, swipe, in_circle, missed;
    int x0, y0, x, y;
    int max_d2;
    double t0, report_t;
} ts_touch_t;

/* swipe false: the press is a tap at once and its release does nothing. */
ts_result_t ts_press(ts_touch_t *s, int x, int y, double t, bool swipe);
/* report_t: last controller report; cancels after TS_STALE_S without one, or on a jump over max(TS_JUMP_PX, TS_JUMP_PX_PER_MS x gap) (TS_JUMP_PX after a read TS_MISS_S past the last report). */
void ts_move(ts_touch_t *s, int x, int y, double report_t, double now);
ts_result_t ts_release(ts_touch_t *s, int x, int y, double report_t, double now);
void ts_cancel(ts_touch_t *s);
/* The music page's control under a tap at x, y: controls TS_CTL_PX wide, TS_CTL_GAP apart, centred on (TS_CX, TS_CTL_Y); the zones split halfway between them and end at the discs' top and bottom. */
ts_zone_t ts_tap_zone(int x, int y);
/* Tap counts per zone packed in one word (TS_TAP_BITS each, < 128 per zone), so a count and its zone move together: add ts_tap_word to a shared word, take it whole, split it with ts_tap_counts (returns the total). */
unsigned ts_tap_word(ts_zone_t zone, int n);
int ts_tap_counts(unsigned word, int counts[TS_ZONES]);

int ts_page_step(int pos, int n, int steps);

/* Wipe edge in next-page rows (black above, then a fading band): TS_H at the start, -TS_WIPE_BAND_PX when done. */
int ts_wipe_edge(double since_s);
/* Opacity 0-255 of a row at dy px below the edge, 0 <= dy < TS_WIPE_BAND_PX. */
int ts_wipe_band_opa(int dy);
/* Rows [a, b) in next-page rows to screen rows y1..y2 (inclusive) for dir (1 next, -1 previous), clipped; false when empty. */
bool ts_wipe_span(int a, int b, int dir, int *y1, int *y2);
/* Screen rows to redraw when the edge moves from prev to e. */
bool ts_wipe_rows(int prev, int e, int dir, int *y1, int *y2);

#ifdef __cplusplus
}
#endif
