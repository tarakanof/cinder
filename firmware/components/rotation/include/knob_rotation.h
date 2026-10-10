#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Driver frame 472x466; the glass shows frame/GRAM columns 6..471 and rows 0..465 (docs/board.md). */
#define KR_FRAME_W 472
#define KR_FRAME_H 466
#define KR_VIS_X0 6
/* MX/MY mirror over 480 columns and 480 rows, checked on the knob 2026-10-10 (docs/features.md, "Display rotation"). */
#define KR_MIRROR_W 480
#define KR_MIRROR_H 480

#define KR_N_SUPPORTED 2
extern const int KR_SUPPORTED[KR_N_SUPPORTED];

typedef struct {
    bool mirror_x, mirror_y;
    int gap_x, gap_y;
    int min_x, min_y;
} kr_panel_t;

bool kr_valid(int deg);
/* A supported rotation as is; anything else 0. */
int kr_effective(int deg);
kr_panel_t kr_panel(int deg);
/* Mirror window in columns/rows (even); kr_panel uses KR_MIRROR_W/H. */
kr_panel_t kr_panel_in(int deg, int mirror_w, int mirror_h);
/* Dirty area to CO5300 windows: even start, odd end, inside [min_x/min_y, frame end]. */
void kr_round(const kr_panel_t *p, int *x1, int *y1, int *x2, int *y2);
/* Raw touch point (frame coordinates at 0 degrees) to frame coordinates at deg, clamped to the frame. */
void kr_touch(int deg, int *x, int *y);

#define KR_UNKNOWN (-1)
#define KR_RETRY_MIN_MS 100
#define KR_RETRY_MAX_MS 5000

/* Writes gap and mirror for deg; 0 on success. */
typedef int (*kr_orient_fn)(void *ctx, int deg);

typedef enum {
    KR_IDLE,
    KR_APPLIED,
    KR_ROLLED_BACK,
    KR_LOST,
} kr_result_t;

/* shown: what the panel has (KR_UNKNOWN after a failed rollback); touch: the last orientation written successfully. */
typedef struct {
    int want, shown, touch;
    int64_t retry_ms;
    int backoff_ms;
} kr_state_t;

#define KR_STATE_INIT {.backoff_ms = KR_RETRY_MIN_MS}

void kr_state_init(kr_state_t *s);
void kr_want(kr_state_t *s, int deg);
kr_result_t kr_step(kr_state_t *s, int64_t now_ms, kr_orient_fn orient, void *ctx);

#ifdef __cplusplus
}
#endif
