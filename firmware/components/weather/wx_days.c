#include "wx_days.h"

#include <math.h>
#include <string.h>

#include "wx_clock.h"
#include "wx_temp.h"

int wx_weekday(long long t, bool has_offset, int utc_offset) { return wx_local_wday(t + 43200, has_offset ? utc_offset : 0); }

wx_icon_t wx_days_icon(int face)
{
    switch (face) {
    case WX_CLEAR_DAY:
    case WX_CLEAR_NIGHT: return WX_ICON_SUN;
    case WX_PARTLY_CLOUDY: return WX_ICON_PARTLY;
    case WX_FOG: return WX_ICON_FOG;
    case WX_RAIN: return WX_ICON_RAIN;
    case WX_SNOW: return WX_ICON_SNOW;
    case WX_STORM: return WX_ICON_STORM;
    default: return WX_ICON_CLOUD;
    }
}

bool wx_days_wet(int icon) { return icon == WX_ICON_RAIN || icon == WX_ICON_SNOW || icon == WX_ICON_STORM; }

static short clamp_c(int v) { return (short)(v < -99 ? -99 : v > 99 ? 99 : v); }

void wx_days_from(const wx_obs_t *o, wx_days_t *d)
{
    memset(d, 0, sizeof *d);
    d->on = true;
    if (!o || !o->valid || !o->enabled) return;
    long long now = 0;
    wx_knob_now(o, &now);
    int n = o->day_n < 0 ? 0 : o->day_n > WX_DAYS ? WX_DAYS : o->day_n;
    for (int i = 0; i < n; i++) {
        const wx_day_t *s = &o->day[i];
        long long end = s->end > s->t ? s->end : s->t + 86400;
        if (now >= end) continue;
        wx_days_row_t *r = &d->row[d->n++];
        r->wday = (signed char)wx_weekday(s->t, o->has_utc_offset, o->utc_offset);
        r->icon = (signed char)wx_days_icon(s->face);
        short a = clamp_c(s->min_c), b = clamp_c(s->max_c);
        r->lo = a < b ? a : b;
        r->hi = a < b ? b : a;
        r->pop = (signed char)(wx_days_wet(r->icon) && s->pop >= 0 ? (s->pop > 100 ? 100 : s->pop) : -1);
    }
    d->have = d->n > 0;
    if (!d->have) return;
    d->amin = d->row[0].lo;
    d->amax = d->row[0].hi;
    for (int i = 1; i < d->n; i++) {
        if (d->row[i].lo < d->amin) d->amin = d->row[i].lo;
        if (d->row[i].hi > d->amax) d->amax = d->row[i].hi;
    }
    if (d->amin == d->amax) {
        d->amin--;
        d->amax++;
    }
}

bool wx_days_same(const wx_days_t *a, const wx_days_t *b)
{
    if (a->on != b->on || a->have != b->have || a->n != b->n || a->amin != b->amin || a->amax != b->amax) return false;
    for (int i = 0; i < a->n && i < WX_DAYS; i++) {
        const wx_days_row_t *x = &a->row[i], *y = &b->row[i];
        if (x->wday != y->wday || x->icon != y->icon || x->pop != y->pop || x->lo != y->lo || x->hi != y->hi) return false;
    }
    return true;
}

int wx_days_row_y(int n, int i) { return WX_DAYS_ROW_Y + (2 * i - (n - 1)) * WX_DAYS_PITCH / 2; }

void wx_days_span(const wx_days_t *d, int i, int bar_w, float *x0, float *x1)
{
    const wx_days_row_t *r = &d->row[i];
    float k = (float)bar_w / (float)(d->amax - d->amin);
    float a = (float)(r->lo - d->amin) * k, b = (float)(r->hi - d->amin) * k;
    if (b - a < WX_DAYS_BAR_H) {
        float c = (a + b) / 2;
        a = c - WX_DAYS_BAR_H / 2.0f;
        b = c + WX_DAYS_BAR_H / 2.0f;
        if (a < 0) {
            b -= a;
            a = 0;
        }
        if (b > (float)bar_w) {
            a -= b - (float)bar_w;
            b = (float)bar_w;
        }
    }
    *x0 = a;
    *x1 = b;
}

int wx_days_deg_w(int v)
{
    static const int DIGIT[10] = {24, 13, 21, 21, 24, 21, 22, 22, 23, 22};
    int a = v < 0 ? -v : v, w = 15 + (v < 0 ? 21 : 0);
    do {
        w += DIGIT[a % 10];
        a /= 10;
    } while (a);
    return w;
}

int wx_days_name_w(int wday)
{
    static const int W[7] = {59, 68, 55, 70, 58, 39, 49};
    return W[((wday % 7) + 7) % 7];
}

static int imax(int a, int b) { return a > b ? a : b; }

void wx_days_cols(const wx_days_t *d, wx_days_cols_t *c)
{
    int dw = 0, lw = 0, hw = 0;
    for (int i = 0; i < d->n && i < WX_DAYS; i++) {
        dw = imax(dw, wx_days_name_w(d->row[i].wday));
        lw = imax(lw, wx_days_deg_w(d->row[i].lo));
        hw = imax(hw, wx_days_deg_w(d->row[i].hi));
    }
    int day = imax(WX_DAYS_DAY_W, dw), lo = imax(WX_DAYS_TEMP_W, lw), hi = imax(WX_DAYS_TEMP_W, hw);
    int bar = WX_DAYS_BAR_W - (day - WX_DAYS_DAY_W) - (lo - WX_DAYS_TEMP_W) - (hi - WX_DAYS_TEMP_W);
    int total = day + WX_DAYS_ICON + lo + bar + hi + 4 * WX_DAYS_GAP;
    c->x0 = (WX_FACE - total) / 2;
    c->day_x = c->x0;
    c->day_w = day;
    c->icon_x = c->day_x + day + WX_DAYS_GAP;
    c->lo_x = c->icon_x + WX_DAYS_ICON + WX_DAYS_GAP;
    c->lo_w = lo;
    c->bar_x = c->lo_x + lo + WX_DAYS_GAP;
    c->bar_w = bar;
    c->hi_x = c->bar_x + bar + WX_DAYS_GAP;
    c->hi_w = hi;
    c->x1 = c->hi_x + hi;
}

void wx_days_layout(const wx_days_t *d, wx_days_layout_t *l)
{
    memset(l, 0, sizeof *l);
    wx_days_cols(d, &l->c);
    l->n = d->n < 0 ? 0 : d->n > WX_DAYS ? WX_DAYS : d->n;
    for (int i = 0; i < l->n; i++) l->y[i] = wx_days_row_y(l->n, i);
    l->sprite = (wx_box_t){(l->c.icon_x - 2) & ~1, 0, l->c.bar_x + l->c.bar_w + 2, 2 * WX_DAYS_ROW_UP};
    l->sprite.x1 += l->sprite.x1 & 1;
    int px = (l->sprite.x1 - l->sprite.x0) * (l->sprite.y1 - l->sprite.y0);
    for (int i = 0; i < l->n; i++) l->at[i] = i * px * 3;
    l->total = l->n * px * 3;
    if (!l->n) return;
    l->area = (wx_box_t){(l->c.x0 - 4) & ~1, l->y[0] - WX_DAYS_ROW_UP, l->c.x1 + 4,
                         l->y[l->n - 1] - WX_DAYS_ROW_UP + WX_DAYS_ROW_H + WX_DAYS_SLIDE};
    l->area.x1 += l->area.x1 & 1;
}

void wx_days_arrive(int i, double t, int *opa, int *dy)
{
    double p = (t - i * WX_DAYS_STAGGER_S) / WX_DAYS_IN_S;
    double u = !(p > 0) ? 0 : p >= 1 ? 1 : 1 - (1 - p) * (1 - p) * (1 - p);
    *opa = (int)lround(255 * u);
    *dy = (int)lround(WX_DAYS_SLIDE * (1 - u));
}

void wx_days_frame(const wx_days_t *d, double t, wx_days_frame_t *f)
{
    memset(f, 0, sizeof *f);
    for (int i = 0; i < d->n && i < WX_DAYS; i++) {
        wx_days_arrive(i, t, &f->opa[i], &f->dy[i]);
        f->lo[i] = wx_count_value(d->row[i].lo, wx_count_step(wx_count_n(d->row[i].lo), t));
        f->hi[i] = wx_count_value(d->row[i].hi, wx_count_step(wx_count_n(d->row[i].hi), t));
    }
}

wx_box_t wx_days_row_box(const wx_days_layout_t *l, int i, int dy)
{
    int y = l->y[i] - WX_DAYS_ROW_UP + dy;
    return (wx_box_t){l->area.x0, y, l->area.x1, y + WX_DAYS_ROW_H};
}
