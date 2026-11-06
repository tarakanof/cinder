#include "room_face.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "room_report.h"
#include "wx_temp.h"

#define MINUS "\xE2\x88\x92"
#define DEGREE "\xC2\xB0"
#define RING_STEPS (WX_RING_LUT - 1)

typedef struct {
    float x;
    uint32_t rgb;
} stop_t;

static const stop_t AIR[] = {{400, 0x4FD6CC}, {600, 0x50CCAA}, {800, 0xF0E641}, {1200, 0xFF5050}, {1800, 0xD03060}};
static const stop_t LUX[] = {{0, 0x3A3F66}, {0.5f, 0xB8A868}, {1, 0xF4F0C0}};
static const uint32_t BAND[] = {0x4FD6CC, 0x50CCAA, 0xF0E641, 0xFF5050, 0xD03060};
static const char *const WORD[] = {"Excellent air", "Good air", "Moderate air", "Poor air", "Unhealthy air"};

static const uint32_t FACE_HAS[ROOM_FACE_N] = {ROOM_HAS_CLIMATE, ROOM_HAS_AIR, ROOM_HAS_LIGHT};

unsigned room_faces_mask(uint32_t found)
{
    unsigned m = 0;
    for (int f = 0; f < ROOM_FACE_N; f++)
        if (found & FACE_HAS[f]) m |= 1u << f;
    return m;
}

static bool has(unsigned mask, int face) { return face >= 0 && face < ROOM_FACE_N && (mask >> face & 1u); }

int room_faces_n(unsigned mask)
{
    int n = 0;
    for (int f = 0; f < ROOM_FACE_N; f++) n += has(mask, f);
    return n;
}

int room_face_pos(unsigned mask, int face)
{
    if (!has(mask, face)) return -1;
    int k = 0;
    for (int f = 0; f < face; f++) k += has(mask, f);
    return k;
}

static int nth(unsigned mask, int k)
{
    for (int f = 0; f < ROOM_FACE_N; f++)
        if (has(mask, f) && k-- == 0) return f;
    return -1;
}

int room_face_step(unsigned mask, int face, int steps)
{
    int n = room_faces_n(mask);
    if (n == 0) return -1;
    int pos = room_face_pos(mask, face);
    if (pos < 0) {
        if (steps == 0) return nth(mask, 0);
        pos = steps > 0 ? -1 : n;
    }
    int s = steps % n;
    return nth(mask, ((pos + s) % n + n) % n);
}

static bool air_reading(const room_t *r, int64_t now_ms) { return (r->has & ROOM_HAS_AIR) && room_fresh(r->air_ms, now_ms); }

static bool air_have(const room_t *r, int64_t now_ms)
{
    return air_reading(r, now_ms) && r->air_validity == ENS160_NORMAL && r->aqi >= 1 && r->aqi <= 5;
}

bool room_air_poor(const room_t *r, int64_t now_ms, bool shown)
{
    if (!air_have(r, now_ms)) return false;
    return shown ? r->aqi > ROOM_AQI_CLEAR : r->aqi >= ROOM_AQI_POOR;
}

int room_face_auto(unsigned mask, bool poor)
{
    if (poor && has(mask, ROOM_FACE_AIR)) return ROOM_FACE_AIR;
    if (has(mask, ROOM_FACE_CLIMATE)) return ROOM_FACE_CLIMATE;
    return nth(mask, 0);
}

void room_faces_open(room_faces_t *f) { *f = (room_faces_t){.pick = ROOM_FACE_AUTO, .face = -1}; }

int room_faces_resolve(room_faces_t *f, unsigned mask, const room_t *r, int64_t now_ms)
{
    int face;
    if (f->pick != ROOM_FACE_AUTO && has(mask, f->pick)) {
        face = f->pick;
    } else {
        bool shown = f->pick == ROOM_FACE_AUTO && f->face == ROOM_FACE_AIR;
        face = room_face_auto(mask, room_air_poor(r, now_ms, shown));
    }
    f->face = face;
    return face;
}

void room_faces_turn(room_faces_t *f, unsigned mask, int detents)
{
    int base = has(mask, f->pick) ? f->pick : has(mask, f->face) ? f->face : nth(mask, 0);
    int to = room_face_step(mask, base, detents);
    if (to >= 0) f->pick = to;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static uint32_t lerp(uint32_t a, uint32_t b, float f)
{
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
        out |= (uint32_t)lroundf(ca + (cb - ca) * f) << s;
    }
    return out;
}

static uint32_t ramp(const stop_t *s, int n, float x)
{
    if (!(x > s[0].x)) return s[0].rgb;
    for (int i = 1; i < n; i++)
        if (x <= s[i].x) return lerp(s[i - 1].rgb, s[i].rgb, (x - s[i - 1].x) / (s[i].x - s[i - 1].x));
    return s[n - 1].rgb;
}

uint32_t room_ring_rgb(int face, float frac)
{
    float f = clamp01(frac);
    switch (face) {
    case ROOM_FACE_AIR:
        return ramp(AIR, (int)(sizeof AIR / sizeof AIR[0]), ROOM_ECO2_LO + (ROOM_ECO2_HI - ROOM_ECO2_LO) * f);
    case ROOM_FACE_LIGHT:
        return ramp(LUX, (int)(sizeof LUX / sizeof LUX[0]), f);
    default:
        return wx_temp_rgb(ROOM_TEMP_LO + (ROOM_TEMP_HI - ROOM_TEMP_LO) * f);
    }
}

void room_ring_lut(int face, bool grey, uint16_t lut[WX_RING_LUT])
{
    for (int i = 0; i <= RING_STEPS; i++) lut[i] = wx_rgb565(grey ? ROOM_TRACK_RGB : room_ring_rgb(face, (float)i / RING_STEPS));
}

const char *room_air_word(int aqi) { return aqi >= 1 && aqi <= 5 ? WORD[aqi - 1] : ""; }

uint32_t room_air_rgb(int aqi) { return aqi >= 1 && aqi <= 5 ? BAND[aqi - 1] : ROOM_GREY_RGB; }

float room_lux_frac(float lux)
{
    if (!(lux > 1)) return 0;
    return clamp01(logf(lux) / logf((float)ROOM_LUX_HI));
}

int room_panel_pct(int level)
{
    if (level <= 0) return 0;
    if (level >= 255) return 100;
    return (level * 100 + 127) / 255;
}

static int whole(float v, int lo, int hi)
{
    long r = lroundf(v);
    return r < lo ? lo : r > hi ? hi : (int)r;
}

static void climate(const room_t *r, int64_t now_ms, room_view_t *v)
{
    if (!(r->has & ROOM_HAS_CLIMATE) || !room_fresh(r->climate_ms, now_ms) || !isfinite(r->temp_c)) return;
    v->have = true;
    v->value = whole(r->temp_c, -40, 85);
    v->frac = clamp01((r->temp_c - ROOM_TEMP_LO) / (ROOM_TEMP_HI - ROOM_TEMP_LO));
    if (isfinite(r->rh_pct)) snprintf(v->line2, sizeof v->line2, "%d %%", whole(r->rh_pct, 0, 100));
    v->line2_rgb = ROOM_TEXT_RGB;
}

static void air(const room_t *r, int64_t now_ms, room_view_t *v)
{
    strcpy(v->line2, "ppm eCO2");
    v->line2_rgb = ROOM_UNIT_RGB;
    if (!air_have(r, now_ms)) {
        v->warm = air_reading(r, now_ms) && r->air_validity != ENS160_NORMAL;
        if (v->warm) strcpy(v->line2, "warming up");
        return;
    }
    v->have = true;
    v->aqi = r->aqi;
    v->value = r->eco2_ppm > ROOM_VALUE_MAX ? ROOM_VALUE_MAX : r->eco2_ppm;
    v->frac = clamp01((float)(r->eco2_ppm - ROOM_ECO2_LO) / (ROOM_ECO2_HI - ROOM_ECO2_LO));
    strcpy(v->line3, room_air_word(r->aqi));
    v->line3_rgb = room_air_rgb(r->aqi);
}

static void light(const room_t *r, int64_t now_ms, int panel_level, room_view_t *v)
{
    strcpy(v->line2, "lux");
    v->line2_rgb = ROOM_UNIT_RGB;
    if (panel_level >= 0) snprintf(v->line3, sizeof v->line3, "Panel %d %%", room_panel_pct(panel_level));
    v->line3_rgb = ROOM_TEXT_RGB;
    if (!(r->has & ROOM_HAS_LIGHT) || !room_fresh(r->light_ms, now_ms) || !isfinite(r->lux) || r->lux < 0) return;
    v->have = true;
    v->value = whole(r->lux, 0, ROOM_VALUE_MAX);
    v->frac = room_lux_frac(r->lux);
}

void room_view(const room_t *r, int64_t now_ms, int face, int panel_level, room_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->face = face;
    if (face == ROOM_FACE_CLIMATE) climate(r, now_ms, v);
    else if (face == ROOM_FACE_AIR) air(r, now_ms, v);
    else if (face == ROOM_FACE_LIGHT) light(r, now_ms, panel_level, v);
}

void room_value_text(const room_view_t *v, int value, char *buf, size_t n)
{
    bool deg = v->face == ROOM_FACE_CLIMATE;
    if (!v->have) snprintf(buf, n, MINUS MINUS "%s", deg ? DEGREE : "");
    else if (value < 0) snprintf(buf, n, MINUS "%d%s", -value, deg ? DEGREE : "");
    else snprintf(buf, n, "%d%s", value, deg ? DEGREE : "");
}

uint32_t room_value_rgb(const room_view_t *v, int value)
{
    if (!v->have) return ROOM_GREY_RGB;
    if (v->face == ROOM_FACE_AIR) return room_air_rgb(v->aqi);
    if (v->face == ROOM_FACE_LIGHT) return room_ring_rgb(ROOM_FACE_LIGHT, room_lux_frac((float)value));
    return wx_temp_rgb((float)value);
}
