#include "wx_cast.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static long long fmod_ll(long long a, long long m) { return ((a % m) + m) % m; }

static long long floor_div(long long a, long long m) { return (a - fmod_ll(a, m)) / m; }

static float dial_deg(long long local) { return (float)fmod_ll(local, WX_CAST_SPAN_S) / 120.0f; }

static int clamp_in(int v) { return v < 1 ? 1 : v > 3 ? 3 : v; }

typedef struct {
    long long now;
    const wx_spell_t *cur, *next;
    bool wet;
} scan_t;

static bool scan(const wx_obs_t *o, scan_t *s)
{
    memset(s, 0, sizeof *s);
    if (!o || !o->valid || wx_look_from_obs(o).still || !wx_knob_now(o, &s->now)) return false;
    for (int i = 0; i < o->rain_n && i < WX_SPELLS; i++) {
        const wx_spell_t *p = &o->rain_spell[i];
        if (p->start <= s->now && s->now < p->end) s->cur = p;
        else if (p->start > s->now && (!s->next || p->start < s->next->start)) s->next = p;
    }
    s->wet = (o->has_now_rain && o->now_rain > 0) || s->cur;
    if (s->wet && o->rain_until > 0 && s->now >= o->rain_until) s->wet = false;
    return true;
}

static const char *word(bool snow, int v, bool cap)
{
    static const char *const W[2][2][3] = {
        {{"light rain", "rain", "heavy rain"}, {"light snow", "snow", "heavy snow"}},
        {{"Light rain", "Rain", "Heavy rain"}, {"Light snow", "Snow", "Heavy snow"}},
    };
    return W[cap][snow][clamp_in(v) - 1];
}

void wx_cast_from(const wx_obs_t *o, const wx_cast_t *shown, wx_cast_t *c)
{
    memset(c, 0, sizeof *c);
    c->on = true;
    scan_t s;
    if (!scan(o, &s)) return;
    if (!o->has_rain || !o->has_utc_offset) {
        snprintf(c->sub, sizeof c->sub, "No forecast");
        return;
    }
    c->have = true;
    long long off = o->utc_offset, q = s.now - fmod_ll(s.now, WX_CAST_STEP_S);
    c->now_deg = dial_deg(s.now + off);
    for (int i = 0; i < o->rain_n && i < WX_SPELLS; i++) {
        const wx_spell_t *p = &o->rain_spell[i];
        long long a = p->start > q ? p->start : q, b = p->end < q + WX_CAST_SPAN_S ? p->end : q + WX_CAST_SPAN_S;
        if (b - a < 60) continue;
        c->arc[c->narc++] = (wx_arc_t){(int)fmod_ll(floor_div(a + off, 60), 720), (int)((b - a) / 60), clamp_in(p->intensity)};
    }
    long long at = 0;
    if (s.wet) {
        c->wet = true;
        c->variant = clamp_in(s.cur ? s.cur->intensity : o->has_now_rain && o->now_rain > 0 ? o->now_rain : 2);
        c->snow = s.cur ? s.cur->kind == 1 : wx_look_from_obs(o).face == WX_SNOW;
        if (shown && shown->on && shown->have && shown->wet && shown->snow == c->snow && c->variant < shown->variant)
            c->variant = shown->variant;
        if (o->rain_until > s.now) at = o->rain_until;
    } else if (s.next) {
        c->variant = clamp_in(s.next->intensity);
        c->snow = s.next->kind == 1;
        at = s.next->start;
    }
    if (!at) {
        snprintf(c->sub, sizeof c->sub, "%s", s.wet ? word(c->snow, c->variant, true) : "Dry for 12 h");
        return;
    }
    c->has_count = true;
    wx_cast_round(at - s.now, &c->hours, &c->count);
    int m = (int)(fmod_ll(at + off, 86400) / 60);
    if (m % 60) snprintf(c->label, sizeof c->label, "%02d:%02d", m / 60, m % 60);
    else snprintf(c->label, sizeof c->label, "%02d", m / 60);
    float d = c->now_deg + (float)(at - s.now) / 120.0f;
    c->label_deg = fmodf(d, 360.0f);
    if (s.wet) {
        c->to_deg = d - 1.5f > c->now_deg ? d - 1.5f : c->now_deg;
    } else {
        float cs = 30.0f * floorf(d / 30.0f) + 1.5f;
        c->to_deg = (d > cs ? d : cs) + 1.5f;
    }
    snprintf(c->sub, sizeof c->sub, "#%06X %s# %s %02d:%02d", WX_CAST_RGB, word(c->snow, c->variant, false),
             s.wet ? "until" : "at", m / 60, m % 60);
}

bool wx_cast_soon(const wx_obs_t *o, bool shown)
{
    scan_t s;
    if (!scan(o, &s) || !o->has_rain || !o->has_utc_offset || s.wet || !s.next) return false;
    return s.next->start - s.now <= (shown ? WX_CAST_KEEP_S : WX_CAST_SOON_S);
}

bool wx_cast_same_scene(const wx_cast_t *a, const wx_cast_t *b)
{
    return a->on == b->on && (!a->on || (a->variant == b->variant && a->snow == b->snow));
}

bool wx_cast_same_ring(const wx_cast_t *a, const wx_cast_t *b)
{
    if (a->narc != b->narc) return false;
    for (int i = 0; i < a->narc; i++)
        if (a->arc[i].a != b->arc[i].a || a->arc[i].n != b->arc[i].n || a->arc[i].intensity != b->arc[i].intensity)
            return false;
    return true;
}

void wx_cast_round(long long s, bool *hours, int *count)
{
    if (s < 0) s = 0;
    long long m5 = (s + 150) / 300;
    if (m5 < 12) {
        *hours = false;
        *count = m5 < 1 ? 1 : (int)m5;
        return;
    }
    long long h = (s + 900) / 1800;
    *hours = true;
    *count = h < 2 ? 2 : (int)h;
}

void wx_cast_count_text(bool hours, int count, char *buf, size_t n)
{
    if (!hours) {
        snprintf(buf, n, "%d", count * 5);
        return;
    }
    int whole = count / 2;
    bool half = count % 2;
    if (whole || !half) snprintf(buf, n, "%d%s", whole, half ? "\xC2\xBD" : "");
    else snprintf(buf, n, "\xC2\xBD");
}

int wx_cast_count_n(const wx_cast_t *c) { return c->has_count ? c->count : 0; }

void wx_cast_step_text(const wx_cast_t *c, int k, char *buf, size_t n)
{
    int v = k < 0 ? 0 : k > c->count ? c->count : k;
    wx_cast_count_text(c->hours, v, buf, n);
}

void wx_cast_ring_xy(float deg, float *x, float *y)
{
    float a = deg * (float)M_PI / 180.0f;
    *x = 233.0f + 222.0f * sinf(a);
    *y = 233.0f - 222.0f * cosf(a);
}

void wx_cast_label_at(float deg, float *x, float *y, int *align)
{
    float lc = fmodf(fmodf(deg, 360.0f) + 360.0f, 360.0f), r = 188;
    *align = 0;
    if (lc > 45 && lc < 135) {
        *align = -1;
        r = 198;
    } else if (lc > 225 && lc < 315) {
        *align = 1;
        r = 198;
    }
    float a = lc * (float)M_PI / 180.0f;
    *x = 233.0f + r * sinf(a);
    *y = 233.0f - r * cosf(a);
}

double wx_cast_ease(double p)
{
    if (!(p > 0)) return 0;
    if (p >= 1) return 1;
    double lo = 0, hi = 1;
    for (int i = 0; i < 40; i++) {
        double u = (lo + hi) / 2, v = 1 - u;
        double x = 3 * v * v * u * 0.45 + 3 * v * u * u * 0.25 + u * u * u;
        if (x < p) lo = u;
        else hi = u;
    }
    double u = (lo + hi) / 2, v = 1 - u;
    return 3 * v * u * u + u * u * u;
}

float wx_cast_drop_deg(const wx_cast_t *c, double t)
{
    return c->now_deg + (c->to_deg - c->now_deg) * (float)wx_cast_ease(t / WX_CAST_SLIDE_S);
}

static int ramp(double t, double t0, double t1, bool up)
{
    double f = t <= t0 ? 0 : t >= t1 ? 1 : (t - t0) / (t1 - t0);
    return (int)lround(255 * (up ? f : 1 - f));
}

int wx_cast_drop_opa(double t)
{
    if (!(t >= 0) || t >= WX_CAST_DROP_OUT_S) return 0;
    if (t < WX_CAST_DROP_IN_S) return ramp(t, 0, WX_CAST_DROP_IN_S, true);
    return t <= WX_CAST_SLIDE_S ? 255 : ramp(t, WX_CAST_SLIDE_S, WX_CAST_DROP_OUT_S, false);
}

int wx_cast_gate_opa(double t) { return ramp(t, WX_CAST_GATE_S, WX_CAST_GATE_END_S, true); }

int wx_cast_label_opa(double t) { return ramp(t, WX_CAST_SLIDE_S, WX_CAST_LABEL_END_S, true); }

static const wx_streak_t LIGHT_A[] = {{176, 6, 5, 0.8f, 0}, {214, 22, 5, 1, 0}, {248, 10, 5, 0.7f, 0}, {282, 30, 5, 0.9f, 0},
                                      {196, 34, 5, 0.6f, 0}};
static const wx_streak_t LIGHT_B[] = {{186, 14, 5, 0.9f, 0}, {232, 2, 5, 0.75f, 0}, {266, 20, 5, 1, 0}, {204, 28, 5, 0.65f, 0},
                                      {292, 8, 5, 0.8f, 0}};
static const wx_streak_t MOD_LA[] = {{72, 4, 10, 0.85f, 3}, {118, 17, 10, 1, 3}, {150, 29, 10, 0.7f, 3},
                                     {94, 35, 10, 0.9f, 4},  {132, 9, 10, 0.75f, 3}, {60, 22, 10, 0.8f, 3}};
static const wx_streak_t MOD_LB[] = {{84, 8, 10, 0.8f, 3},  {136, 21, 10, 0.95f, 3}, {66, 27, 10, 0.65f, 3},
                                     {160, 36, 10, 0.85f, 4}, {104, 14, 10, 0.9f, 3}, {146, 2, 10, 0.7f, 3}};
static const wx_streak_t MOD_RA[] = {{312, 7, 10, 0.9f, 3}, {356, 20, 10, 0.75f, 3}, {392, 32, 10, 1, 4},
                                     {334, 36, 10, 0.6f, 3}, {376, 2, 10, 0.85f, 3}, {306, 24, 10, 0.75f, 3}};
static const wx_streak_t MOD_RB[] = {{326, 3, 10, 0.85f, 3}, {372, 15, 10, 1, 3}, {398, 27, 10, 0.7f, 3},
                                     {344, 31, 10, 0.9f, 4}, {388, 10, 10, 0.8f, 3}, {316, 37, 10, 0.7f, 3}};
static const wx_streak_t HEAVY_LA[] = {{70, 4, 16, 0.9f, 6}, {102, 22, 16, 1, 6}, {138, 10, 16, 0.75f, 6},
                                       {170, 34, 16, 0.95f, 6}, {196, 48, 14, 0.7f, 5}};
static const wx_streak_t HEAVY_LB[] = {{84, 14, 16, 0.85f, 6}, {120, 40, 16, 1, 6}, {154, 2, 16, 0.8f, 6},
                                       {186, 24, 16, 0.9f, 6}, {66, 46, 14, 0.7f, 5}};
static const wx_streak_t HEAVY_RA[] = {{262, 8, 16, 0.9f, 6}, {296, 30, 16, 1, 6}, {330, 4, 16, 0.75f, 6},
                                       {362, 26, 16, 0.95f, 6}, {394, 44, 14, 0.7f, 5}};
static const wx_streak_t HEAVY_RB[] = {{278, 20, 16, 0.85f, 6}, {312, 46, 16, 1, 6}, {346, 14, 16, 0.8f, 6},
                                       {380, 36, 16, 0.9f, 6}, {256, 50, 14, 0.7f, 5}};

#define N(a) ((int)(sizeof a / sizeof a[0]))

static const wx_cast_sheet_t SHEETS[3][WX_CAST_SIDES] = {
    {{{160, 172, 302, 208}, 40, 30, 0, 3, N(LIGHT_A), LIGHT_A, LIGHT_B}},
    {{{54, 182, 172, 208}, 40, 12, 5, 4, N(MOD_LA), MOD_LA, MOD_LB}, {{298, 174, 406, 208}, 40, 14, 11, 4, N(MOD_RA), MOD_RA, MOD_RB}},
    {{{58, 172, 200, 208}, 60, 12, 3, 4.5f, N(HEAVY_LA), HEAVY_LA, HEAVY_LB},
     {{248, 164, 400, 208}, 60, 14, 9, 4.5f, N(HEAVY_RA), HEAVY_RA, HEAVY_RB}},
};

int wx_cast_sides(int variant) { return variant == 1 ? 1 : variant == 2 || variant == 3 ? 2 : 0; }

const wx_cast_sheet_t *wx_cast_sheet(int variant, int side)
{
    return side >= 0 && side < wx_cast_sides(variant) ? &SHEETS[variant - 1][side] : NULL;
}

int wx_cast_sheet_off(const wx_cast_sheet_t *s, bool snow, int k)
{
    int frames = 2 * s->tile_frames * (snow ? 2 : 1), period = 2 * s->h;
    if (k < 0) k = 0;
    int a = (k + s->delay_frames) % frames;
    return (int)lround((double)period * a / frames) % period;
}

static int steps_for(double s) { return (int)lround(s * WX_CAST_FPS); }

static double tri_steps(double t, double period, double delay, int n)
{
    double p = fmod(t - delay, period);
    if (p < 0) p += period;
    double half = period / 2, u = p < half ? p / half : (p - half) / half;
    double q = floor(u * n + 1e-9) / n;
    return p < half ? q : 1 - q;
}

int wx_cast_breathe(int variant, int side, int k)
{
    static const double P[3][WX_CAST_SIDES][2] = {{{8, 0}, {8, 0}}, {{8, 0}, {9, -4}}, {{8, 0}, {8.5, -2}}};
    if (variant < 1 || variant > 3 || side < 0 || side >= WX_CAST_SIDES) return 0;
    double period = P[variant - 1][side][0];
    double f = tri_steps((double)(k < 0 ? 0 : k) / WX_CAST_FPS, period, P[variant - 1][side][1], steps_for(period / 2));
    return (int)lround(-WX_CAST_BREATHE_PX + 2.0 * WX_CAST_BREATHE_PX * f);
}

int wx_cast_gloom(int variant, int k)
{
    if (variant != 3) return 255;
    double f = tri_steps((double)(k < 0 ? 0 : k) / WX_CAST_FPS, 3.2, 0, WX_CAST_GLOOM_STEPS);
    return (int)lround(255 * (1 - 0.3 * f));
}
