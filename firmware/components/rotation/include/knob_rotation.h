#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Driver frame 472x466 inside the CO5300's 480x480 GRAM; visible GRAM columns 6..471, rows 0..465 (docs/board.md). */
#define KR_FRAME_W 472
#define KR_FRAME_H 466
#define KR_GRAM 480
#define KR_VIS_X0 6

#define KR_N_SUPPORTED 2
extern const int KR_SUPPORTED[KR_N_SUPPORTED];

typedef struct {
    bool mirror_x, mirror_y;
    int gap_x, gap_y;
} kr_panel_t;

bool kr_valid(int deg);
/* A supported rotation as is; anything else 0. */
int kr_effective(int deg);
kr_panel_t kr_panel(int deg);
/* Raw touch point (frame coordinates at 0 degrees) to frame coordinates at deg, clamped to the frame. */
void kr_touch(int deg, int *x, int *y);

#ifdef __cplusplus
}
#endif
