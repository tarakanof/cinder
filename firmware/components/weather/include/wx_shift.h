#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WX_SHIFT_S 120.0

typedef struct {
    int8_t x, y;
} wx_shift_pt_t;

typedef struct {
    const wx_shift_pt_t *pt;
    int n;
    double period_s;
} wx_shift_path_t;

typedef struct {
    double t;
    int i;
} wx_shift_t;

extern const wx_shift_path_t WX_SHIFT_SQUARE;
extern const wx_shift_path_t WX_SHIFT_ORBIT;

/* True on a step to p's next point (at most one per call); a non-finite or negative dt counts as 0. */
bool wx_shift_tick(wx_shift_t *s, const wx_shift_path_t *p, double dt);
void wx_shift_xy(const wx_shift_t *s, const wx_shift_path_t *p, int *dx, int *dy);

#ifdef __cplusplus
}
#endif
