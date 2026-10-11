#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_paint.h"
#include "wx_rain.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define OBSERVED 1782043200LL

static wx_obs_t obs(const char *cond, const char *code)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = 12, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .has_night = true, .moon_up = -1, .observed_at = OBSERVED, .has_obs_age = true,
                  .obs_age_s = 300};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "%s", cond);
    snprintf(o.code, sizeof o.code, "%s", code);
    return o;
}

static void detail(wx_obs_t *o, int now_rain, long long until, int kind)
{
    o->has_now_rain = true;
    o->now_rain = now_rain;
    o->has_rain = true;
    o->rain_until = until;
    o->has_utc_offset = true;
    o->utc_offset = 3600;
    o->rain_n = 1;
    o->rain_spell[0] = (wx_spell_t){OBSERVED - 900, until ? until : OBSERVED + 7200, 2, kind};
}

static void test_select(void)
{
    wx_rain_t r;
    wx_rain_from(NULL, &r);
    CHECK(!r.on, "no data: off");

    wx_obs_t o = obs("rain", "61");
    detail(&o, 1, 1782049500, 0);
    o.has_pop = true;
    o.pop = 83;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.kind == WX_PRECIP_RAIN && r.intensity == 2, "now.rain 1 in a moderate spell: the spell's peak");
    CHECK(r.has_pop && r.pop == 83 && r.has_until && r.until_min == 14 * 60 + 45 && !strcmp(r.sub, "until 14:45"),
          "chance and until: '%s'", r.sub);
    o.rain_spell[0].intensity = 3;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.intensity == 3, "a heavy spell: heavy");
    o.rain_n = 0;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.intensity == 1, "no spell covers now: now.rain");
    o.now_rain = 3;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.intensity == 3, "no spell covers now: now.rain 3, heavy");
    o.rain_n = 1;
    o.rain_spell[0].intensity = 2;

    o.now_rain = 0;
    o.rain_until = 0;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.intensity == 2 && !r.has_until && !strcmp(r.sub, "Rain"),
          "now.rain 0 in a spell (a pause, not the end): the Rain face, the spell's peak, '%s'", r.sub);
    o.rain_n = 0;
    o.has_rain = true;
    wx_rain_from(&o, &r);
    CHECK(!r.on, "cond rain but now.rain 0 and no spell (polar dry fixture): off");

    long long now = OBSERVED + 300;
    o = obs("clouds", "3");
    detail(&o, 0, 0, 0);
    o.has_now_rain = false;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.intensity == 2, "without now.rain: a spell covering now");
    o.rain_spell[0] = (wx_spell_t){now + 1, now + 3600, 3, 0};
    wx_rain_from(&o, &r);
    CHECK(!r.on, "without now.rain: no spell covering now, off");
    o.rain_spell[0].start = now;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.intensity == 3, "a spell starting now covers now (start <= now)");
    o.rain_spell[0] = (wx_spell_t){now - 3600, now, 2, 0};
    wx_rain_from(&o, &r);
    CHECK(!r.on, "a spell ending now does not (now < end)");
    o.rain_spell[0] = (wx_spell_t){OBSERVED + 200, OBSERVED + 3600, 2, 0};
    wx_rain_from(&o, &r);
    CHECK(r.on, "a spell started after observed_at but before the knob's clock covers now");
    o.rain_spell[0] = (wx_spell_t){OBSERVED - 3600, OBSERVED + 200, 2, 0};
    wx_rain_from(&o, &r);
    CHECK(!r.on, "a spell ended on the knob's clock, not at observed_at: off");

    o = obs("rain", "61");
    detail(&o, 2, now, 0);
    o.rain_n = 0;
    wx_rain_from(&o, &r);
    CHECK(!r.on, "until == the knob's now: dry (now >= until)");
    o.rain_until = now + 1;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.has_until, "until a second ahead: wet");

    o = obs("clouds", "3");
    detail(&o, 2, 1782049500, 1);
    wx_rain_from(&o, &r);
    CHECK(r.on && r.kind == WX_PRECIP_SNOW && r.intensity == 2, "condition not wet, the step is: the spell's kind (snow)");
    o.rain_spell[0].kind = 0;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.kind == WX_PRECIP_RAIN, "spell kind 0: rain");
    o = obs("snow", "73");
    detail(&o, 2, 1782049500, 0);
    wx_rain_from(&o, &r);
    CHECK(r.on && r.kind == WX_PRECIP_SNOW, "the condition wins over the spell's kind");

    o = obs("storm", "95");
    detail(&o, 0, 0, 0);
    o.rain_n = 0;
    wx_rain_from(&o, &r);
    CHECK(r.on && r.kind == WX_PRECIP_STORM && r.intensity == 2 && !r.has_until && !strcmp(r.sub, "Storm"),
          "storm without a wet step: storm face, condition intensity, the word: '%s'", r.sub);
    o.code[0] = 0;
    snprintf(o.code, sizeof o.code, "99");
    wx_rain_from(&o, &r);
    CHECK(r.on && r.intensity == 3, "storm 99: heavy");
    o.rain_until = OBSERVED + 3600;
    wx_rain_from(&o, &r);
    CHECK(r.on && !r.has_until && !strcmp(r.sub, "Storm"), "storm, dry now, until ahead: no until '%s'", r.sub);
    o.rain_until = OBSERVED + 100;
    wx_rain_from(&o, &r);
    CHECK(r.on && !r.has_until && !strcmp(r.sub, "Storm"), "storm, until passed: never a past until '%s'", r.sub);

    o = obs("rain", "65");
    wx_rain_from(&o, &r);
    CHECK(r.on && r.kind == WX_PRECIP_RAIN && r.intensity == 3 && !r.has_pop && !strcmp(r.sub, "Heavy rain"),
          "legacy (no detail): condition and code: '%s'", r.sub);
    o = obs("snow", "71");
    wx_rain_from(&o, &r);
    CHECK(r.on && r.kind == WX_PRECIP_SNOW && r.intensity == 1 && !strcmp(r.sub, "Light snow"), "legacy light snow");
    o = obs("clouds", "3");
    wx_rain_from(&o, &r);
    CHECK(!r.on, "legacy clouds: off");
    o = obs("rain", "61");
    o.stale = true;
    wx_rain_from(&o, &r);
    CHECK(!r.on, "stale: off (the Temperature face greys it)");
    o.stale = false;
    o.age_s = WX_MAX_AGE_S + 1;
    wx_rain_from(&o, &r);
    CHECK(!r.on, "old: off");
    o.age_s = 0;
    o.enabled = false;
    wx_rain_from(&o, &r);
    CHECK(!r.on, "disabled: off");

    o = obs("rain", "61");
    detail(&o, 2, OBSERVED + 600, 0);
    wx_rain_from(&o, &r);
    CHECK(r.on, "until ahead");
    o.age_s = 400;
    wx_rain_from(&o, &r);
    CHECK(!r.on, "until passed on the knob's clock (observation age + time since receipt): off");
    o.has_obs_age = false;
    wx_rain_from(&o, &r);
    CHECK(r.on, "no clock: trust now.rain");

    o = obs("rain", "61");
    detail(&o, 2, 1782049500, 0);
    o.utc_offset = -5 * 3600 - 1800;
    wx_rain_from(&o, &r);
    CHECK(!strcmp(r.sub, "until 08:15"), "negative offset: '%s'", r.sub);
    o.rain_until = 1782000000 + 86400 - 60;
    o.utc_offset = 3600;
    wx_rain_from(&o, &r);
    CHECK(!strcmp(r.sub, "until 00:59"), "wraps past midnight: '%s'", r.sub);
    o.has_utc_offset = false;
    wx_rain_from(&o, &r);
    CHECK(!r.has_until && !strcmp(r.sub, "Rain"), "no offset: the word '%s'", r.sub);

    CHECK(wx_rain_rgb(1) == 0x4F81B7 && wx_rain_rgb(2) == 0x5C9CE0 && wx_rain_rgb(3) == 0x3D7BF5, "intensity colours");
}

static void test_bake_kind(void)
{
    wx_obs_t o = obs("rain", "61");
    detail(&o, 2, 1782049500, 0);
    o.has_pop = true;
    o.pop = 60;
    o.has_cloud = true;
    o.cloud = 95;
    wx_temp_t a, b;
    wx_temp_from(&o, &a);
    CHECK(a.rain.on && a.have, "rain face, temperature kept");
    o.pop = 90;
    o.rain_until += 900;
    o.temp_c = 14;
    o.cloud = 50;
    wx_temp_from_shown(&o, &a, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == 0, "chance, until, temperature, clouds: no bake on the rain face");
    o.has_pop = false;
    wx_temp_from(&o, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == WX_BAKE_RING, "chance appears or goes: ring only");
    o.has_pop = true;
    for (int step = 0; step < 4; step++) {
        static const int NOW[4] = {1, 2, 0, 1};
        o.now_rain = NOW[step];
        wx_temp_from(&o, &b);
        CHECK(b.rain.on && wx_temp_bake_kind(&b, &a) == 0, "showers in a spell (now.rain %d): no re-bake", NOW[step]);
    }
    o.now_rain = 2;
    o.rain_spell[0].intensity = 3;
    wx_temp_from(&o, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "the spell's peak changes: full");
    o.rain_spell[0].intensity = 2;
    snprintf(o.condition, sizeof o.condition, "snow");
    snprintf(o.code, sizeof o.code, "73");
    o.now_rain = 2;
    wx_temp_from(&o, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "kind: full");
    o.now_rain = 0;
    o.rain_n = 0;
    wx_temp_from(&o, &b);
    CHECK(!b.rain.on && wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL && wx_temp_bake_kind(&a, &b) == WX_BAKE_FULL,
          "rain stops or starts: full both ways");
}

static const float CANVAS[3][8][5] = {
    {{70, 12, 16, 0.9f, 3}, {92, 70, 12, 0.6f, 3}, {64, 118, 18, 0.8f, 5}, {86, 30, 14, 0.75f, 3}, {66, 88, 16, 1.0f, 3}, {96, 128, 12, 0.55f, 4}},
    {{376, 20, 16, 0.85f, 3}, {396, 84, 12, 0.6f, 4}, {370, 126, 14, 0.95f, 3}, {390, 8, 14, 0.7f, 3}, {372, 60, 18, 0.9f, 5}, {398, 110, 12, 0.55f, 3}},
    {{172, 10, 16, 0.9f, 3}, {246, 64, 14, 0.7f, 3}, {298, 112, 12, 0.8f, 4}, {205, 128, 16, 0.6f, 3}, {226, 24, 14, 1.0f, 3},
     {186, 80, 12, 0.65f, 5}, {280, 40, 16, 0.8f, 3}, {160, 136, 14, 0.7f, 3}},
};
static const int CANVAS_N[3] = {6, 6, 8};

static void test_sheets(void)
{
    static const int BOX[3][4] = {{58, 150, 118, 332}, {348, 150, 402, 332}, {150, 46, 316, 132}};
    static const int TILE[3] = {20, 24, 17}, DELAY[3] = {6, 15, 0};
    static const double PERIOD[3] = {2.667, 3.2, 2.267};
    wx_rain_t mod = {.on = true, .kind = WX_PRECIP_RAIN, .intensity = 2};
    for (int s = 0; s < WX_SHEETS; s++) {
        const wx_sheet_t *sh = wx_sheet(s);
        CHECK(sh->box.x0 == BOX[s][0] && sh->box.y0 == BOX[s][1] && sh->box.x1 == BOX[s][2] && sh->box.y1 == BOX[s][3],
              "sheet %d box from the AR2 canvas", s);
        CHECK(sh->tile_frames == TILE[s] && sh->delay_frames == DELAY[s], "sheet %d timing", s);
        int frames = wx_sheet_frames(&mod, s);
        CHECK(fabs(frames / 15.0 - PERIOD[s]) < 0.002, "sheet %d period %.3f s at 15 fps (canvas %.3f)", s, frames / 15.0,
              PERIOD[s]);
        int moderate = 0;
        for (int n = 0; n < sh->n; n++) {
            const wx_drop_t *d = &sh->drop[n];
            if (d->level > 2) continue;
            int i = moderate++;
            CHECK(i < CANVAS_N[s] && d->x == CANVAS[s][i][0] && d->y == CANVAS[s][i][1] && d->len == CANVAS[s][i][2] &&
                      d->op == CANVAS[s][i][3] && d->slant == CANVAS[s][i][4] && d->tile == (i >= CANVAS_N[s] / 2),
                  "sheet %d drop %d is the canvas's", s, n);
        }
        CHECK(moderate == (s == 2 ? 8 : 6), "sheet %d: moderate rain = the canvas's %d drops", s, moderate);
        wx_rain_frame_t f;
        int prev = -1;
        for (int k = 0; k < frames; k++) {
            wx_rain_frame(&mod, k, &f);
            int a = (k + DELAY[s]) % frames;
            CHECK(f.off[s] == (int)lround(300.0 * a / frames) % 300, "sheet %d frame %d offset", s, k);
            if (prev >= 0) {
                int step = (f.off[s] - prev + 300) % 300;
                CHECK(step >= 300 / frames && step <= 300 / frames + 1, "sheet %d frame %d: even step %d", s, k, step);
            }
            prev = f.off[s];
        }
        wx_rain_frame(&mod, frames, &f);
        int off0 = f.off[s];
        wx_rain_frame(&mod, 0, &f);
        CHECK(off0 == f.off[s], "sheet %d loops after %d frames", s, frames);
        wx_rain_t snow = mod;
        snow.kind = WX_PRECIP_SNOW;
        CHECK(wx_sheet_frames(&snow, s) == 2 * frames, "snow falls at half speed");
    }
}

static int nonzero(const uint8_t *a, int n)
{
    int c = 0;
    for (int i = 0; i < n; i++) c += a[i] != 0;
    return c;
}

static void test_tiles(void)
{
    static uint8_t tile[166 * WX_SHEET_PERIOD];
    const char *names[] = {"light", "moderate", "heavy"};
    for (int s = 0; s < WX_SHEETS; s++) {
        const wx_sheet_t *sh = wx_sheet(s);
        int w = sh->box.x1 - sh->box.x0, prev = 0;
        for (int in = 1; in <= 3; in++) {
            wx_rain_t r = {.on = true, .kind = WX_PRECIP_RAIN, .intensity = in};
            wx_paint_sheet(tile, s, &r);
            int ink = nonzero(tile, w * WX_SHEET_PERIOD), drops = 0;
            for (int n = 0; n < sh->n; n++) {
                const wx_drop_t *d = &sh->drop[n];
                int x = (int)(d->x - d->slant / 2) - sh->box.x0;
                int y = ((int)(d->y + (d->tile ? WX_SHEET_H : 0) + d->len / 2)) % WX_SHEET_PERIOD;
                uint8_t v = x >= 0 && x < w ? tile[y * w + x] : 255;
                if (wx_drop_shown(d, &r)) {
                    drops++;
                    CHECK(x < 0 || x >= w || v >= (int)(d->op * 255) - 30, "sheet %d %s: drop %d at its middle %d (op %.2f)", s,
                          names[in - 1], n, v, d->op);
                } else {
                    CHECK(x < 0 || x >= w || v == 0, "sheet %d %s: drop %d hidden", s, names[in - 1], n);
                }
            }
            CHECK(ink > prev, "sheet %d: more ink with intensity", s);
            prev = ink;
            printf("rain: sheet %d %s: %d drops, %d ink px of %d in the strip\n", s, names[in - 1], drops, ink, w * WX_SHEET_PERIOD);
        }
        wx_rain_t heavy = {.on = true, .kind = WX_PRECIP_RAIN, .intensity = 3};
        wx_paint_sheet(tile, s, &heavy);
        for (int n = 0; n < sh->n; n++) {
            const wx_drop_t *d = &sh->drop[n];
            float tip = d->y + (d->tile ? WX_SHEET_H : 0) + d->len + 2;
            if (tip <= WX_SHEET_PERIOD) continue;
            int x = (int)(d->x - d->slant) - sh->box.x0, y = (int)(tip - WX_SHEET_PERIOD) - 1;
            CHECK(x < 0 || x >= w || tile[y * w + x] > 0, "sheet %d drop %d crosses the seam: its tip wraps to row %d", s, n, y);
        }
        wx_rain_t snow = {.on = true, .kind = WX_PRECIP_SNOW, .intensity = 2};
        wx_paint_sheet(tile, s, &snow);
        printf("rain: sheet %d snow: %d ink px\n", s, nonzero(tile, w * WX_SHEET_PERIOD));
        CHECK(nonzero(tile, w * WX_SHEET_PERIOD) > 0, "snow flakes");
    }
}

static void test_splash_drip_bolt(void)
{
    static const int HIT[WX_SPLASHES] = {4, 31, 17};
    for (int n = 0; n < WX_SPLASHES; n++)
        CHECK(wx_splash_hit(n) == HIT[n], "splash %d lands on frame %d, the canvas's %d", n, wx_splash_hit(n), HIT[n]);
    wx_rain_t r = {.on = true, .kind = WX_PRECIP_RAIN, .intensity = 2};
    wx_rain_frame_t f;
    int seen[WX_SPLASHES] = {0};
    for (int k = 0; k < 34; k++) {
        wx_rain_frame(&r, k, &f);
        for (int n = 0; n < WX_SPLASHES; n++) {
            int j = ((k - HIT[n]) % 34 + 34) % 34;
            bool on = j < 3;
            seen[n] += on;
            CHECK((f.splash_opa[n] > 0) == on, "splash %d frame %d", n, k);
            if (j == 1) CHECK(f.splash_opa[n] == 255 && f.splash_dy[n] == 0, "splash %d peak", n);
        }
        CHECK(!f.bolt, "no bolt in rain");
    }
    CHECK(seen[0] == 3 && seen[1] == 3 && seen[2] == 3, "three frames each");
    r.intensity = 1;
    wx_rain_frame(&r, HIT[1] + 1, &f);
    CHECK(f.splash_opa[1] == 0, "light rain: no splash for a drop it does not draw");
    static const int DY[7] = {0, 0, 1, 6, 14, 24, 34};
    r.intensity = 2;
    for (int k = 0; k < 96; k++) {
        wx_rain_frame(&r, k, &f);
        int jl = k % 24 - 5, jr = k % 32 - 13;
        CHECK((f.drip_pose[0] >= 0) == (jl >= 0 && jl < 7) && (f.drip_pose[1] >= 0) == (jr >= 0 && jr < 7), "drips frame %d", k);
        if (jl >= 0 && jl < 7) CHECK(f.drip_dy[0] == DY[jl], "left drip dy");
        if (jr >= 0 && jr < 7) CHECK(f.drip_dy[1] == DY[jr], "right drip dy");
    }
    r.kind = WX_PRECIP_STORM;
    static const int LIT[6] = {12, 13, 18, 19, 24, 25};
    bool lit[2 * WX_STORM_FRAMES + 1] = {0};
    int flashes = 0;
    for (int k = 0; k <= 2 * WX_STORM_FRAMES; k++) {
        wx_rain_frame(&r, k, &f);
        lit[k] = f.bolt;
        flashes += f.bolt;
        bool want = false;
        for (int i = 0; i < 6; i++) want |= k % WX_STORM_FRAMES == LIT[i];
        CHECK(f.bolt == want, "storm frame %d: bolt %d, the pinned rhythm says %d", k, f.bolt, want);
    }
    CHECK(flashes == 12, "storm: three 2-frame flashes per 6 s, %d frames lit in 12 s", flashes);
    for (int k0 = 0; k0 + 15 <= 2 * WX_STORM_FRAMES; k0++) {
        int edges = 0;
        for (int k = k0; k < k0 + 15; k++) edges += lit[k] && (k == 0 || !lit[k - 1]);
        CHECK(edges <= 3, "storm: %d flashes in the 1 s from frame %d (WCAG 2.3.1: at most 3)", edges, k0);
    }
    r.kind = WX_PRECIP_SNOW;
    for (int k = 0; k < 96; k++) {
        wx_rain_frame(&r, k, &f);
        CHECK(f.drip_pose[0] < 0 && f.drip_pose[1] < 0 && !f.splash_opa[0] && !f.splash_opa[1] && !f.splash_opa[2] && !f.bolt,
              "snow: no drips, splashes or bolt");
    }
}

static bool inside(wx_box_t in, int x0, int y0, int w, int h)
{
    return x0 >= in.x0 && y0 >= in.y0 && x0 + w <= in.x1 && y0 + h <= in.y1;
}

static void test_boxes(void)
{
    for (int i = 0; i < WX_DRIPS; i++)
        for (int dy = 0; dy <= 34; dy++)
            CHECK(inside(wx_sheet(i)->box, wx_drip_x(i), WX_DRIP_Y + dy, WX_DRIP_W, WX_DRIP_H), "drip %d stays in its sheet", i);
    for (int n = 0; n < WX_SPLASHES; n++) {
        wx_splash_at_t at;
        wx_splash_at(n, &at);
        for (int dy = -2; dy <= 1; dy++)
            CHECK(inside(wx_sheet(2)->box, at.x, at.y + dy, WX_SPLASH_W, WX_SPLASH_H), "splash %d stays in the top band", n);
    }
    CHECK(inside(wx_sheet(2)->box, WX_BOLT_X, WX_BOLT_Y, WX_BOLT_W, WX_BOLT_H), "bolt in the top band");
    wx_rain_layout_t l;
    wx_rain_layout(&l);
    for (int s = 0; s < WX_SHEETS; s++) {
        wx_box_t c = l.cover_box[s];
        CHECK(inside(wx_sheet(s)->box, c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0) && (c.x1 - c.x0) % 2 == 0, "cover %d in its box, even", s);
    }
    int still = wx_clouds_max_px(false) * 3;
    CHECK(l.total <= still, "rain layers %d B fit the still-cloud buffer %d B", l.total, still);
    printf("rain: layers %d B in the %d B still-cloud buffer\n", l.total, still);
}

static uint16_t blend(uint16_t d, uint32_t rgb, int a)
{
    if (a <= 0) return d;
    int dr = (d >> 11) << 3, dg = ((d >> 5) & 63) << 2, db = (d & 31) << 3;
    int r = dr + (((int)(rgb >> 16 & 255) - dr) * a + 127) / 255, g = dg + (((int)(rgb >> 8 & 255) - dg) * a + 127) / 255,
        b = db + (((int)(rgb & 255) - db) * a + 127) / 255;
    return wx_rgb565(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

static uint32_t rgb_of(uint16_t c) { return ((uint32_t)(c >> 11) << 19) | ((uint32_t)((c >> 5) & 63) << 10) | ((uint32_t)(c & 31) << 3); }

static void a8_over(uint16_t *fb, wx_box_t clip, const uint8_t *a8, int w, int h, int x0, int y0, uint32_t rgb, int opa)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int fx = x0 + x, fy = y0 + y;
            if (fx < clip.x0 || fx >= clip.x1 || fy < clip.y0 || fy >= clip.y1) continue;
            int a = a8[y * w + x] * opa / 255;
            fb[fy * WX_FACE + fx] = blend(fb[fy * WX_FACE + fx], rgb, a);
        }
}

typedef struct {
    int pose, y, opa;
    bool on;
} sim_obj_t;

typedef struct {
    int strip_y[WX_SHEETS][2], strip_opa, bolt_opa;
    sim_obj_t drip[WX_DRIPS], splash[WX_SPLASHES];
    bool bolt;
    wx_rain_frame_t shown;
    int shown_opa;
    wx_box_t inv[64];
    int n_inv;
} sim_t;

static void sim_inv(sim_t *v, int s, int x, int y, int w, int h)
{
    wx_box_t b = wx_sheet(s)->box;
    wx_box_t a = {x > b.x0 ? x : b.x0, y > b.y0 ? y : b.y0, x + w < b.x1 ? x + w : b.x1, y + h < b.y1 ? y + h : b.y1};
    if (a.x0 >= a.x1 || a.y0 >= a.y1 || v->n_inv >= 64) return;
    v->inv[v->n_inv++] = a;
}

static void sim_box(sim_t *v, int s)
{
    wx_box_t b = wx_sheet(s)->box;
    sim_inv(v, s, b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0);
}

static void sim_init(sim_t *v)
{
    memset(v, 0, sizeof *v);
    for (int s = 0; s < WX_SHEETS; s++) v->strip_y[s][0] = v->strip_y[s][1] = wx_sheet(s)->box.y0;
    for (int i = 0; i < WX_DRIPS; i++) v->drip[i] = (sim_obj_t){0, wx_sheet(i)->box.y0, 255, false};
    for (int n = 0; n < WX_SPLASHES; n++) v->splash[n] = (sim_obj_t){0, wx_sheet(2)->box.y0, 255, false};
    v->strip_opa = v->bolt_opa = 255;
}

static void sim_attach(sim_t *v)
{
    v->n_inv = 0;
    for (int s = 0; s < WX_SHEETS; s++) sim_box(v, s);
    for (int i = 0; i < WX_DRIPS; i++) v->drip[i].on = false;
    for (int n = 0; n < WX_SPLASHES; n++) v->splash[n].on = false;
    v->bolt = false;
    memset(&v->shown, 0xFF, sizeof v->shown);
    v->shown_opa = -1;
}

static void sim_move(sim_t *v, int s, sim_obj_t *o, int x, int w, int h, int y)
{
    if (o->on) sim_inv(v, s, x, o->y, w, h);
    o->y = wx_sheet(s)->box.y0 + y;
    if (o->on) sim_inv(v, s, x, o->y, w, h);
}

static void sim_touch(sim_t *v, int s, const sim_obj_t *o, int x, int w, int h)
{
    if (o->on) sim_inv(v, s, x, o->y, w, h);
}

static void sim_show(sim_t *v, int s, sim_obj_t *o, int x, int w, int h, bool on)
{
    if (on != o->on) sim_inv(v, s, x, o->y, w, h);
    o->on = on;
}

static void sim_frame(sim_t *v, const wx_rain_t *r, int k, int opa)
{
    v->n_inv = 0;
    wx_rain_frame_t f;
    wx_rain_frame(r, k, &f);
    wx_rain_ops_t op;
    wx_rain_ops(&v->shown, v->shown_opa, &f, opa, &op);
    v->shown_opa = opa;
    for (int s = 0; s < WX_SHEETS; s++) {
        for (int c = 0; c < 2 && op.strip_y[s]; c++) {
            v->strip_y[s][c] = wx_sheet(s)->box.y0 + op.strip_top[s][c];
            sim_box(v, s);
        }
        if (op.opa) {
            v->strip_opa = op.layer_opa;
            sim_box(v, s);
        }
    }
    for (int i = 0; i < WX_DRIPS; i++) {
        sim_obj_t *o = &v->drip[i];
        int x = wx_drip_x(i);
        if (op.drip_buf[i]) {
            o->pose = op.drip_pose[i];
            sim_touch(v, i, o, x, WX_DRIP_W, WX_DRIP_H);
        }
        if (op.drip_y[i]) sim_move(v, i, o, x, WX_DRIP_W, WX_DRIP_H, op.drip_top[i]);
        if (op.drip_opa[i]) {
            o->opa = op.drip_alpha[i];
            sim_touch(v, i, o, x, WX_DRIP_W, WX_DRIP_H);
        }
        sim_show(v, i, o, x, WX_DRIP_W, WX_DRIP_H, op.drip_on[i]);
    }
    for (int n = 0; n < WX_SPLASHES; n++) {
        sim_obj_t *o = &v->splash[n];
        wx_splash_at_t at;
        wx_splash_at(n, &at);
        if (op.splash_y[n]) sim_move(v, 2, o, at.x, WX_SPLASH_W, WX_SPLASH_H, op.splash_top[n]);
        if (op.splash_opa[n]) {
            o->opa = op.splash_alpha[n];
            sim_touch(v, 2, o, at.x, WX_SPLASH_W, WX_SPLASH_H);
        }
        sim_show(v, 2, o, at.x, WX_SPLASH_W, WX_SPLASH_H, op.splash_on[n]);
    }
    if (op.opa) {
        v->bolt_opa = op.layer_opa;
        if (v->bolt) sim_inv(v, 2, WX_BOLT_X, WX_BOLT_Y, WX_BOLT_W, WX_BOLT_H);
    }
    if (op.bolt_on != v->bolt) sim_inv(v, 2, WX_BOLT_X, WX_BOLT_Y, WX_BOLT_W, WX_BOLT_H);
    v->bolt = op.bolt_on;
    v->shown = f;
}

static void sim_ideal(sim_t *v, const wx_rain_frame_t *f, int opa)
{
    sim_init(v);
    for (int s = 0; s < WX_SHEETS; s++) {
        v->strip_y[s][0] = wx_sheet(s)->box.y0 + f->off[s] - WX_SHEET_PERIOD;
        v->strip_y[s][1] = wx_sheet(s)->box.y0 + f->off[s];
    }
    v->strip_opa = v->bolt_opa = opa;
    for (int i = 0; i < WX_DRIPS; i++)
        v->drip[i] = (sim_obj_t){f->drip_pose[i], WX_DRIP_Y + f->drip_dy[i], f->drip_opa[i] * opa / 255, f->drip_pose[i] >= 0};
    for (int n = 0; n < WX_SPLASHES; n++) {
        wx_splash_at_t at;
        wx_splash_at(n, &at);
        v->splash[n] = (sim_obj_t){0, at.y + f->splash_dy[n], f->splash_opa[n] * opa / 255, f->splash_opa[n] > 0};
    }
    v->bolt = f->bolt;
}

static bool sim_same(const sim_t *a, const sim_t *b, char *why, size_t n)
{
    for (int s = 0; s < WX_SHEETS; s++)
        if (a->strip_y[s][0] != b->strip_y[s][0] || a->strip_y[s][1] != b->strip_y[s][1] || a->strip_opa != b->strip_opa) {
            snprintf(why, n, "sheet %d strips at %d %d opa %d, want %d %d opa %d", s, a->strip_y[s][0], a->strip_y[s][1],
                     a->strip_opa, b->strip_y[s][0], b->strip_y[s][1], b->strip_opa);
            return false;
        }
    for (int i = 0; i < WX_DRIPS; i++) {
        const sim_obj_t *x = &a->drip[i], *y = &b->drip[i];
        if (x->on != y->on || (y->on && (x->pose != y->pose || x->y != y->y || x->opa != y->opa))) {
            snprintf(why, n, "drip %d on %d pose %d y %d opa %d, want on %d pose %d y %d opa %d", i, x->on, x->pose, x->y,
                     x->opa, y->on, y->pose, y->y, y->opa);
            return false;
        }
    }
    for (int k = 0; k < WX_SPLASHES; k++) {
        const sim_obj_t *x = &a->splash[k], *y = &b->splash[k];
        if (x->on != y->on || (y->on && (x->y != y->y || x->opa != y->opa))) {
            snprintf(why, n, "splash %d on %d y %d opa %d, want on %d y %d opa %d", k, x->on, x->y, x->opa, y->on, y->y, y->opa);
            return false;
        }
    }
    if (a->bolt != b->bolt || (b->bolt && a->bolt_opa != b->bolt_opa)) {
        snprintf(why, n, "bolt %d opa %d, want %d opa %d", a->bolt, a->bolt_opa, b->bolt, b->bolt_opa);
        return false;
    }
    return true;
}

static void compose(uint16_t *fb, const uint16_t *bg, const uint8_t *m, const wx_rain_layout_t *l, const wx_rain_t *r,
                    const sim_t *v)
{
    memcpy(fb, bg, (size_t)WX_FACE * WX_FACE * 2);
    uint32_t col = r->kind == WX_PRECIP_SNOW ? 0xD0DCE8 : wx_rain_rgb(r->intensity);
    for (int s = 0; s < WX_SHEETS; s++) {
        wx_box_t b = wx_sheet(s)->box;
        int w = b.x1 - b.x0;
        for (int c = 0; c < 2; c++) a8_over(fb, b, m + l->tile[s], w, WX_SHEET_PERIOD, b.x0, v->strip_y[s][c], col, v->strip_opa);
        if (s < WX_DRIPS && v->drip[s].on)
            a8_over(fb, b, m + l->drip[v->drip[s].pose], WX_DRIP_W, WX_DRIP_H, wx_drip_x(s), v->drip[s].y, col, v->drip[s].opa);
        if (s == 2)
            for (int n = 0; n < WX_SPLASHES; n++) {
                wx_splash_at_t at;
                wx_splash_at(n, &at);
                if (v->splash[n].on)
                    a8_over(fb, b, m + l->splash[n], WX_SPLASH_W, WX_SPLASH_H, at.x, v->splash[n].y, col, v->splash[n].opa);
            }
        a8_over(fb, b, m + l->top[s], w, l->ramp_h[s], b.x0, b.y0, 0, 255);
        a8_over(fb, b, m + l->bottom[s], w, l->ramp_h[s], b.x0, b.y1 - l->ramp_h[s], 0, 255);
        if (s == 2 && v->bolt) a8_over(fb, b, m + l->bolt, WX_BOLT_W, WX_BOLT_H, WX_BOLT_X, WX_BOLT_Y, 0xF0D040, v->bolt_opa);
        wx_box_t c = l->cover_box[s];
        int cw = c.x1 - c.x0, ch = c.y1 - c.y0;
        const uint16_t *px = (const uint16_t *)(m + l->cover[s]);
        const uint8_t *al = m + l->cover[s] + (size_t)cw * ch * 2;
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++) {
                int i = (c.y0 + y) * WX_FACE + c.x0 + x;
                fb[i] = blend(fb[i], rgb_of(px[y * cw + x]), al[y * cw + x]);
            }
    }
}

static long box_px(wx_box_t b) { return (long)(b.x1 - b.x0) * (b.y1 - b.y0); }

static int join_areas(wx_box_t *a, int n)
{
    for (bool again = true; again;) {
        again = false;
        for (int i = 0; i < n && !again; i++)
            for (int j = 0; j < n && !again; j++) {
                if (i == j || a[i].x0 >= a[j].x1 || a[j].x0 >= a[i].x1 || a[i].y0 >= a[j].y1 || a[j].y0 >= a[i].y1) continue;
                wx_box_t u = {a[i].x0 < a[j].x0 ? a[i].x0 : a[j].x0, a[i].y0 < a[j].y0 ? a[i].y0 : a[j].y0,
                              a[i].x1 > a[j].x1 ? a[i].x1 : a[j].x1, a[i].y1 > a[j].y1 ? a[i].y1 : a[j].y1};
                if (box_px(u) >= box_px(a[i]) + box_px(a[j])) continue;
                a[i] = u;
                a[j] = a[--n];
                again = true;
            }
    }
    return n;
}

static void test_ops(void)
{
    static const wx_precip_t KIND[3] = {WX_PRECIP_RAIN, WX_PRECIP_STORM, WX_PRECIP_SNOW};
    static const int START[4] = {0, 7, 18, 61};
    int bad = 0;
    for (int c = 0; c < 3; c++)
        for (int in = 1; in <= 3; in++)
            for (int st = 0; st < 4; st++) {
                wx_rain_t r = {.on = true, .kind = KIND[c], .intensity = in};
                sim_t v, want;
                sim_init(&v);
                sim_attach(&v);
                for (int k = START[st]; k < START[st] + 400; k++) {
                    int opa = k - START[st] < 3 ? 85 * (k - START[st] + 1) : 255;
                    if (k == START[st] + 200) sim_attach(&v);
                    sim_frame(&v, &r, k, opa);
                    wx_rain_frame_t f;
                    wx_rain_frame(&r, k, &f);
                    sim_ideal(&want, &f, opa);
                    char why[160];
                    if (!sim_same(&v, &want, why, sizeof why) && bad++ < 5)
                        CHECK(false, "kind %d intensity %d from frame %d: frame %d %s", (int)KIND[c], in, START[st], k, why);
                }
            }
    CHECK(bad == 0, "the replayed LVGL calls draw every frame as wx_rain_frame says (%d frames off)", bad);

    wx_rain_frame_t p = {0}, f = {0};
    for (int i = 0; i < WX_DRIPS; i++) p.drip_pose[i] = -1;
    f.drip_pose[0] = f.drip_pose[1] = 0;
    f.drip_opa[0] = f.drip_opa[1] = 255;
    f.splash_opa[0] = 255;
    wx_rain_ops_t op;
    wx_rain_ops(&p, 255, &f, 255, &op);
    CHECK(op.drip_buf[0] && op.drip_y[0] && op.drip_opa[0] && op.splash_y[0] && op.splash_opa[0] && !op.splash_y[1] && !op.opa,
          "a drip or splash appearing at dy 0 over a hidden one cached at dy 0 gets its buffer, y and opacity");
    p = f;
    f.drip_pose[0] = 1;
    wx_rain_ops(&p, 255, &f, 255, &op);
    CHECK(op.drip_buf[0] && op.drip_y[0] && !op.drip_buf[1] && !op.drip_y[1] && !op.drip_opa[1] && !op.splash_y[0],
          "a pose change sets the y too; nothing else moves");

    memset(&p, 0xFF, sizeof p);
    f = (wx_rain_frame_t){.off = {10, 120, 299}, .drip_pose = {2, -1}, .drip_dy = {14, 0}, .drip_opa = {230, 0},
                          .splash_dy = {-2, 1, 0}, .splash_opa = {153, 0, 179}, .bolt = true};
    wx_rain_ops(&p, -1, &f, 170, &op);
    wx_splash_at_t at0, at2;
    wx_splash_at(0, &at0);
    wx_splash_at(2, &at2);
    CHECK(op.opa && op.layer_opa == 170, "after an attach: every layer gets the opacity");
    for (int s = 0; s < WX_SHEETS; s++)
        CHECK(op.strip_y[s] && op.strip_top[s][0] == f.off[s] - 300 && op.strip_top[s][1] == f.off[s],
              "sheet %d: strips at %d and %d", s, op.strip_top[s][0], op.strip_top[s][1]);
    CHECK(op.drip_on[0] && op.drip_buf[0] && op.drip_pose[0] == 2 && op.drip_top[0] == WX_DRIP_Y + 14 - wx_sheet(0)->box.y0 &&
              op.drip_alpha[0] == 230 * 170 / 255,
          "drip 0: pose 2, y %d, alpha %d", op.drip_top[0], op.drip_alpha[0]);
    CHECK(!op.drip_on[1] && !op.drip_buf[1] && !op.drip_y[1] && !op.drip_opa[1], "drip 1 hidden, untouched");
    CHECK(op.splash_on[0] && op.splash_top[0] == at0.y - 2 - wx_sheet(2)->box.y0 && op.splash_alpha[0] == 153 * 170 / 255,
          "splash 0: y %d, alpha %d", op.splash_top[0], op.splash_alpha[0]);
    CHECK(op.splash_on[2] && op.splash_top[2] == at2.y - wx_sheet(2)->box.y0 && op.splash_alpha[2] == 179 * 170 / 255,
          "splash 2: y %d, alpha %d", op.splash_top[2], op.splash_alpha[2]);
    CHECK(!op.splash_on[1] && !op.splash_y[1] && !op.splash_opa[1] && op.bolt_on, "splash 1 hidden; bolt lit");
    p = f;
    f.splash_dy[0] = 0;
    f.splash_opa[0] = 255;
    wx_rain_ops(&p, 170, &f, 255, &op);
    CHECK(op.opa && op.layer_opa == 255 && op.splash_y[0] && op.splash_top[0] == at0.y - wx_sheet(2)->box.y0 &&
              op.splash_alpha[0] == 255 && op.drip_opa[0] && op.drip_alpha[0] == 230 && op.splash_opa[2] &&
              op.splash_alpha[2] == 179 && !op.drip_y[0] && !op.strip_y[0],
          "a fade step: every visible object's alpha scales, only the moved splash gets a y");
}

static void dump(const char *dir, const char *name, const uint16_t *fb)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *fp = fopen(path, "wb");
    if (!fp) return;
    fprintf(fp, "P6 %d %d 255\n", WX_FACE, WX_FACE);
    for (int i = 0; i < WX_FACE * WX_FACE; i++) {
        uint32_t c = rgb_of(fb[i]);
        unsigned char p[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c};
        fwrite(p, 1, 3, fp);
    }
    fclose(fp);
}

static void test_bake_and_areas(void)
{
    static uint16_t bg[WX_FACE * WX_FACE], fb[WX_FACE * WX_FACE], prev[WX_FACE * WX_FACE], ref[WX_FACE * WX_FACE];
    static uint16_t lut[WX_RING_LUT];
    static wx_ring_t ring;
    size_t still = (size_t)wx_clouds_max_px(false) * 3;
    uint8_t *m = calloc(1, still), *hero = calloc(1, WX_HERO_BOX * WX_HERO_BOX * 3), *drift = calloc(1, (size_t)wx_clouds_max_px(true) * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .still = m, .drift = drift};
    wx_rain_layout_t l;
    wx_rain_layout(&l);
    const char *dir = getenv("CINDER_RAIN_PPM");
    static const struct {
        const char *name;
        wx_precip_t kind;
        int in;
        bool pop;
    } CASES[] = {{"rain-light", WX_PRECIP_RAIN, 1, true}, {"rain-moderate", WX_PRECIP_RAIN, 2, true}, {"rain-heavy", WX_PRECIP_RAIN, 3, true},
                 {"snow", WX_PRECIP_SNOW, 2, true},       {"storm", WX_PRECIP_STORM, 3, false}};
    for (size_t c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
        wx_temp_t t = {0};
        t.rain = (wx_rain_t){.on = true, .kind = CASES[c].kind, .intensity = CASES[c].in, .has_pop = CASES[c].pop, .pop = 90};
        wx_bake_out_t o = {.hero = {1, 1, 2, 2}, .still = {1, 1, 2, 2}, .drift = {1, 1, 2, 2}};
        clock_t c0 = clock();
        wx_bake(&t, WX_BAKE_FULL, &b, &o);
        double ms = (double)(clock() - c0) * 1000 / CLOCKS_PER_SEC;
        printf("rain: full bake %s %.2f ms on the host\n", CASES[c].name, ms);
        CHECK(o.hero.x1 == 0 && o.still.x1 == 0 && o.drift.x1 == 0, "%s: no hero or cloud sprites", CASES[c].name);
        float ex, ey;
        wx_gauge_xy(1, &ex, &ey);
        uint16_t end = bg[(int)ey * WX_FACE + (int)ex - 4];
        CHECK(CASES[c].pop ? end == wx_rgb565(wx_rain_rgb(CASES[c].in)) : end == wx_rgb565(0x262626),
              "%s: ring end %04X", CASES[c].name, end);
        CHECK(bg[260 * WX_FACE + 233] == 0 && bg[350 * WX_FACE + 233] == wx_rgb565(0xFF8A3D) &&
                  bg[150 * WX_FACE + 233] == 0 && bg[95 * WX_FACE + 233] == wx_rgb565(0xFF8A3D),
              "%s: handle passes behind the text, black canopy, orange rim", CASES[c].name);
        for (int s = 0; s < WX_SHEETS; s++) {
            wx_box_t bx = wx_sheet(s)->box, cv = l.cover_box[s];
            int cw = cv.x1 - cv.x0, outside = 0, off = 0;
            const uint16_t *px = (const uint16_t *)(m + l.cover[s]);
            const uint8_t *al = m + l.cover[s] + (size_t)cw * (cv.y1 - cv.y0) * 2;
            for (int y = bx.y0; y < bx.y1; y++)
                for (int x = bx.x0; x < bx.x1; x++) {
                    bool in = x >= cv.x0 && x < cv.x1 && y >= cv.y0 && y < cv.y1;
                    uint16_t g = bg[y * WX_FACE + x];
                    if (!in) {
                        outside += g != 0;
                        continue;
                    }
                    int i = (y - cv.y0) * cw + x - cv.x0;
                    uint16_t over = blend(0, rgb_of(px[i]), al[i]);
                    uint32_t a = rgb_of(over), e = rgb_of(g);
                    for (int sh = 0; sh < 24; sh += 8)
                        if (abs((int)((a >> sh) & 255) - (int)((e >> sh) & 255)) > 12) off++;
                }
            CHECK(outside == 0, "%s: sheet %d: no umbrella ink outside its cover (%d px)", CASES[c].name, s, outside);
            CHECK(off == 0, "%s: sheet %d: the cover over black equals the background (%d channels off)", CASES[c].name, s, off);
        }
        wx_rain_t *r = &t.rain;
        int cycle = 1;
        for (int s = 0; s < WX_SHEETS; s++) {
            int fr = wx_sheet_frames(r, s), a = cycle, g = fr;
            while (g) {
                int tmp = a % g;
                a = g;
                g = tmp;
            }
            cycle = cycle / a * fr;
        }
        int frames = cycle * 4 > 2400 ? 2400 : cycle * 4;
        wx_rain_frame_t f;
        sim_t v, want;
        sim_init(&v);
        sim_attach(&v);
        int max_areas = 0, max_px = 0, max_changed = 0, off = 0;
        long sum_changed = 0;
        for (int k = 0; k <= frames; k++) {
            sim_frame(&v, r, k, 255);
            compose(fb, bg, m, &l, r, &v);
            wx_rain_frame(r, k, &f);
            sim_ideal(&want, &f, 255);
            compose(ref, bg, m, &l, r, &want);
            if (memcmp(fb, ref, sizeof ref) && off++ < 3) CHECK(false, "%s frame %d: the replayed calls draw another picture", CASES[c].name, k);
            if (k > 0) {
                wx_box_t area[64];
                memcpy(area, v.inv, sizeof area);
                int areas = join_areas(area, v.n_inv), px = 0, changed = 0, stray = 0;
                for (int a = 0; a < areas; a++) px += (int)box_px(area[a]);
                for (int i = 0; i < WX_FACE * WX_FACE; i++) {
                    if (fb[i] == prev[i]) continue;
                    changed++;
                    int x = i % WX_FACE, y = i / WX_FACE;
                    bool in = false;
                    for (int a = 0; a < v.n_inv; a++)
                        in |= x >= v.inv[a].x0 && x < v.inv[a].x1 && y >= v.inv[a].y0 && y < v.inv[a].y1;
                    stray += !in;
                }
                CHECK(stray == 0, "%s frame %d: %d changed px outside the invalidated areas", CASES[c].name, k, stray);
                if (areas > max_areas) max_areas = areas;
                if (px > max_px) max_px = px;
                if (changed > max_changed) max_changed = changed;
                sum_changed += changed;
            }
            if (dir && (k == 4 || k == 12)) {
                char name[64];
                snprintf(name, sizeof name, "%s-%d", CASES[c].name, k);
                dump(dir, name, fb);
            }
            memcpy(prev, fb, sizeof prev);
        }
        CHECK(off == 0, "%s: %d frames differ from wx_rain_frame's picture", CASES[c].name, off);
        CHECK(max_areas <= 3, "%s: at most 3 areas a frame, got %d", CASES[c].name, max_areas);
        printf("rain: %s: %d frames: %d areas/frame, %d px redrawn/frame, changed px max %d avg %ld\n", CASES[c].name, frames,
               max_areas, max_px, max_changed, sum_changed / frames);
    }
    wx_temp_t t = {0};
    t.rain = (wx_rain_t){.on = true, .kind = WX_PRECIP_RAIN, .intensity = 2, .has_pop = false};
    wx_bake_out_t o;
    wx_bake(&t, WX_BAKE_FULL, &b, &o);
    static uint16_t keep[WX_FACE * WX_FACE];
    memcpy(keep, bg, sizeof keep);
    uint8_t *mk = malloc(still);
    memcpy(mk, m, still);
    t.rain.has_pop = true;
    wx_bake(&t, WX_BAKE_RING, &b, &o);
    CHECK(!memcmp(bg, keep, sizeof keep) && !memcmp(m, mk, still), "a ring bake leaves the background and the layers");
    wx_ring_blit(&ring, bg);
    static uint16_t full[WX_FACE * WX_FACE];
    memcpy(keep, bg, sizeof keep);
    wx_bake(&t, WX_BAKE_FULL, &b, &o);
    memcpy(full, bg, sizeof full);
    CHECK(!memcmp(keep, full, sizeof full), "ring blit = full bake");
    free(mk);
    free(m);
    free(hero);
    free(drift);
}

static void test_ramps(void)
{
    static uint8_t a[166 * 40];
    wx_rain_layout_t l;
    wx_rain_layout(&l);
    for (int s = 0; s < WX_SHEETS; s++) {
        const wx_sheet_t *sh = wx_sheet(s);
        int w = sh->box.x1 - sh->box.x0, h = sh->box.y1 - sh->box.y0, rh = l.ramp_h[s];
        CHECK(rh == (int)ceilf(0.16f * h), "sheet %d ramp %d rows", s, rh);
        wx_paint_ramp(a, s, false, rh);
        CHECK(a[0] > 240 && a[(rh - 1) * w] < 20 && a[w - 1] == a[0], "sheet %d top ramp: dark at the edge, clear inside", s);
        for (int k = 1; k < rh; k++) CHECK(a[k * w] <= a[(k - 1) * w], "top ramp decreasing");
        wx_paint_ramp(a, s, true, rh);
        CHECK(a[(rh - 1) * w] > 240 && a[0] < 20, "sheet %d bottom ramp mirrored", s);
    }
}

int main(void)
{
    test_select();
    test_bake_kind();
    test_sheets();
    test_tiles();
    test_splash_drip_bolt();
    test_ops();
    test_boxes();
    test_ramps();
    test_bake_and_areas();
    if (failures) {
        printf("rain: %d failure(s)\n", failures);
        return 1;
    }
    printf("rain: all tests passed\n");
    return 0;
}
