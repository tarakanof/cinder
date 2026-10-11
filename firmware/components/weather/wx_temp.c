#include "wx_temp.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    float t;
    uint32_t rgb;
} stop_t;

static const stop_t SCALE[] = {
    {-20, 0xB070E0}, {-12, 0x7060E8}, {-6, 0x4C84F0}, {-2, 0x5CB0F0}, {0, 0x66C2EC}, {4, 0x52C6D2},
    {8, 0x4AC5B9},   {8.545455f, 0x4BC8AA}, {9.090909f, 0x4CCAA2}, {9.636364f, 0x4CCB9A}, {10.181818f, 0x5CCC92},
    {10.727273f, 0x6BCD8A}, {11.272727f, 0x7BCE82}, {12, 0x8ACF7A}, {12.363636f, 0x9AD072},
    {12.909091f, 0xAAD06A}, {13.454545f, 0xBAD162}, {14, 0xC9D25A}, {15, 0xE8D44A}, {25, 0xF08A3A},
    {35, 0xFF5050}, {40, 0xE0306A},
};
#define N_SCALE ((int)(sizeof SCALE / sizeof SCALE[0]))

uint32_t wx_temp_rgb(float c)
{
    if (!(c > SCALE[0].t)) return SCALE[0].rgb;
    if (c >= SCALE[N_SCALE - 1].t) return SCALE[N_SCALE - 1].rgb;
    int i = 1;
    while (c > SCALE[i].t) i++;
    const stop_t *a = &SCALE[i - 1], *b = &SCALE[i];
    float f = (c - a->t) / (b->t - a->t);
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        float ca = (float)((a->rgb >> s) & 0xFF), cb = (float)((b->rgb >> s) & 0xFF);
        int v = (int)lroundf(ca + (cb - ca) * f);
        out |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v) << s;
    }
    return out;
}

uint32_t wx_uv_rgb(int uv) { return uv <= 5 ? 0xF0E641 : uv <= 7 ? 0xF08A3A : uv <= 10 ? 0xFF5050 : 0xB060E0; }

const char *wx_uv_advice(int uv)
{
    if (uv < 3) return NULL;
    return uv <= 5 ? "Sunglasses on" : uv <= 7 ? "Use sunscreen" : uv <= 10 ? "Sunscreen & shade" : "Avoid the sun";
}

static const int BAND_EDGE[4] = {10, 40, 70, 90};

int wx_cloud_band(int cloud)
{
    int b = 0;
    while (b < 4 && cloud >= BAND_EDGE[b]) b++;
    return b;
}

int wx_cloud_band_keep(int cloud, int shown)
{
    int b = wx_cloud_band(cloud);
    if (shown < 0 || shown > 4 || b == shown) return b;
    bool above = shown == 0 || cloud >= BAND_EDGE[shown - 1] - WX_BAND_HYST;
    bool below = shown == 4 || cloud < BAND_EDGE[shown] + WX_BAND_HYST;
    return above && below ? shown : b;
}

static int cloud_of(const wx_obs_t *o, const wx_look_t *l)
{
    if (o->has_cloud) return o->cloud;
    switch (l->face) {
    case WX_CLEAR_DAY:
    case WX_CLEAR_NIGHT: return 0;
    case WX_PARTLY_CLOUDY: return 55;
    default: return 95;
    }
}

static const char *sky_word(const wx_look_t *l, int band, bool has_cloud, bool night)
{
    static const char *const BANDS[2][5] = {
        {"Sunny", "Mostly sunny", "Partly cloudy", "Cloudy", "Overcast"},
        {"Clear", "Mostly clear", "Partly cloudy", "Cloudy", "Overcast"},
    };
    switch (l->face) {
    case WX_FOG: return "Fog";
    case WX_RAIN: return l->intensity == WX_INT_LIGHT ? "Light rain" : l->intensity == WX_INT_HEAVY ? "Heavy rain" : "Rain";
    case WX_SNOW: return l->intensity == WX_INT_LIGHT ? "Light snow" : l->intensity == WX_INT_HEAVY ? "Heavy snow" : "Snow";
    case WX_STORM: return "Storm";
    default: break;
    }
    if (has_cloud) return BANDS[night][band];
    if (l->face == WX_PARTLY_CLOUDY) return "Partly cloudy";
    if (l->face == WX_OVERCAST) return "Overcast";
    return BANDS[night][0];
}

void wx_temp_from(const wx_obs_t *o, wx_temp_t *t) { wx_temp_from_shown(o, NULL, t); }

void wx_temp_from_shown(const wx_obs_t *o, const wx_temp_t *shown, wx_temp_t *t)
{
    memset(t, 0, sizeof *t);
    t->lo = WX_SCALE_LO;
    t->hi = WX_SCALE_HI;
    t->sub_rgb = 0xBDBDBD;
    wx_rain_from(o, &t->rain);
    wx_rain_keep(&t->rain, shown ? &shown->rain : NULL);
    if (!o || !o->valid || !o->has_temp || !isfinite(o->temp_c)) return;
    wx_look_t l = wx_look_from_obs(o);
    t->have = true;
    t->still = l.still;
    t->value = (int)lroundf(o->temp_c < -99 ? -99 : o->temp_c > 99 ? 99 : o->temp_c);
    if (o->has_today) {
        t->has_range = true;
        t->lo = o->min_c < t->value ? o->min_c : t->value;
        t->hi = o->max_c > t->value ? o->max_c : t->value;
        if (t->lo == t->hi) {
            t->lo--;
            t->hi++;
        }
    }
    float f = (float)(t->value - t->lo) / (float)(t->hi - t->lo);
    t->frac = f < 0 ? 0 : f > 1 ? 1 : f;
    t->night = l.night;
    int cloud = cloud_of(o, &l);
    t->cloud_band = wx_cloud_band_keep(cloud, shown && shown->have ? shown->cloud_band : -1);
    if (!t->night) {
        if (t->cloud_band <= 1) t->hero = WX_HERO_SUN;
    } else if (o->has_moon && o->moon_up == 1 && t->cloud_band < 4) {
        t->hero = t->cloud_band < 3 ? WX_HERO_MOON : WX_HERO_MOON_SMALL;
        t->moon_phase = o->moon_phase;
        int il = o->moon_illum < 0 ? 0 : o->moon_illum > 100 ? 100 : o->moon_illum;
        t->moon_illum = (il + WX_ILLUM_STEP / 2) / WX_ILLUM_STEP * WX_ILLUM_STEP;
    }
    t->stars = t->night && t->cloud_band <= 1;
    const char *adv = !t->night && o->has_uv ? wx_uv_advice(o->uv) : NULL;
    if (adv) {
        snprintf(t->sub, sizeof t->sub, "%s", adv);
        t->sub_rgb = wx_uv_rgb(o->uv);
    } else {
        snprintf(t->sub, sizeof t->sub, "%s", sky_word(&l, t->cloud_band, o->has_cloud, t->night));
    }
}

int wx_temp_bake_kind(const wx_temp_t *t, const wx_temp_t *s)
{
    if (!wx_rain_same_scene(&t->rain, &s->rain)) return WX_BAKE_FULL;
    if (t->rain.on) return t->rain.has_pop != s->rain.has_pop ? WX_BAKE_RING : 0;
    if (t->have != s->have || t->night != s->night || t->cloud_band != s->cloud_band || t->stars != s->stars ||
        (t->hero == WX_HERO_SUN) != (s->hero == WX_HERO_SUN))
        return WX_BAKE_FULL;
    int k = 0;
    if (t->has_range != s->has_range || t->lo != s->lo || t->hi != s->hi) k |= WX_BAKE_RING;
    if (t->hero != s->hero || (t->moon_phase < 50) != (s->moon_phase < 50) || t->moon_illum != s->moon_illum)
        k |= WX_BAKE_HERO;
    return k;
}

bool wx_temp_same_bake(const wx_temp_t *a, const wx_temp_t *b) { return wx_temp_bake_kind(a, b) == 0; }

int wx_count_n(int v)
{
    int a = v < 0 ? -v : v;
    return a <= WX_COUNT_MAX ? a : 23;
}

int wx_count_value(int v, int k)
{
    int n = wx_count_n(v);
    if (k <= 0 || n == 0) return 0;
    if (k >= n) return v;
    if (n == (v < 0 ? -v : v)) return v < 0 ? -k : k;
    return (int)lround((double)k * v / n);
}

int wx_count_step(int n, double t)
{
    if (!(t > 0)) return 0;
    if (t >= WX_ARRIVE_S) return n;
    double p = 1 - t / WX_ARRIVE_S;
    int k = (int)floor(n * (1 - p * p * p) + 1e-9);
    return k > n ? n : k;
}

double wx_arrive(double t)
{
    if (!(t > 0)) return 0;
    if (t >= WX_ARRIVE_S) return 1;
    double p = 1 - t / WX_ARRIVE_S;
    return 1 - p * p * p;
}

void wx_gauge_xy(float frac, float *x, float *y)
{
    float a = (225.0f + 270.0f * frac) * (float)M_PI / 180.0f;
    *x = WX_FACE_C + WX_RING_R * sinf(a);
    *y = WX_FACE_C - WX_RING_R * cosf(a);
}

float wx_merc_top(float c)
{
    float top = 144.0f - (c - 12.0f) * 22.0f / 19.0f;
    return top < 122.0f ? 122.0f : top > 178.0f ? 178.0f : top;
}

typedef struct {
    int side_back;
    float base;
    int n;
    float c[4][3];
} cloud_src_t;

static const cloud_src_t BAND1[] = {
    {0, 150, 3, {{302.2f, 136.2f, 18.4f}, {341.3f, 120.1f, 29.9f}, {385.0f, 133.9f, 25.3f}}},
};
static const cloud_src_t BAND2[] = {
    {0, 182, 3, {{78.5f, 163.1f, 29.7f}, {129.8f, 146.9f, 35.1f}, {175.7f, 165.8f, 21.6f}}},
    {0, 160, 3, {{298.3f, 143.8f, 21.6f}, {344.2f, 124.9f, 35.1f}, {395.5f, 141.1f, 29.7f}}},
};
static const cloud_src_t BAND3[] = {
    {1, 142, 3, {{52.1f, 121.0f, 33.0f}, {109.1f, 103.0f, 39.0f}, {160.1f, 124.0f, 24.0f}}},
    {1, 128, 3, {{307.9f, 110.0f, 24.0f}, {358.9f, 89.0f, 39.0f}, {415.9f, 107.0f, 33.0f}}},
    {0, 190, 3, {{70.1f, 169.0f, 33.0f}, {127.1f, 151.0f, 39.0f}, {178.1f, 172.0f, 24.0f}}},
    {0, 176, 3, {{293.9f, 158.0f, 24.0f}, {344.9f, 137.0f, 39.0f}, {401.9f, 155.0f, 33.0f}}},
};
static const cloud_src_t BAND4[] = {
    {1, 140, 4, {{20, 124, 28}, {66, 104, 32}, {118, 110, 30}, {160, 128, 20}}},
    {1, 128, 4, {{308, 114, 22}, {352, 98, 30}, {400, 92, 32}, {446, 112, 28}}},
    {0, 196, 4, {{24, 178, 30}, {74, 162, 34}, {128, 168, 30}, {178, 182, 24}}},
    {0, 184, 4, {{284, 170, 24}, {330, 156, 30}, {380, 150, 34}, {430, 166, 30}}},
};

void wx_clouds_for(int band, bool night, wx_clouds_t *out)
{
    static const struct {
        const cloud_src_t *src;
        int n;
        wx_tone_t tone;
        float fade_from;
    } B[5] = {
        {NULL, 0, WX_TONE_WHITE, 175},
        {BAND1, 1, WX_TONE_WHITE, 175},
        {BAND2, 2, WX_TONE_WHITE, 175},
        {BAND3, 4, WX_TONE_LIGHT, 165},
        {BAND4, 4, WX_TONE_DARK, 160},
    };
    memset(out, 0, sizeof *out);
    if (band < 0 || band > 4) return;
    out->n = B[band].n;
    out->tone = night ? WX_TONE_NIGHT : B[band].tone;
    out->fade_from = B[band].fade_from;
    for (int i = 0; i < out->n; i++) {
        const cloud_src_t *s = &B[band].src[i];
        wx_cloud_t *c = &out->cl[i];
        c->back = s->side_back;
        c->base = s->base;
        c->n = s->n;
        for (int k = 0; k < s->n; k++) c->c[k] = (wx_circle_t){s->c[k][0], s->c[k][1], s->c[k][2]};
    }
}

bool wx_shift_tick(wx_shift_t *s, double dt)
{
    if (isfinite(dt) && dt > 0) s->t += dt;
    if (s->t < WX_SHIFT_S) return false;
    s->t = 0;
    s->i = (s->i + 1) & 3;
    return true;
}

void wx_shift_xy(const wx_shift_t *s, int *dx, int *dy)
{
    *dx = (s->i == 1 || s->i == 2) ? WX_SHIFT_PX : 0;
    *dy = s->i >= 2 ? WX_SHIFT_PX : 0;
}
