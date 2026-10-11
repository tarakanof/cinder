#include "wx_wind.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int hi;
    uint32_t rgb;
    const char *name;
} band_t;

static const band_t BANDS[] = {
    {0, 0x9AA4B0, "Calm"},           {5, 0x9AA4B0, "Light air"},      {11, 0x4FD6CC, "Light breeze"},
    {19, 0x8ACF7A, "Gentle breeze"}, {28, 0xC9D25A, "Moderate breeze"}, {38, 0xE8D44A, "Fresh breeze"},
    {49, 0xF08A3A, "Strong breeze"}, {61, 0xFF5050, "Near gale"},    {74, 0xD03060, "Gale"},
    {88, 0xD03060, "Strong gale"},   {102, 0xD03060, "Storm"},       {117, 0xD03060, "Violent storm"},
    {0x7FFFFFFF, 0xD03060, "Hurricane"},
};

static const band_t *band(int kmh)
{
    int i = 0;
    while (kmh > BANDS[i].hi) i++;
    return &BANDS[i];
}

uint32_t wx_wind_rgb(int kmh) { return band(kmh)->rgb; }

const char *wx_wind_band(int kmh) { return band(kmh)->name; }

uint32_t wx_wind_dark(uint32_t rgb)
{
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) out |= (uint32_t)lround(((rgb >> s) & 0xFF) * 0.55) << s;
    return out;
}

const char *wx_wind_compass(int dir)
{
    static const char *const P[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int d = ((dir % 360) + 360) % 360;
    return P[((2 * d + 45) / 90) % 8];
}

static const int EDGE[WX_WIND_LEVELS - 1] = {12, 29};

int wx_wind_level(int kmh)
{
    int l = 0;
    while (l < WX_WIND_LEVELS - 1 && kmh >= EDGE[l]) l++;
    return l;
}

int wx_wind_level_keep(int kmh, int shown)
{
    int l = wx_wind_level(kmh);
    if (shown < 0 || shown >= WX_WIND_LEVELS || l == shown) return l;
    bool above = shown == 0 || kmh >= EDGE[shown - 1] - WX_WIND_HYST;
    bool below = shown == WX_WIND_LEVELS - 1 || kmh < EDGE[shown] + WX_WIND_HYST;
    return above && below ? shown : l;
}

static bool fresh(const wx_obs_t *o) { return o && o->valid && o->has_wind && !wx_look_from_obs(o).still; }

static int clamp_kmh(int v) { return v < 0 ? 0 : v > WX_WIND_MAX_KMH ? WX_WIND_MAX_KMH : v; }

void wx_wind_from(const wx_obs_t *o, const wx_wind_t *shown, wx_wind_t *w)
{
    memset(w, 0, sizeof *w);
    w->on = true;
    if (!fresh(o)) return;
    w->have = true;
    w->kmh = clamp_kmh(o->wind_kmh);
    w->has_dir = o->has_wind_dir;
    w->dir = o->has_wind_dir ? ((o->wind_dir % 360) + 360) % 360 : 0;
    w->level = wx_wind_level_keep(w->kmh, shown && shown->on && shown->have ? shown->level : -1);
    w->frac = (float)(w->kmh < WX_WIND_GAUGE_KMH ? w->kmh : WX_WIND_GAUGE_KMH) / WX_WIND_GAUGE_KMH;
    int n = snprintf(w->sub, sizeof w->sub, "#%06X %s#", (unsigned)wx_wind_rgb(w->kmh), wx_wind_band(w->kmh));
    if (w->has_dir && w->kmh > 0 && n > 0 && (size_t)n < sizeof w->sub)
        snprintf(w->sub + n, sizeof w->sub - (size_t)n, " \xE2\x80\xA2 %s", wx_wind_compass(w->dir));
}

bool wx_wind_strong(const wx_obs_t *o, bool shown)
{
    return fresh(o) && clamp_kmh(o->wind_kmh) >= (shown ? WX_WIND_KEEP_KMH : WX_WIND_AUTO_KMH);
}

bool wx_wind_same_scene(const wx_wind_t *a, const wx_wind_t *b)
{
    return a->on == b->on && (!a->on || (a->have == b->have && a->level == b->level));
}

static const wx_wind_geo_t GEO[WX_WIND_LEVELS] = {
    {{240.3f, 78},
     64.3f,
     10,
     5,
     0.012f,
     {{{0.76f, -22.79f}, {27.62f, -16.04f}, {21.75f, 17.69f}, {-0.76f, 22.79f}},
      {{27.62f, -16.04f}, {50.99f, 1.03f}, {45.41f, 15.74f}, {21.75f, 17.69f}},
      {{50.99f, 1.03f}, {73.5f, 12.21f}, {68.13f, 24.42f}, {45.41f, 15.74f}},
      {{73.5f, 12.21f}, {95.63f, 23.24f}, {91.23f, 33.26f}, {68.13f, 24.42f}},
      {{95.63f, 23.24f}, {117.76f, 34.27f}, {114.33f, 42.1f}, {91.23f, 33.26f}}},
     1,
     0,
     {{40, 14, 22, 3.0f, 0, 22}},
     {{0, 0, 0}}},
    {{201.7f, 101},
     26.8f,
     5,
     4,
     0.02f,
     {{{0, -22.8f}, {24.7f, -20.14f}, {24.7f, 20.14f}, {0, 22.8f}},
      {{24.7f, -20.14f}, {49.4f, -17.48f}, {49.4f, 17.48f}, {24.7f, 20.14f}},
      {{49.4f, -17.48f}, {75.42f, -14.76f}, {72.78f, 14.76f}, {49.4f, 17.48f}},
      {{75.42f, -14.76f}, {100.57f, -2.6f}, {96.26f, 11.34f}, {72.78f, 14.76f}},
      {{100.57f, -2.6f}, {122.69f, 10.58f}, {119.19f, 18.38f}, {96.26f, 11.34f}}},
     3,
     0,
     {{-34, 30, 34, 1.6f, 0, 12}, {64, 46, 30, 1.6f, -0.53f, 12}, {86, 22, 26, 1.6f, -1.07f, 12}},
     {{0, 0, 0}}},
    {{191.2f, 101},
     0,
     2,
     3,
     0.03f,
     {{{0, -22.8f}, {24.7f, -20.14f}, {24.7f, 20.14f}, {0, 22.8f}},
      {{24.7f, -20.14f}, {49.4f, -17.48f}, {49.4f, 17.48f}, {24.7f, 20.14f}},
      {{49.4f, -17.48f}, {74.1f, -14.82f}, {74.1f, 14.82f}, {49.4f, 17.48f}},
      {{74.1f, -14.82f}, {98.8f, -12.16f}, {98.8f, 12.16f}, {74.1f, 14.82f}},
      {{98.8f, -12.16f}, {123.5f, -9.5f}, {123.5f, 9.5f}, {98.8f, 12.16f}}},
     4,
     2,
     {{-26, 18, 36, 0.8f, 0, 6}, {34, 40, 32, 0.8f, -0.2f, 6}, {54, 14, 30, 0.8f, -0.4f, 6}, {72, 52, 28, 0.8f, -0.6f, 6}},
     {{-10, 1.2f, 0}, {64, 1.6f, -0.7f}}},
};

static int clamp_level(int level) { return level < 0 ? 0 : level >= WX_WIND_LEVELS ? WX_WIND_LEVELS - 1 : level; }

const wx_wind_geo_t *wx_wind_geo(int level) { return &GEO[clamp_level(level)]; }

void wx_wind_quad(int level, int pose, int q, wx_pt_t out[4])
{
    const wx_wind_geo_t *g = wx_wind_geo(level);
    float a = 0, sx = 1, sy = 1;
    if (pose == 0) {
        a = -g->ang;
        sx = 1 - g->stretch;
        sy = 1.02f;
    } else if (pose == 2) {
        a = g->ang;
        sx = 1 + g->stretch;
        sy = 0.96f;
    }
    float r = (a + g->droop) * ((float)M_PI / 180.0f), c = cosf(r), s = sinf(r);
    for (int i = 0; i < 4; i++) {
        float x = g->quad[q][i].x * sx, y = g->quad[q][i].y * sy;
        out[i] = (wx_pt_t){g->mount.x + x * c - y * s, g->mount.y + x * s + y * c};
    }
}

int wx_wind_streak_x(int level, int r)
{
    const wx_wind_geo_t *g = wx_wind_geo(level);
    return (int)floorf(g->mount.x + g->row[r].x0) - WX_STREAK_PAD;
}

int wx_wind_streak_y(int level, int r)
{
    const wx_wind_geo_t *g = wx_wind_geo(level);
    return (int)floorf(g->mount.y + g->row[r].dy) - WX_STREAK_H / 2;
}

static double stepped(double f, int n) { return floor(f * n + 1e-6) / n; }

static double anim_t(int k, float period, float phase)
{
    double frames = (double)lroundf(period * WX_WIND_FPS), t = fmod((double)k - (double)phase * WX_WIND_FPS, frames);
    return (t < 0 ? t + frames : t) / WX_WIND_FPS;
}

static void leaf_at(const wx_wind_geo_t *g, int i, int k, wx_wind_frame_t *f)
{
    const wx_wind_leaf_t *l = &g->leaf[i];
    int n = (int)lround(l->period / 2 * WX_WIND_FPS);
    double p = anim_t(k, l->period, l->phase) / l->period, tx, ty, rot, op;
    if (p < 0.5) {
        double u = stepped(p / 0.5, n);
        tx = 70 * u;
        ty = -10 * u;
        rot = 160 * u;
    } else {
        double u = stepped((p - 0.5) / 0.5, n);
        tx = 70 + 70 * u;
        ty = -10 + 16 * u;
        rot = 160 + 170 * u;
    }
    if (p < 0.12) op = stepped(p / 0.12, n);
    else if (p < 0.88) op = 1;
    else op = 1 - stepped((p - 0.88) / 0.12, n);
    double cx = g->mount.x - 6 + 8 + tx, cy = g->mount.y + l->dy + ty;
    f->leaf_x[i] = (int)lround(cx) - WX_LEAF_BOX / 2;
    f->leaf_y[i] = (int)lround(cy) - WX_LEAF_BOX / 2;
    f->leaf_rot[i] = (int)lround(rot / (180.0 / WX_LEAF_ROTS)) % WX_LEAF_ROTS;
    f->leaf_opa[i] = (int)lround(255 * op);
}

void wx_wind_frame(int level, int k, wx_wind_frame_t *f)
{
    static const int SEQ[4] = {0, 1, 2, 1};
    memset(f, 0, sizeof *f);
    f->pose = 1;
    if (k < 0) return;
    const wx_wind_geo_t *g = wx_wind_geo(level);
    f->pose = SEQ[(k / g->pose_frames) % 4];
    for (int r = 0; r < g->nrows; r++) {
        const wx_wind_row_t *w = &g->row[r];
        double half = w->period / 2.0, t = anim_t(k, w->period, w->phase);
        bool first = t < half;
        double u = stepped((first ? t : t - half) / half, w->steps);
        f->streak_dx[r] = (int)lround(first ? -24 + 40 * u : 16 + 40 * u);
        f->streak_opa[r] = (int)lround(255 * 0.75 * (first ? u : 1 - u));
    }
    for (int i = 0; i < g->nleaves; i++) leaf_at(g, i, k, f);
}
