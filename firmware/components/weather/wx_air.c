#include "wx_air.h"

#include <math.h>
#include <string.h>

#define PETAL_FRAMES 105
#define PETAL_PHASE 30

typedef struct {
    int at;
    uint32_t rgb;
} stop_t;

static const stop_t STOPS[] = {{10, 0x4FD6CC}, {30, 0x50CCAA}, {50, 0xF0E641}, {70, 0xFF5050}, {90, 0xD03060}};
#define N_STOPS ((int)(sizeof STOPS / sizeof STOPS[0]))

uint32_t wx_air_rgb(int aqi)
{
    if (aqi <= STOPS[0].at) return STOPS[0].rgb;
    if (aqi >= STOPS[N_STOPS - 1].at) return STOPS[N_STOPS - 1].rgb;
    int i = 1;
    while (aqi > STOPS[i].at) i++;
    const stop_t *a = &STOPS[i - 1], *b = &STOPS[i];
    double f = (double)(aqi - a->at) / (double)(b->at - a->at);
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        double ca = (double)((a->rgb >> s) & 0xFF), cb = (double)((b->rgb >> s) & 0xFF);
        out |= (uint32_t)lround(ca + (cb - ca) * f) << s;
    }
    return out;
}

static const struct {
    int hi;
    uint32_t rgb;
    const char *word;
} BANDS[WX_AIR_BANDS] = {
    {20, 0x4FD6CC, "Good"},      {40, 0x50CCAA, "Fair"},      {60, 0xF0E641, "Moderate"},
    {80, 0xFF5050, "Poor"},      {100, 0xD03060, "Very poor"}, {WX_AIR_MAX, 0xB060E0, "Extremely poor"},
};

int wx_air_band(int aqi)
{
    int b = 0;
    while (b < WX_AIR_BANDS - 1 && aqi > BANDS[b].hi) b++;
    return b;
}

static int clamp_band(int b) { return b < 0 ? 0 : b >= WX_AIR_BANDS ? WX_AIR_BANDS - 1 : b; }

const char *wx_air_word(int band) { return BANDS[clamp_band(band)].word; }

uint32_t wx_air_band_rgb(int band) { return BANDS[clamp_band(band)].rgb; }

bool wx_air_wilted(int aqi, int shown)
{
    if (shown == 1) return aqi > WX_AIR_POOR - WX_AIR_HYST;
    if (shown == 0) return aqi > WX_AIR_POOR + WX_AIR_HYST;
    return aqi > WX_AIR_POOR;
}

static bool fresh(const wx_obs_t *o) { return o && o->has_aqi && !wx_look_from_obs(o).still; }

static int clamp_aqi(int v) { return v < 0 ? 0 : v > WX_AIR_MAX ? WX_AIR_MAX : v; }

void wx_air_from(const wx_obs_t *o, const wx_air_t *shown, wx_air_t *a)
{
    memset(a, 0, sizeof *a);
    a->on = true;
    if (!fresh(o)) return;
    a->have = true;
    a->aqi = clamp_aqi(o->aqi);
    a->band = wx_air_band(a->aqi);
    a->wilted = wx_air_wilted(a->aqi, shown && shown->on && shown->have ? shown->wilted : -1);
    a->frac = (float)(a->aqi < WX_AIR_GAUGE ? a->aqi : WX_AIR_GAUGE) / WX_AIR_GAUGE;
}

bool wx_air_poor(const wx_obs_t *o, bool shown)
{
    return fresh(o) && o->aqi > (shown ? WX_AIR_POOR - WX_AIR_HYST : WX_AIR_POOR);
}

bool wx_air_same_scene(const wx_air_t *a, const wx_air_t *b)
{
    return a->on == b->on && a->have == b->have && a->wilted == b->wilted;
}

static double ease(double p)
{
    if (!(p > 0)) return 0;
    if (p >= 1) return 1;
    double lo = 0, hi = 1;
    for (int i = 0; i < 40; i++) {
        double u = (lo + hi) / 2, v = 1 - u;
        if (3 * v * v * u * 0.42 + 3 * v * u * u * 0.58 + u * u * u < p) lo = u;
        else hi = u;
    }
    double u = (lo + hi) / 2;
    return 3 * (1 - u) * u * u + u * u * u;
}

void wx_air_petal(int k, wx_petal_t *p)
{
    memset(p, 0, sizeof *p);
    if (k < 0) return;
    double t = (double)((k + PETAL_PHASE) % PETAL_FRAMES) / PETAL_FRAMES;
    double u = t < 0.62 ? ease(t / 0.62) : 1;
    double op = t < 0.08 ? ease(t / 0.08) : t < 0.9 ? 1 : 1 - ease((t - 0.9) / 0.1);
    p->x = (int)lround(WX_PETAL_X + 14 * (1 - u)) - WX_PETAL_BOX / 2;
    p->y = (int)lround(WX_PETAL_Y - 58 * (1 - u)) - WX_PETAL_BOX / 2;
    p->rot = (int)lround(5 * u);
    p->opa = (int)lround(255 * op);
}

wx_box_t wx_air_petal_box(void)
{
    return (wx_box_t){WX_PETAL_X - WX_PETAL_BOX / 2, WX_PETAL_Y - 58 - WX_PETAL_BOX / 2, WX_PETAL_X + 14 + WX_PETAL_BOX / 2,
                      WX_PETAL_Y + WX_PETAL_BOX / 2};
}
