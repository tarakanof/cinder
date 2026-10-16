#include "wx_clock.h"

#include <math.h>
#include <stdio.h>

#define DAY_S 86400LL

static long long day_of(long long s) { return s >= 0 ? s / DAY_S : -((-s + DAY_S - 1) / DAY_S); }

void wx_hhmm(long long t, int utc_offset, char *buf, size_t n)
{
    long long s = t + utc_offset, m = (s - day_of(s) * DAY_S) / 60;
    snprintf(buf, n, "%02d:%02d", (int)(m / 60), (int)(m % 60));
}

int wx_local_wday(long long t, int utc_offset) { return (int)(((day_of(t + utc_offset) + 4) % 7 + 7) % 7); }

void wx_arc_dot_xy(int k, int n, float r, float pitch, float cx, float cy, float *x, float *y)
{
    float a = ((float)k - (float)(n - 1) / 2) * pitch / r;
    *x = cx + r * sinf(a);
    *y = cy + r * cosf(a);
}

const char *wx_weekday_name(int wday)
{
    static const char *const N[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return N[((wday % 7) + 7) % 7];
}
