#include "weather_scene.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TAU ((float)(2 * M_PI))

static const wx_sprite_size_t SIZES[WX_SPR_COUNT] = {
    [WX_SPR_RAIN] = {8, 18, 1},
    [WX_SPR_FLAKE_S] = {6, 6, 1},
    [WX_SPR_FLAKE_L] = {14, 14, 1},
    [WX_SPR_CLOUD_L] = {110, 56, 2},
    [WX_SPR_CLOUD_S] = {68, 36, 2},
    [WX_SPR_BOLT] = {20, 40, 1},
    [WX_SPR_MOON] = {40, 40, 1},
    [WX_SPR_STAR] = {10, 10, 1},
    [WX_SPR_SUN] = {36, 36, 1},
    [WX_SPR_RAYS] = {60, 60, WX_RAY_STEPS},
    [WX_SPR_FOG] = {128, 10, 1},
};

wx_sprite_size_t wx_sprite_size(int id)
{
    wx_sprite_size_t none = {0, 0, 0};
    return (unsigned)id < WX_SPR_COUNT ? SIZES[id] : none;
}

#define RAIN_SLANT (-4.0f / 14.0f)

static const float CLOUD_C[3][3] = {{30, 34, 16}, {58, 25, 21}, {86, 35, 15}};
static const float CLOUD_SLAB[4] = {30, 30, 86, 50};
#define CLOUD_PTS 72

static void cloud_xf(int id, float *scale, float *oy)
{
    *scale = id == WX_SPR_CLOUD_S ? 0.6f : 1.0f;
    *oy = id == WX_SPR_CLOUD_S ? 2.0f : 0.0f;
}

static bool cloud_inside(float x, float y)
{
    for (int i = 0; i < 3; i++) {
        float dx = x - CLOUD_C[i][0], dy = y - CLOUD_C[i][1];
        if (dx * dx + dy * dy <= CLOUD_C[i][2] * CLOUD_C[i][2]) return true;
    }
    return x >= CLOUD_SLAB[0] && x <= CLOUD_SLAB[2] && y >= CLOUD_SLAB[1] && y <= CLOUD_SLAB[3];
}

static void stroke_line(wx_stroke_t *s, float x0, float y0, float x1, float y1, float hw)
{
    s->n = 2;
    s->x[0] = x0; s->y[0] = y0;
    s->x[1] = x1; s->y[1] = y1;
    s->hw = hw;
}

static void arc_append(wx_stroke_t *s, float cx, float cy, float r, float a0, float sweep, int n)
{
    for (int k = 0; k < n && s->n < WX_STROKE_PTS; k++) {
        float a = a0 + sweep * (float)k / (float)(n - 1);
        s->x[s->n] = cx + r * cosf(a);
        s->y[s->n] = cy + r * sinf(a);
        s->n++;
    }
}

static float ccw_sweep(float from, float to)
{
    float d = fmodf(to - from, TAU);
    return d < 0 ? d + TAU : d;
}

static bool angle_in_ccw(float a, float from, float sweep)
{
    return ccw_sweep(from, a) <= sweep;
}

static void moon_stroke(wx_stroke_t *s)
{
    const float c1x = 20, c1y = 20, R = 15, c2x = 28, c2y = 14, r = 13;
    float dx = c2x - c1x, dy = c2y - c1y, d = sqrtf(dx * dx + dy * dy);
    float a = (R * R - r * r + d * d) / (2 * d), h = sqrtf(R * R - a * a);
    float mx = c1x + a * dx / d, my = c1y + a * dy / d;
    float px = mx + h * dy / d, py = my - h * dx / d;
    float qx = mx - h * dy / d, qy = my + h * dx / d;

    float p1 = atan2f(py - c1y, px - c1x), q1 = atan2f(qy - c1y, qx - c1x);
    float sw1 = ccw_sweep(p1, q1);
    if (angle_in_ccw(atan2f(dy, dx), p1, sw1)) sw1 -= TAU;
    float q2 = atan2f(qy - c2y, qx - c2x), p2 = atan2f(py - c2y, px - c2x);
    float sw2 = ccw_sweep(q2, p2);
    if (!angle_in_ccw(atan2f(-dy, -dx), q2, sw2)) sw2 -= TAU;

    s->n = 0;
    s->hw = 1.4f;
    arc_append(s, c1x, c1y, R, p1, sw1, 40);
    arc_append(s, c2x, c2y, r, q2, sw2, 32);
    s->x[s->n] = s->x[0];
    s->y[s->n] = s->y[0];
    s->n++;
}

int wx_sprite_strokes(int id, int frame, wx_stroke_t out[WX_STROKES_MAX])
{
    switch (id) {
    case WX_SPR_RAIN:
        stroke_line(&out[0], 6, 2, 6 + 14 * RAIN_SLANT, 16, 0.9f);
        return 1;
    case WX_SPR_FLAKE_S:
        stroke_line(&out[0], 3, 3, 3, 3, 1.6f);
        return 1;
    case WX_SPR_FLAKE_L:
        for (int i = 0; i < 3; i++) {
            float a = (float)M_PI / 2 + i * (float)M_PI / 3, c = cosf(a) * 5.2f, s = sinf(a) * 5.2f;
            stroke_line(&out[i], 7 - c, 7 - s, 7 + c, 7 + s, 0.8f);
        }
        return 3;
    case WX_SPR_CLOUD_L:
    case WX_SPR_CLOUD_S: {
        if (frame != WX_CLOUD_OUTLINE) return 0;
        float sc, oy;
        cloud_xf(id, &sc, &oy);
        wx_stroke_t *s = &out[0];
        const float cx = 58, cy = 36;
        for (int k = 0; k < CLOUD_PTS; k++) {
            float a = TAU * k / CLOUD_PTS, ca = cosf(a), sa = sinf(a), r = 0;
            while (r < 80 && cloud_inside(cx + (r + 0.25f) * ca, cy + (r + 0.25f) * sa)) r += 0.25f;
            s->x[k] = sc * (cx + r * ca);
            s->y[k] = oy + sc * (cy + r * sa);
        }
        s->x[CLOUD_PTS] = s->x[0];
        s->y[CLOUD_PTS] = s->y[0];
        s->n = CLOUD_PTS + 1;
        s->hw = id == WX_SPR_CLOUD_S ? 1.2f : 1.5f;
        return 1;
    }
    case WX_SPR_BOLT: {
        static const float bx[4] = {13, 5, 11, 5}, by[4] = {3, 20, 20, 37};
        out[0].n = 4;
        memcpy(out[0].x, bx, sizeof bx);
        memcpy(out[0].y, by, sizeof by);
        out[0].hw = 1.4f;
        return 1;
    }
    case WX_SPR_MOON:
        moon_stroke(&out[0]);
        return 1;
    case WX_SPR_STAR:
        stroke_line(&out[0], 5, 1.3f, 5, 8.7f, 0.7f);
        stroke_line(&out[1], 1.3f, 5, 8.7f, 5, 0.7f);
        return 2;
    case WX_SPR_SUN:
        out[0].n = 0;
        out[0].hw = 1.5f;
        arc_append(&out[0], 18, 18, 13, 0, TAU, 49);
        return 1;
    case WX_SPR_RAYS: {
        float base = (float)(frame % WX_RAY_STEPS) * ((float)M_PI / 4) / WX_RAY_STEPS;
        for (int i = 0; i < 8; i++) {
            float a = base + i * (float)M_PI / 4, c = cosf(a), s = sinf(a);
            stroke_line(&out[i], 30 + 21 * c, 30 + 21 * s, 30 + 27 * c, 30 + 27 * s, 1.4f);
        }
        return 8;
    }
    case WX_SPR_FOG: {
        wx_stroke_t *s = &out[0];
        s->n = 24;
        s->hw = 1.4f;
        for (int k = 0; k < s->n; k++) {
            float x = 5 + 118.0f * k / (s->n - 1);
            s->x[k] = x;
            s->y[k] = 5 + 1.6f * sinf(TAU * x / 40);
        }
        return 1;
    }
    default:
        return 0;
    }
}

void wx_sprite_fill(int id, int frame, uint8_t *mask)
{
    if ((id != WX_SPR_CLOUD_L && id != WX_SPR_CLOUD_S) || frame != WX_CLOUD_FILL) return;
    float sc, oy;
    cloud_xf(id, &sc, &oy);
    wx_sprite_size_t z = SIZES[id];
    for (int y = 0; y < z.h; y++)
        for (int x = 0; x < z.w; x++)
            if (cloud_inside((x + 0.5f) / sc, (y + 0.5f - oy) / sc)) mask[y * z.w + x] = 255;
}

static uint32_t rng_next(wx_scene_t *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s->rng = x;
}

static float frand(wx_scene_t *s) { return (float)(rng_next(s) >> 8) / 16777216.0f; }

#define CLOUD_TOP_Y 4
#define DROP_TOP 50.0f
#define DROP_X0 58.0f
#define DROP_X1 154.0f
#define FLAKE_X0 40.0f
#define FLAKE_X1 160.0f

static int drop_count(const wx_look_t *l)
{
    if (l->face == WX_RAIN) return l->intensity == WX_INT_LIGHT ? 5 : l->intensity == WX_INT_HEAVY ? 16 : 10;
    if (l->face == WX_STORM) return l->intensity == WX_INT_HEAVY ? 14 : 10;
    if (l->face == WX_SNOW) return l->intensity == WX_INT_LIGHT ? 6 : l->intensity == WX_INT_HEAVY ? 12 : 9;
    return 0;
}

static void spawn(wx_scene_t *s, wx_particle_t *p, bool initial)
{
    memset(p, 0, sizeof *p);
    if (s->look.face == WX_SNOW) {
        p->sprite = frand(s) < 0.4f ? WX_SPR_FLAKE_L : WX_SPR_FLAKE_S;
        int w = SIZES[p->sprite].w, h = SIZES[p->sprite].h;
        p->amp = 4 + 4 * frand(s);
        p->freq = 0.5f + 0.4f * frand(s);
        p->phase = TAU * frand(s);
        p->x = FLAKE_X0 + p->amp + frand(s) * (FLAKE_X1 - FLAKE_X0 - 2 * p->amp - w);
        float ymax = WX_SKY_H - h;
        p->y = initial ? DROP_TOP + frand(s) * (ymax - DROP_TOP) : DROP_TOP - 2 + 4 * frand(s);
        p->vy = 18 + 14 * frand(s);
    } else {
        p->sprite = WX_SPR_RAIN;
        p->vy = 150 * (0.9f + 0.2f * frand(s));
        p->vx = p->vy * RAIN_SLANT;
        p->x = DROP_X0 + frand(s) * (DROP_X1 - DROP_X0);
        float ymax = WX_SKY_H - SIZES[WX_SPR_RAIN].h;
        p->y = initial ? DROP_TOP + frand(s) * (ymax - DROP_TOP) : DROP_TOP + 6 * frand(s);
        if (initial) p->x -= (p->y - DROP_TOP) * -RAIN_SLANT;
    }
}

static void schedule_bolt(wx_scene_t *s, double from, double min_gap, double spread)
{
    s->next_bolt = from + min_gap + spread * frand(s);
}

void wx_scene_init(wx_scene_t *s, uint32_t seed)
{
    memset(s, 0, sizeof *s);
    s->rng = seed ? seed : 0x9E3779B9u;
    s->look.face = WX_FACE_COUNT;
    s->dirty = true;
}

bool wx_scene_set_look(wx_scene_t *s, const wx_look_t *look)
{
    if (wx_look_equal(&s->look, look)) return false;
    s->look = *look;
    s->np = drop_count(look);
    for (int i = 0; i < s->np; i++) spawn(s, &s->p[i], true);
    static const int SX[WX_STARS] = {28, 158, 44, 150, 178, 14}, SY[WX_STARS] = {26, 18, 98, 104, 60, 62};
    for (int i = 0; i < WX_STARS; i++) {
        s->star_x[i] = SX[i] + (int)(frand(s) * 8) - 4;
        s->star_y[i] = SY[i] + (int)(frand(s) * 8) - 4;
    }
    s->twinkle = 0;
    s->twinkle_start = s->t;
    s->next_twinkle = s->t + 1.0;
    s->bolt_on = s->bolt_off = s->bolt_on2 = s->bolt_off2 = -1;
    schedule_bolt(s, s->t, 2, 3);
    s->acc = s->since = 0;
    s->dirty = true;
    return true;
}

double wx_scene_period(const wx_look_t *l)
{
    if (l->still) return 0;
    switch (l->face) {
    case WX_RAIN:
    case WX_STORM: return 1.0 / 12;
    case WX_SNOW: return 1.0 / 10;
    default: return 1.0 / 4;
    }
}

static void step(wx_scene_t *s, double dt)
{
    s->t += dt;
    float fdt = (float)dt;
    for (int i = 0; i < s->np; i++) {
        wx_particle_t *p = &s->p[i];
        p->x += p->vx * fdt;
        p->y += p->vy * fdt;
        int h = SIZES[p->sprite].h;
        if (p->y > WX_SKY_H - h) spawn(s, p, false);
    }
    if (s->t >= s->next_twinkle) {
        s->twinkle = (int)(frand(s) * WX_STARS) % WX_STARS;
        s->twinkle_start = s->t;
        s->next_twinkle = s->t + 0.8 + 0.8 * frand(s);
    }
    if (s->look.face == WX_STORM && s->t >= s->next_bolt) {
        s->bolt_on = s->t;
        s->bolt_off = s->t + 0.25;
        if (frand(s) < 0.5f) {
            s->bolt_on2 = s->t + 0.4;
            s->bolt_off2 = s->t + 0.6;
        } else {
            s->bolt_on2 = s->bolt_off2 = -1;
        }
        s->bolt_x = 60 + (int)(frand(s) * 70);
        schedule_bolt(s, s->t, 4, 6);
    }
}

void wx_scene_invalidate(wx_scene_t *s) { s->dirty = true; }

bool wx_scene_tick(wx_scene_t *s, double dt)
{
    double per = wx_scene_period(&s->look);
    if (per <= 0) {
        bool d = s->dirty;
        s->dirty = false;
        return d;
    }
    s->acc += dt;
    s->since += dt;
    if (!s->dirty && s->acc < per) return false;
    s->acc = s->dirty ? 0 : s->acc - per;
    if (s->acc > per) s->acc = 0;
    double elapsed = s->dirty ? 0 : (s->since > 0.5 ? 0.5 : s->since);
    s->since = 0;
    s->dirty = false;
    step(s, elapsed);
    return true;
}

bool wx_scene_flash(const wx_scene_t *s)
{
    return s->look.face == WX_STORM && !s->look.still &&
           ((s->t >= s->bolt_on && s->t < s->bolt_off) || (s->t >= s->bolt_on2 && s->t < s->bolt_off2));
}

typedef struct {
    wx_draw_t *out;
    int n, max;
    bool still;
} emit_t;

#define STILL_RGB 0x5A5A5A

static void emit(emit_t *e, int sprite, int frame, float x, float y, uint32_t rgb, int alpha)
{
    if (e->n >= e->max) return;
    wx_draw_t *d = &e->out[e->n++];
    d->sprite = (uint8_t)sprite;
    d->frame = (uint8_t)frame;
    d->x = (int16_t)floorf(x + 0.5f);
    d->y = (int16_t)floorf(y + 0.5f);
    d->rgb = e->still && rgb ? STILL_RGB : rgb;
    d->alpha = (uint8_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
}

static void cloud(emit_t *e, int id, float x, float y, uint32_t rgb, int alpha)
{
    emit(e, id, WX_CLOUD_FILL, x, y, 0x000000, 255);
    emit(e, id, WX_CLOUD_OUTLINE, x, y, rgb, alpha);
}

static float drift(double t, float amp, float period, float phase)
{
    return amp * sinf(TAU * (float)fmod(t / period, 1.0) + phase);
}

#define SUN_RGB 0xE0A030
#define RAYS_RGB 0xC08828
#define MOON_RGB 0xD8D0B0
#define STAR_RGB 0xC8D0E0
#define CLOUD_RGB 0x9AA4B0
#define RAIN_CLOUD_RGB 0x7C8694
#define STORM_CLOUD_RGB 0x6A7280
#define LIT_CLOUD_RGB 0xC8CCD8
#define SNOW_CLOUD_RGB 0xA8B0BC
#define RAIN_RGB 0x5C9CE0
#define SNOW_RGB 0xD0DCE8
#define BOLT_RGB 0xF0D040
#define FOG_RGB 0x8890A0
#define RIME_RGB 0x90C8E0

static void sun(emit_t *e, const wx_scene_t *s, float cx, float cy)
{
    int step = e->still ? 0 : (int)fmod(s->t * 4.0, WX_RAY_STEPS);
    emit(e, WX_SPR_RAYS, step, cx - 30, cy - 30, RAYS_RGB, 255);
    emit(e, WX_SPR_SUN, 0, cx - 18, cy - 18, SUN_RGB, 255);
}

static void particles(emit_t *e, const wx_scene_t *s, uint32_t rgb, int alpha)
{
    for (int i = 0; i < s->np; i++) {
        const wx_particle_t *p = &s->p[i];
        float x = p->x;
        if (p->amp > 0) x += p->amp * sinf(TAU * (float)fmod(s->t * p->freq, 1.0) + p->phase);
        emit(e, p->sprite, 0, x, p->y, rgb, alpha);
    }
}

int wx_scene_draw(const wx_scene_t *s, wx_draw_t *out, int max)
{
    emit_t e = {.out = out, .max = max, .still = s->look.still};
    const wx_look_t *l = &s->look;
    double t = s->t;
    switch (l->face) {
    case WX_CLEAR_DAY:
        sun(&e, s, 100, 70);
        break;
    case WX_CLEAR_NIGHT:
        for (int i = 0; i < WX_STARS; i++) {
            int a = 90;
            if (i == s->twinkle && !l->still) {
                double dur = s->next_twinkle - s->twinkle_start, k = dur > 0 ? (t - s->twinkle_start) / dur : 0;
                a += (int)(165 * sin(M_PI * (k < 0 ? 0 : k > 1 ? 1 : k)));
            }
            emit(&e, WX_SPR_STAR, 0, (float)s->star_x[i], (float)s->star_y[i], STAR_RGB, a);
        }
        emit(&e, WX_SPR_MOON, 0, 80, 46, MOON_RGB, 255);
        break;
    case WX_PARTLY_CLOUDY:
        if (l->night) emit(&e, WX_SPR_MOON, 0, 48, 30, MOON_RGB, 255);
        else sun(&e, s, 70, 52);
        cloud(&e, WX_SPR_CLOUD_L, 74 + drift(t, 14, 40, 0), 62, CLOUD_RGB, 255);
        break;
    case WX_OVERCAST:
        cloud(&e, WX_SPR_CLOUD_S, 22 + drift(t, 10, 50, 1), 24, CLOUD_RGB, 150);
        cloud(&e, WX_SPR_CLOUD_L, 66 + drift(t, 16, 36, 0), 52, CLOUD_RGB, 255);
        break;
    case WX_FOG: {
        static const float A[4] = {28, 22, 30, 18}, P[4] = {23, 31, 19, 27}, PH[4] = {0, 2.1f, 4.0f, 1.2f};
        for (int i = 0; i < 4; i++)
            emit(&e, WX_SPR_FOG, 0, 36 + drift(t, A[i], P[i], PH[i]), 30 + 22.0f * i, l->rime ? RIME_RGB : FOG_RGB,
                 i % 2 ? 140 : 210);
        break;
    }
    case WX_RAIN:
    case WX_STORM:
    case WX_SNOW: {
        bool storm = l->face == WX_STORM, flash = wx_scene_flash(s);
        if (flash) emit(&e, WX_SPR_BOLT, 0, (float)s->bolt_x, DROP_TOP, BOLT_RGB, 255);
        uint32_t crgb = l->face == WX_SNOW ? SNOW_CLOUD_RGB : storm ? (flash ? LIT_CLOUD_RGB : STORM_CLOUD_RGB)
                                                                     : RAIN_CLOUD_RGB;
        cloud(&e, WX_SPR_CLOUD_L, 45 + drift(t, 6, 30, 0), CLOUD_TOP_Y, crgb, 255);
        if (l->face == WX_SNOW) particles(&e, s, SNOW_RGB, 230);
        else particles(&e, s, RAIN_RGB, l->intensity == WX_INT_LIGHT ? 170 : l->intensity == WX_INT_HEAVY ? 255 : 210);
        break;
    }
    default:
        break;
    }
    return e.n;
}
