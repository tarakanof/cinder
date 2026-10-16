#include "wx_sun.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wx_cast.h"
#include "wx_clock.h"
#include "wx_temp.h"

#define BLUE_OUT 0x4A4FC8
#define BLUE_IN 0x3E6FD8
#define MAUVE 0x9A6A9E
#define DUSK_PINK 0xE08A5A
#define DAY_EDGE 0x7E5A20
#define DAY_NOON 0xB5832A
#define DAY_S 86400LL

static void keep(long long t[WX_SUN_N], int i, long long v, long long *lo, long long hi)
{
    if (v < *lo || v > hi) return;
    t[i] = v;
    *lo = v;
}

static void crossings(const wx_obs_t *o, long long t[WX_SUN_N])
{
    const long long r = o->sunrise, s = o->sunset;
    t[WX_SUN_RISE] = r;
    t[WX_SUN_SET] = s;
    long long lo = r - WX_SUN_SPAN_MAX_S;
    keep(t, WX_SUN_DAWN, o->twilight[0], &lo, r);
    keep(t, WX_SUN_BLUE_END, o->twilight[1], &lo, r);
    lo = r;
    keep(t, WX_SUN_GOLD_END, o->twilight[2], &lo, s);
    keep(t, WX_SUN_GOLD_START, o->twilight[3], &lo, s);
    lo = s;
    keep(t, WX_SUN_BLUE_START, o->twilight[4], &lo, s + WX_SUN_SPAN_MAX_S);
    keep(t, WX_SUN_DUSK, o->twilight[5], &lo, s + WX_SUN_SPAN_MAX_S);
}

void wx_sun_round(long long s, bool *hours, int *count)
{
    if (s > WX_SUN_COUNT_MAX_S) s = WX_SUN_COUNT_MAX_S;
    if (s >= 3600) {
        *hours = true;
        *count = (int)(s / 1800);
        return;
    }
    *hours = false;
    long long m = (s + 59) / 60;
    *count = m < 1 ? 1 : (int)m;
}

static void value_of(wx_sun_t *s, long long secs, bool left)
{
    wx_sun_round(secs, &s->hours, &s->count);
    s->unit = s->hours ? (left ? "h left" : "h") : (left ? "min left" : "min");
}

static void sub_at(wx_sun_t *s, const char *what, long long t, uint32_t rgb)
{
    char at[8];
    wx_hhmm(t, s->utc_offset, at, sizeof at);
    snprintf(s->sub, sizeof s->sub, "%s %s", what, at);
    s->sub_rgb = rgb;
}

void wx_sun_from(const wx_obs_t *o, wx_sun_t *s)
{
    memset(s, 0, sizeof *s);
    s->on = true;
    s->sub_rgb = WX_SUN_GREY_RGB;
    if (!o || !o->valid) return;
    long long now = 0;
    s->has_now = wx_knob_now(o, &now);
    s->have = o->sunrise > 0 && o->sunset > o->sunrise && o->sunset - o->sunrise < DAY_S && o->has_utc_offset;
    if (!s->have) {
        if (o->sunrise == 0 && o->sunset == 0 && o->moon_up >= 0 && o->has_sun_down) {
            s->polar = o->sun_down ? WX_SUN_POLAR_NIGHT : WX_SUN_POLAR_DAY;
            snprintf(s->sub, sizeof s->sub, "%s", o->sun_down ? "Polar night" : "Midnight sun");
        }
        return;
    }
    s->utc_offset = o->utc_offset;
    if (s->has_now) {
        long long day = ((o->sunrise + o->sunset) / 2 + s->utc_offset) / DAY_S, late = (now + s->utc_offset) / DAY_S - day;
        if (late < 0 || late > WX_SUN_ROLL_DAYS) {
            s->have = false;
            return;
        }
        late = (now + s->utc_offset - WX_SUN_ROLL_GRACE_S) / DAY_S - day;
        crossings(o, s->t);
        for (int i = 0; late > 0 && i < WX_SUN_N; i++)
            if (s->t[i]) s->t[i] += late * DAY_S;
    } else {
        crossings(o, s->t);
    }
    wx_hhmm(s->t[WX_SUN_RISE], s->utc_offset, s->rise, sizeof s->rise);
    wx_hhmm(s->t[WX_SUN_SET], s->utc_offset, s->set, sizeof s->set);
    if (!s->has_now) return;
    const long long r = s->t[WX_SUN_RISE], st = s->t[WX_SUN_SET], ge = s->t[WX_SUN_GOLD_END], gs = s->t[WX_SUN_GOLD_START];
    s->frac = wx_sun_frac(s, now);
    if (now < r) {
        s->phase = WX_SUN_BEFORE;
        value_of(s, r - now, false);
        sub_at(s, "until sunrise", r, WX_SUN_HZN_RGB);
    } else if (now >= st) {
        s->phase = WX_SUN_AFTER;
        value_of(s, st - r, false);
        snprintf(s->sub, sizeof s->sub, "of daylight today");
    } else {
        value_of(s, st - now, true);
        if (now < ge) {
            s->phase = WX_SUN_MORNING;
            sub_at(s, "golden hour to", ge, WX_SUN_GOLD_RGB);
        } else if (gs && now >= gs) {
            s->phase = WX_SUN_EVENING;
            sub_at(s, "sunset", st, WX_SUN_HZN_RGB);
        } else {
            s->phase = WX_SUN_DAY;
            if (gs) sub_at(s, "golden hour", gs, WX_SUN_GOLD_RGB);
            else sub_at(s, "sunset", st, WX_SUN_HZN_RGB);
        }
    }
}

bool wx_sun_same_scene(const wx_sun_t *a, const wx_sun_t *b)
{
    if (a->on != b->on || a->have != b->have || a->utc_offset != b->utc_offset) return false;
    for (int i = 0; i < WX_SUN_N; i++)
        if (a->t[i] != b->t[i]) return false;
    return true;
}

long long wx_sun_start(const wx_sun_t *s)
{
    for (int i = 0; i <= WX_SUN_RISE; i++)
        if (s->t[i]) return s->t[i];
    return 0;
}

long long wx_sun_end(const wx_sun_t *s)
{
    for (int i = WX_SUN_N - 1; i >= WX_SUN_SET; i--)
        if (s->t[i]) return s->t[i];
    return 0;
}

float wx_sun_frac(const wx_sun_t *s, long long t)
{
    long long a = wx_sun_start(s), b = wx_sun_end(s);
    if (b <= a) return 0;
    double f = (double)(t - a) / (double)(b - a);
    return f < 0 ? 0 : f > 1 ? 1 : (float)f;
}

typedef struct {
    long long t;
    uint32_t rgb;
} stop_t;

static uint32_t lerp(uint32_t a, uint32_t b, double f)
{
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        double ca = (double)((a >> sh) & 0xFF), cb = (double)((b >> sh) & 0xFF);
        out |= (uint32_t)lround(ca + (cb - ca) * f) << sh;
    }
    return out;
}

static void add(stop_t *st, int *n, long long anchor, long long d, uint32_t rgb)
{
    if (!anchor) return;
    long long t = anchor + d;
    if (*n && t <= st[*n - 1].t) return;
    st[(*n)++] = (stop_t){t, rgb};
}

static uint32_t day_edge(long long golden, long long noon)
{
    long long span = golden > noon ? golden - noon : noon - golden;
    return lerp(DAY_EDGE, DAY_NOON, span <= WX_SUN_BLEND_S ? 1 : (double)WX_SUN_BLEND_S / (double)span);
}

uint32_t wx_sun_rgb(const wx_sun_t *s, long long t)
{
    const long long *x = s->t, B = WX_SUN_BLEND_S;
    stop_t st[17];
    int n = 0;
    add(st, &n, x[WX_SUN_DAWN], 0, BLUE_OUT);
    add(st, &n, x[WX_SUN_BLUE_END], 0, BLUE_IN);
    add(st, &n, x[WX_SUN_RISE], -300, MAUVE);
    add(st, &n, x[WX_SUN_RISE], 360, DUSK_PINK);
    add(st, &n, x[WX_SUN_RISE], B, WX_SUN_HZN_RGB);
    add(st, &n, x[WX_SUN_GOLD_END], -B, WX_SUN_GOLD_RGB);
    long long noon = x[WX_SUN_RISE] && x[WX_SUN_SET] ? (x[WX_SUN_RISE] + x[WX_SUN_SET]) / 2 : 0;
    add(st, &n, x[WX_SUN_GOLD_END], B, day_edge(x[WX_SUN_GOLD_END], noon));
    add(st, &n, noon, 0, DAY_NOON);
    add(st, &n, x[WX_SUN_GOLD_START], -B, day_edge(x[WX_SUN_GOLD_START], noon));
    add(st, &n, x[WX_SUN_GOLD_START], B, WX_SUN_GOLD_RGB);
    add(st, &n, x[WX_SUN_SET], -B, WX_SUN_HZN_RGB);
    add(st, &n, x[WX_SUN_SET], -360, DUSK_PINK);
    add(st, &n, x[WX_SUN_SET], 300, MAUVE);
    add(st, &n, x[WX_SUN_BLUE_START], 0, BLUE_IN);
    add(st, &n, x[WX_SUN_DUSK], 0, BLUE_OUT);
    if (!n) return DAY_NOON;
    if (t <= st[0].t) return st[0].rgb;
    for (int i = 1; i < n; i++)
        if (t <= st[i].t) return lerp(st[i - 1].rgb, st[i].rgb, (double)(t - st[i - 1].t) / (double)(st[i].t - st[i - 1].t));
    return st[n - 1].rgb;
}

int wx_sun_count_n(const wx_sun_t *s) { return wx_count_n(s->count); }

const char *wx_sun_value(const wx_sun_t *s, int k, char *buf, size_t n)
{
    if (!s->have || !s->has_now) {
        snprintf(buf, n, "--");
        return NULL;
    }
    int v = wx_count_value(s->count, k);
    if (s->hours) wx_cast_count_text(true, v, buf, n);
    else snprintf(buf, n, "%d", v);
    return s->unit;
}

void wx_sun_frame(double t, wx_sun_frame_t *f)
{
    f->dy = (int)lround(WX_SUN_RISE_PX * (1 - wx_arrive(t)));
    double tf = t > 0 ? floor(t * WX_SUN_FPS + 1e-9) / WX_SUN_FPS : 0;
    double p = (tf - WX_SUN_RAYS_ON_S) / (WX_SUN_RAYS_IN_S - WX_SUN_RAYS_ON_S);
    f->rays_opa = !(p > 0) ? 0 : p >= 1 ? 255 : (int)lround(255 * p);
}

void wx_sun_layout(wx_sun_layout_t *l)
{
    l->rays = (wx_box_t){WX_SUN_RAYS_X0, WX_SUN_RAYS_Y0, WX_SUN_RAYS_X1, WX_SUN_CLIP_Y};
    l->sun = (wx_box_t){WX_SUN_CX - 75, WX_SUN_CY - 74, WX_SUN_CX + 75, WX_SUN_CLIP_Y};
    l->rays_at = 0;
    l->sun_at = (l->rays.x1 - l->rays.x0) * (l->rays.y1 - l->rays.y0);
    l->total = l->sun_at + (l->sun.x1 - l->sun.x0) * (l->sun.y1 - l->sun.y0) * 3;
}

bool wx_sun_hero(const wx_sun_t *s) { return s->have || s->polar == WX_SUN_POLAR_DAY; }
