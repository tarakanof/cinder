#include "touch_swipe.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

ts_result_t ts_press(ts_touch_t *s, int x, int y, double t, bool swipe)
{
    memset(s, 0, sizeof *s);
    int dx = x - TS_CX, dy = y - TS_CY;
    s->down = swipe;
    s->swipe = swipe;
    s->in_circle = dx * dx + dy * dy <= TS_R * TS_R;
    s->x0 = s->x = x;
    s->y0 = s->y = y;
    s->t0 = s->report_t = t;
    return swipe ? TS_NONE : TS_TAP;
}

void ts_move(ts_touch_t *s, int x, int y, double report_t, double now)
{
    if (!s->down) return;
    if (report_t > s->report_t) {
        double gap = report_t - s->report_t, lim = gap * 1000 * TS_JUMP_PX_PER_MS;
        if (s->missed || lim < TS_JUMP_PX) lim = TS_JUMP_PX;
        double jx = x - s->x, jy = y - s->y;
        if (gap > TS_STALE_S || jx * jx + jy * jy > lim * lim) {
            s->down = false;
            return;
        }
        s->missed = false;
        s->report_t = report_t;
        s->x = x;
        s->y = y;
        int dx = x - s->x0, dy = y - s->y0, d2 = dx * dx + dy * dy;
        if (d2 > s->max_d2) s->max_d2 = d2;
    }
    else if (now - s->report_t > TS_MISS_S) s->missed = true;
    if (now - s->report_t > TS_STALE_S) s->down = false;
}

ts_result_t ts_release(ts_touch_t *s, int x, int y, double report_t, double now)
{
    ts_move(s, x, y, report_t, now);
    if (!s->down) return TS_NONE;
    s->down = false;
    double t = now;
    if (s->max_d2 <= TS_TAP_SLOP_PX * TS_TAP_SLOP_PX) return TS_TAP;
    int dx = s->x - s->x0, dy = s->y - s->y0;
    if (!s->in_circle || t - s->t0 > TS_SWIPE_MAX_S) return TS_NONE;
    if (abs(dy) < TS_SWIPE_MIN_PX || 2 * abs(dy) < 3 * abs(dx)) return TS_NONE;
    return dy < 0 ? TS_NEXT : TS_PREV;
}

void ts_cancel(ts_touch_t *s) { s->down = false; }

int ts_page_step(int pos, int n, int steps)
{
    if (n <= 0) return 0;
    return ((pos + steps) % n + n) % n;
}

int ts_wipe_edge(double since_s)
{
    double p = since_s <= 0 ? 0 : since_s >= TS_WIPE_S ? 1 : since_s / TS_WIPE_S;
    double e = 1 - (1 - p) * (1 - p);
    return TS_H - (int)lround(e * (TS_H + TS_WIPE_BAND_PX));
}

int ts_wipe_band_opa(int dy)
{
    if (dy < 0) return 255;
    if (dy >= TS_WIPE_BAND_PX) return 0;
    return 255 * (TS_WIPE_BAND_PX - dy) / TS_WIPE_BAND_PX;
}

bool ts_wipe_span(int a, int b, int dir, int *y1, int *y2)
{
    if (dir < 0) {
        int t = TS_H - b;
        b = TS_H - a;
        a = t;
    }
    if (a < 0) a = 0;
    if (b > TS_H) b = TS_H;
    *y1 = a;
    *y2 = b - 1;
    return b > a;
}

bool ts_wipe_rows(int prev, int e, int dir, int *y1, int *y2)
{
    if (e >= prev) return false;
    return ts_wipe_span(e, prev + TS_WIPE_BAND_PX, dir, y1, y2);
}
