#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Seconds: the settle shows variant -1, then +1 until RIM_SETTLE_S; none once the variants come later than RIM_SETTLE_LATE_S after the swap. */
#define RIM_SETTLE_M1_S 0.06
#define RIM_SETTLE_S 0.14
#define RIM_SETTLE_LATE_S 1.0

/* Outlines are bot_rim_key values, -1 none. */
typedef struct {
    int job, shown;
    bool own;
    bool settle_due;
    double swap_t, settle_t;
} rim_beat_t;

typedef struct {
    bool start; /* render job's base into the spare slot, then its variants */
    bool swap;  /* show job's base now */
    bool own;   /* the shown outline is job's: its ready variants may show; else only the base */
    int settle; /* -1 or +1: the variant step to show instead of the base, 0 none */
} rim_beat_out_t;

void rim_beat_init(rim_beat_t *r);
/* A new job when want differs from it (true: start rendering want); rim_beat_step does this first. */
bool rim_beat_want(rim_beat_t *r, int want);
/* want: the pose's mood outline; show: its face's; ready: job's base is rendered; m1, p1: its variants -1 and +1 are;
   still: the pose asks for the base variant. */
void rim_beat_step(rim_beat_t *r, int want, int show, bool ready, bool m1, bool p1, bool still, double t, rim_beat_out_t *out);
/* Whether job is mood's outline and its base can show (rendered: ready, or the outline on screen already). */
bool rim_beat_ready(const rim_beat_t *r, int mood, bool ready);

#ifdef __cplusplus
}
#endif
