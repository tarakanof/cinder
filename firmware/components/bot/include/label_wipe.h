#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LW_BINS 360

typedef struct {
    uint8_t bits[LW_BINS / 8];
    int count;
} label_wipe_t;

void lw_clear(label_wipe_t *w);
bool lw_has(const label_wipe_t *w, int bin);
/* dx, dy: screen px from the centre, y down; bins are degrees, 0 = 3 o'clock, clockwise. */
int lw_bin(double dx, double dy);

/* Angular span and outer radius of round-capped polylines (n points each, half width hw,
 * screen px around cx, cy): from_deg <= to_deg, to_deg - from_deg < 360. False when a point
 * sits on the centre. */
typedef struct {
    const double *x, *y;
    int n;
    double hw;
} lw_stroke_t;
bool lw_span(const lw_stroke_t *s, int ns, double cx, double cy, double *from_deg, double *to_deg, double *outer);

/* Mark the bins from from_deg to to_deg; the newly marked ones go to added (up to cap).
 * Returns how many were new. */
int lw_add(label_wipe_t *w, double from_deg, double to_deg, uint16_t *added, int cap);

#ifdef __cplusplus
}
#endif
