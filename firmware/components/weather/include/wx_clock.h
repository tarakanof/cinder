#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "HH:MM" of Unix seconds t at utc_offset s. */
void wx_hhmm(long long t, int utc_offset, char *buf, size_t n);
/* 0 Sunday .. 6 Saturday of the local day holding t. */
int wx_local_wday(long long t, int utc_offset);
/* "Sun" .. "Sat"; any integer wraps. */
const char *wx_weekday_name(int wday);
/* Centre of dot k of n, pitch px apart, on an arc of radius r at the bottom of a face centred at (cx, cy). */
void wx_arc_dot_xy(int k, int n, float r, float pitch, float cx, float cy, float *x, float *y);

#ifdef __cplusplus
}
#endif
