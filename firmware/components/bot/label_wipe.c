#include "label_wipe.h"

#include <math.h>
#include <string.h>

void lw_clear(label_wipe_t *w) { memset(w, 0, sizeof *w); }

bool lw_has(const label_wipe_t *w, int bin) { return (w->bits[bin >> 3] >> (bin & 7)) & 1; }

static int wrap_bin(int b) { return ((b % LW_BINS) + LW_BINS) % LW_BINS; }

int lw_bin(double dx, double dy)
{
    double deg = atan2(dy, dx) * 180 / M_PI;
    return wrap_bin((int)floor(deg));
}

static double unwrap(double deg, double ref)
{
    while (deg - ref > 180) deg -= 360;
    while (deg - ref < -180) deg += 360;
    return deg;
}

bool lw_span(const lw_stroke_t *s, int ns, double cx, double cy, double *from_deg, double *to_deg, double *outer)
{
    bool first = true;
    double ref = 0, lo = 0, hi = 0, r = 0;
    for (int k = 0; k < ns; k++)
        for (int i = 0; i < s[k].n; i++) {
            double dx = s[k].x[i] - cx, dy = s[k].y[i] - cy, d = hypot(dx, dy);
            if (d <= s[k].hw) return false;
            double a = atan2(dy, dx) * 180 / M_PI, half = asin(s[k].hw / d) * 180 / M_PI;
            if (first) ref = a, lo = a - half, hi = a + half, first = false;
            a = unwrap(a, ref);
            lo = fmin(lo, a - half);
            hi = fmax(hi, a + half);
            r = fmax(r, d + s[k].hw);
        }
    if (first) return false;
    if (hi - lo >= 360) hi = lo + 359.999;
    *from_deg = lo;
    *to_deg = hi;
    *outer = r;
    return true;
}

int lw_add(label_wipe_t *w, double from_deg, double to_deg, uint16_t *added, int cap)
{
    int n = 0;
    int b0 = (int)floor(from_deg), b1 = (int)floor(to_deg);
    if (b1 - b0 >= LW_BINS) b1 = b0 + LW_BINS - 1;
    for (int b = b0; b <= b1; b++) {
        int k = wrap_bin(b);
        if (lw_has(w, k)) continue;
        w->bits[k >> 3] |= (uint8_t)(1u << (k & 7));
        w->count++;
        if (n < cap) added[n] = (uint16_t)k;
        n++;
    }
    return n;
}
