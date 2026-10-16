#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_faces.h"
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
                  .obs_age_s = 300, .has_pop = true, .pop = 35};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "%s", cond);
    snprintf(o.code, sizeof o.code, "%s", code);
    return o;
}

static wx_obs_t wet(void)
{
    wx_obs_t o = obs("rain", "63");
    o.has_now_rain = o.has_rain = o.has_utc_offset = true;
    o.now_rain = 2;
    o.utc_offset = 3600;
    o.rain_until = OBSERVED + 3600;
    o.rain_n = 1;
    o.rain_spell[0] = (wx_spell_t){OBSERVED - 900, OBSERVED + 3600, 2, 0};
    return o;
}

static void test_step(void)
{
    CHECK(WXF_N == 7 && WXF_TEMP == 0 && WXF_RAIN == 1 && WXF_CAST == 2 && WXF_WIND == 3 && WXF_DAYS == 4 && WXF_SUN == 5 && WXF_AIR == 6,
          "the list: Temperature, Rain now, Rain forecast, Wind, Next days, Sun, Air");
    CHECK(wx_face_step(WXF_TEMP, 1) == WXF_RAIN && wx_face_step(WXF_RAIN, 1) == WXF_CAST && wx_face_step(WXF_CAST, 1) == WXF_WIND &&
              wx_face_step(WXF_WIND, 1) == WXF_DAYS && wx_face_step(WXF_DAYS, 1) == WXF_SUN && wx_face_step(WXF_SUN, 1) == WXF_AIR &&
              wx_face_step(WXF_AIR, 1) == WXF_TEMP,
          "next wraps");
    CHECK(wx_face_step(WXF_TEMP, -1) == WXF_AIR && wx_face_step(WXF_AIR, -1) == WXF_SUN && wx_face_step(WXF_SUN, -1) == WXF_DAYS &&
              wx_face_step(WXF_DAYS, -1) == WXF_WIND && wx_face_step(WXF_WIND, -1) == WXF_CAST && wx_face_step(WXF_CAST, -1) == WXF_RAIN &&
              wx_face_step(WXF_RAIN, -1) == WXF_TEMP,
          "previous wraps");
    CHECK(wx_face_step(WXF_TEMP, 2) == WXF_CAST && wx_face_step(WXF_RAIN, -5) == WXF_WIND && wx_face_step(WXF_CAST, 4) == WXF_AIR &&
              wx_face_step(WXF_WIND, -6) == WXF_DAYS && wx_face_step(WXF_DAYS, 2) == WXF_AIR && wx_face_step(WXF_SUN, -7) == WXF_SUN,
          "several detents in one frame");
    CHECK(wx_face_step(WXF_RAIN, 0) == WXF_RAIN, "no detent: same face");
    CHECK(wx_face_step(WXF_TEMP, -2147483647) == WXF_AIR && wx_face_step(WXF_TEMP, 2147483647) == WXF_RAIN, "a huge count does not overflow");
    CHECK(wx_face_step(7, 1) == WXF_RAIN && wx_face_step(-1, 0) == WXF_TEMP, "a face outside the list steps from the first");
}

static void test_turn(void)
{
    wx_faces_t f = {.pick = WXF_RAIN};
    wx_faces_open(&f);
    CHECK(f.pick == WXF_AUTO, "a page open forgets the pick: auto face");
    CHECK(!wx_faces_turn(&f, WXF_TEMP, WXF_TEMP, 0) && f.pick == WXF_AUTO, "no detent: still auto");
    CHECK(wx_faces_turn(&f, WXF_TEMP, WXF_TEMP, 1) && f.pick == WXF_RAIN, "a turn from Temperature picks Rain now");
    CHECK(wx_faces_turn(&f, WXF_RAIN, WXF_RAIN, 1) && f.pick == WXF_CAST, "then Rain forecast");
    CHECK(wx_faces_turn(&f, WXF_CAST, WXF_CAST, 1) && f.pick == WXF_WIND, "then Wind");
    CHECK(wx_faces_turn(&f, WXF_WIND, WXF_WIND, 1) && f.pick == WXF_DAYS, "then Next days");
    CHECK(wx_faces_turn(&f, WXF_DAYS, WXF_DAYS, 1) && f.pick == WXF_SUN, "then Sun");
    CHECK(wx_faces_turn(&f, WXF_SUN, WXF_SUN, 1) && f.pick == WXF_AIR, "then Air");
    CHECK(wx_faces_turn(&f, WXF_AIR, WXF_AIR, -6) && f.pick == WXF_TEMP, "and back");
    CHECK(!wx_faces_turn(&f, WXF_TEMP, WXF_TEMP, 7) && f.pick == WXF_TEMP, "a full turn round the list keeps the face, picked");
    wx_faces_open(&f);
    CHECK(!wx_faces_turn(&f, WXF_RAIN, WXF_RAIN, 7) && f.pick == WXF_RAIN,
          "a seven-detent flick from auto shows no change but pins the face on screen: it stops following the weather");
    wx_faces_open(&f);
    CHECK(f.pick == WXF_AUTO, "the next open is auto again");
}

static void test_turn_base(void)
{
    wx_faces_t f;
    wx_faces_open(&f);
    CHECK(wx_faces_base(&f, WXF_TEMP, WXF_RAIN) == WXF_TEMP, "auto: the face on screen, not the one the weather asks for");
    CHECK(wx_faces_turn(&f, WXF_TEMP, WXF_RAIN, 1) && f.pick == WXF_RAIN,
          "a turn while Temperature fades out towards Rain now steps from Temperature: Rain now");
    wx_faces_open(&f);
    CHECK(wx_faces_base(&f, -1, WXF_RAIN) == WXF_RAIN, "nothing on screen (black while baking): the face the page asks for");
    CHECK(wx_faces_turn(&f, -1, WXF_TEMP, 1) && f.pick == WXF_RAIN, "no buffers yet: one turn from the resolved face");
    CHECK(wx_faces_turn(&f, -1, WXF_TEMP, 1) && f.pick == WXF_CAST,
          "a second turn before the buffers come steps from the pick, not from a stale resolved face");
    CHECK(wx_faces_turn(&f, WXF_TEMP, WXF_TEMP, 1) && f.pick == WXF_WIND, "the pick, not the old face still on screen");
    CHECK(wx_faces_turn(&f, WXF_TEMP, WXF_RAIN, 1) && f.pick == WXF_DAYS, "two quick detents while the first bake runs move two faces");
    wx_faces_open(&f);
    CHECK(wx_faces_base(&f, -1, -1) == WXF_TEMP, "nothing known (just reopened): the first face");
    CHECK(wx_faces_base(&f, 9, -1) == WXF_TEMP, "ids outside the list are unknown");
}

static void test_pick(void)
{
    wx_obs_t dry = obs("clear", "0"), rain = wet();
    wx_temp_t t;

    wx_temp_face(&dry, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP && !t.rain.on && t.have && t.value == 12, "dry, auto: Temperature");
    wx_temp_face(&rain, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_RAIN && t.rain.on && !t.rain.dry && t.rain.intensity == 2, "wet, auto: Rain now");
    wx_temp_from(&rain, &t);
    CHECK(t.face == WXF_RAIN, "wx_temp_from is the auto face");
    wx_temp_face(&rain, NULL, 9, &t);
    CHECK(t.face == WXF_RAIN && t.rain.on, "an id outside the list: auto");

    wx_temp_face(&rain, NULL, WXF_TEMP, &t);
    CHECK(t.face == WXF_TEMP && !t.rain.on && t.have && t.value == 12 && t.lo == WX_SCALE_LO, "wet, Temperature picked");
    CHECK(!t.rain.has_pop && !t.rain.has_until && !t.rain.sub[0], "it carries no rain data");
    CHECK(!strcmp(t.sub, "Rain"), "its sky word says rain: '%s'", t.sub);

    wx_temp_face(&dry, NULL, WXF_RAIN, &t);
    CHECK(t.face == WXF_RAIN && t.rain.on && t.rain.dry, "dry, Rain now picked: the dry face");
    CHECK(t.rain.kind == WX_PRECIP_RAIN && t.rain.intensity == 1, "light colours, no drops");
    CHECK(t.rain.has_pop && t.rain.pop == 35 && !t.rain.has_until, "today's chance");
    CHECK(!strcmp(t.rain.sub, "Dry now"), "sub-line: '%s'", t.rain.sub);
    dry.has_pop = false;
    wx_temp_face(&dry, NULL, WXF_RAIN, &t);
    CHECK(t.rain.dry && !t.rain.has_pop, "no chance known: --%%");
    dry.has_pop = true;
    dry.pop = 140;
    wx_temp_face(&dry, NULL, WXF_RAIN, &t);
    CHECK(t.rain.pop == 100, "chance clamped");

    wx_obs_t old = obs("clear", "0");
    old.age_s = 7200;
    wx_temp_face(&old, NULL, WXF_RAIN, &t);
    CHECK(t.rain.on && t.rain.dry && !t.rain.has_pop && !t.rain.sub[0], "old data: grey --%%, no sub-line");
    old.age_s = 0;
    old.stale = true;
    wx_temp_face(&old, NULL, WXF_RAIN, &t);
    CHECK(t.rain.dry && !t.rain.has_pop, "stale data: no chance");
    wx_temp_face(NULL, NULL, WXF_RAIN, &t);
    CHECK(t.face == WXF_RAIN && t.rain.on && t.rain.dry && !t.have && !t.rain.has_pop, "no data: the dry face, grey");

    wx_temp_face(&rain, NULL, WXF_RAIN, &t);
    CHECK(t.rain.on && !t.rain.dry && t.rain.intensity == 2 && !strcmp(t.rain.sub, "until 14:00"),
          "wet, Rain now picked: the wet face as auto shows it: '%s'", t.rain.sub);
    wx_obs_t storm = obs("thunderstorm", "95");
    wx_temp_face(&storm, NULL, WXF_RAIN, &t);
    CHECK(t.rain.on && !t.rain.dry && t.rain.kind == WX_PRECIP_STORM, "a dry storm keeps the storm face");
}

static void test_bake_kind(void)
{
    wx_obs_t dry = obs("clear", "0"), rain = wet();
    wx_temp_t tt, tr, rd, rw, x;
    wx_temp_face(&rain, NULL, WXF_TEMP, &tt);
    wx_temp_face(&dry, NULL, WXF_RAIN, &rd);
    wx_temp_face(&rain, NULL, WXF_RAIN, &rw);
    wx_temp_face(&dry, NULL, WXF_TEMP, &tr);
    CHECK(wx_temp_bake_kind(&rd, &tr) == WX_BAKE_FULL && wx_temp_bake_kind(&tr, &rd) == WX_BAKE_FULL,
          "Temperature <-> Rain now: a full scene");
    CHECK(wx_temp_bake_kind(&rw, &tt) == WX_BAKE_FULL, "wet: Temperature -> Rain now is a full scene");
    CHECK(wx_temp_bake_kind(&rw, &rd) == WX_BAKE_FULL && wx_temp_bake_kind(&rd, &rw) == WX_BAKE_FULL,
          "rain starts or stops on the picked Rain now face: a full scene (drops come or go)");
    wx_obs_t light = wet();
    light.now_rain = 1;
    light.rain_spell[0].intensity = 1;
    wx_temp_face(&light, NULL, WXF_RAIN, &x);
    CHECK(x.rain.intensity == rd.rain.intensity && x.rain.kind == rd.rain.kind && x.rain.has_pop == rd.rain.has_pop,
          "light rain: same kind and intensity as the dry face");
    CHECK(wx_temp_bake_kind(&x, &rd) == WX_BAKE_FULL && wx_temp_bake_kind(&rd, &x) == WX_BAKE_FULL,
          "light rain starts or stops on the dry face: still a full scene");
    x = tt;
    x.face = WXF_RAIN;
    CHECK(wx_temp_bake_kind(&x, &tt) == WX_BAKE_FULL, "the face id alone is a full scene (the dots)");
    dry.pop = 80;
    wx_temp_face(&dry, &rd, WXF_RAIN, &x);
    CHECK(wx_temp_bake_kind(&x, &rd) == 0, "a new chance on the dry face only moves the marker");
    dry.has_pop = false;
    wx_temp_face(&dry, &rd, WXF_RAIN, &x);
    CHECK(wx_temp_bake_kind(&x, &rd) == WX_BAKE_RING, "the chance going is a ring bake");

    wx_rain_t heavy = rw.rain, r = rd.rain;
    heavy.intensity = 3;
    wx_rain_keep(&r, &heavy);
    CHECK(r.intensity == 1 && r.dry, "the dry face never takes a wet face's intensity");
    r = rw.rain;
    heavy.dry = true;
    wx_rain_keep(&r, &heavy);
    CHECK(r.intensity == 2, "nor the reverse");
}

static uint32_t rgb_at(const uint16_t *bg, float x, float y)
{
    uint16_t c = bg[(int)y * WX_FACE + (int)x];
    return ((uint32_t)(c >> 11) << 19) | ((uint32_t)((c >> 5) & 63) << 10) | ((uint32_t)(c & 31) << 3);
}

static void test_dots(void)
{
    static const float CANVAS[6][2] = {{193.28f, 435.13f}, {209.07f, 437.6f}, {225.01f, 438.84f},
                                       {240.99f, 438.84f}, {256.93f, 437.6f}, {272.72f, 435.13f}};
    for (int k = 0; k < 6; k++) {
        float x, y;
        wx_face_dot_xy(k, 6, &x, &y);
        CHECK(fabsf(x - CANVAS[k][0]) < 0.05f && fabsf(y - CANVAS[k][1]) < 0.05f, "dot %d of 6 at (%.2f, %.2f) as the canvas", k,
              x, y);
    }
    float x0, y0, x1, y1;
    wx_face_dot_xy(0, 2, &x0, &y0);
    wx_face_dot_xy(1, 2, &x1, &y1);
    CHECK(fabsf(x0 + x1 - 2 * WX_FACE_C) < 0.01f && fabsf(x1 - x0 - 16) < 0.05f && fabsf(y0 - y1) < 0.01f,
          "two dots centred, 16 px apart");

    static uint16_t bg[WX_FACE * WX_FACE], lut[WX_RING_LUT];
    static uint8_t hero[WX_HERO_BOX * WX_HERO_BOX * 3];
    static wx_ring_t ring;
    uint8_t *scene = malloc((size_t)wx_scene_px() * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .scene = scene};
    wx_obs_t dry = obs("clear", "0"), rain = wet();
    struct {
        const wx_obs_t *o;
        int face;
        const char *what;
    } C[] = {{&dry, WXF_TEMP, "Temperature"}, {&dry, WXF_RAIN, "dry Rain now"}, {&rain, WXF_RAIN, "wet Rain now"},
             {&rain, WXF_TEMP, "Temperature while wet"}, {NULL, WXF_TEMP, "no data"}, {&rain, WXF_CAST, "wet Rain forecast"},
             {&dry, WXF_CAST, "dry Rain forecast"}, {NULL, WXF_CAST, "Rain forecast, no data"}, {&dry, WXF_WIND, "Wind"},
             {NULL, WXF_WIND, "Wind, no data"}, {&dry, WXF_DAYS, "Next days, no days"}, {NULL, WXF_DAYS, "Next days, no data"}};
    for (int c = 0; c < (int)(sizeof C / sizeof C[0]); c++) {
        wx_temp_t t;
        wx_bake_out_t o;
        wx_temp_face(C[c].o, NULL, C[c].face, &t);
        wx_bake(&t, WX_BAKE_FULL, &b, &o);
        for (int k = 0; k < WXF_N; k++) {
            float x, y;
            wx_face_dot_xy(k, WXF_N, &x, &y);
            uint32_t want = k == C[c].face ? 0xF8FCF8 : 0x484848;
            CHECK(rgb_at(bg, x, y) == want, "%s: dot %d %06X, want %06X", C[c].what, k, (unsigned)rgb_at(bg, x, y),
                  (unsigned)want);
            CHECK(rgb_at(bg, x + WX_DOT_R + 1, y) == 0 && rgb_at(bg, x, y + WX_DOT_R + 1) == 0, "%s: dot %d r 4 on black",
                  C[c].what, k);
        }
    }
    free(scene);
}

static int s_yields;
static double s_since, s_gap;

static double cpu_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static void count_yield(void)
{
    double now = cpu_ms();
    if (now - s_since > s_gap) s_gap = now - s_since;
    s_since = now;
    s_yields++;
}

static wx_obs_t moon(int cloud)
{
    wx_obs_t o = obs("clear", "0");
    o.night = true;
    o.has_moon = true;
    o.moon_up = 1;
    o.moon_phase = 80;
    o.moon_illum = 30;
    o.has_cloud = true;
    o.cloud = cloud;
    return o;
}

static wx_obs_t days(void)
{
    wx_obs_t o = obs("clear", "0");
    static const int FACE[3] = {WX_PARTLY_CLOUDY, WX_RAIN, WX_CLEAR_DAY}, LO[3] = {8, 9, 6}, HI[3] = {15, 13, 17}, POP[3] = {10, 70, 0};
    o.has_utc_offset = true;
    o.utc_offset = 3600;
    o.day_n = 3;
    for (int i = 0; i < 3; i++)
        o.day[i] = (wx_day_t){.t = OBSERVED + (i + 1) * 86400LL, .end = OBSERVED + (i + 2) * 86400LL, .face = (signed char)FACE[i],
                              .pop = (signed char)POP[i], .min_c = (short)LO[i], .max_c = (short)HI[i]};
    return o;
}

static void bake(const wx_temp_t *t, int k, uint16_t *bg, uint8_t *hero, uint8_t *scene, size_t sn,
                 uint16_t *lut, wx_ring_t *ring, wx_bake_out_t *o)
{
    memset(bg, 0x5A, (size_t)WX_FACE * WX_FACE * 2);
    memset(hero, 0x5A, (size_t)WX_HERO_BOX * WX_HERO_BOX * 3);
    memset(scene, 0x5A, sn);
    memset(ring, 0, sizeof *ring);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = ring, .hero = hero, .scene = scene, .yield = k ? count_yield : NULL};
    s_yields = 0;
    s_gap = 0;
    s_since = cpu_ms();
    wx_bake(t, WX_BAKE_FULL, &b, o);
    if (k) count_yield();
}

static void test_bake_yield(void)
{
    static uint16_t bg[2][WX_FACE * WX_FACE], lut[2][WX_RING_LUT];
    static uint8_t hero[2][WX_HERO_BOX * WX_HERO_BOX * 3];
    static wx_ring_t ring[2];
    size_t sn = (size_t)wx_scene_px() * 3;
    uint8_t *scene[2] = {calloc(1, sn), calloc(1, sn)};
    wx_obs_t dry = obs("clear", "0"), rain = wet(), poor = obs("clear", "0"), night = moon(0), dim = moon(80), next = days();
    rain.has_now_rain = true;
    poor.has_aqi = true;
    poor.aqi = 120;
    struct {
        const wx_obs_t *o;
        int face;
        const char *what;
    } C[] = {{&dry, WXF_TEMP, "Temperature"}, {&rain, WXF_TEMP, "Temperature while wet"}, {&night, WXF_TEMP, "Temperature, moon"},
             {&dim, WXF_TEMP, "Temperature, small moon"}, {&dry, WXF_RAIN, "dry Rain now"}, {&rain, WXF_RAIN, "wet Rain now"},
             {&rain, WXF_CAST, "wet Rain forecast"}, {&dry, WXF_CAST, "dry Rain forecast"}, {&dry, WXF_WIND, "Wind"},
             {&dry, WXF_DAYS, "Next days, no days"}, {&next, WXF_DAYS, "Next days"}, {&dry, WXF_SUN, "Sun"},
             {&dry, WXF_AIR, "Air, no data"}, {&poor, WXF_AIR, "Air, wilted"}};
    double worst = 0;
    for (int c = 0; c < (int)(sizeof C / sizeof C[0]); c++) {
        wx_temp_t t;
        wx_temp_face(C[c].o, NULL, C[c].face, &t);
        if (C[c].o == &night) CHECK(t.hero == WX_HERO_MOON, "%s: the moon hero", C[c].what);
        if (C[c].o == &dim) CHECK(t.hero == WX_HERO_MOON_SMALL, "%s: the small moon hero", C[c].what);
        if (C[c].o == &next) CHECK(t.days.on && t.days.n == 3, "%s: three rows (%d)", C[c].what, t.days.n);
        wx_bake_out_t o[2];
        double plain = INFINITY, gap = INFINITY, share = INFINITY, last = 0;
        int calls = 0;
        for (int run = 0; run < 10; run++) {
            int k = run & 1;
            double t0 = cpu_ms();
            bake(&t, k, bg[k], hero[k], scene[k], sn, lut[k], &ring[k], &o[k]);
            if (!k) {
                last = cpu_ms() - t0;
                continue;
            }
            calls = s_yields - 1;
            if (last > 0 && s_gap / last < share) share = s_gap / last, gap = s_gap, plain = last;
        }
        CHECK(!memcmp(bg[0], bg[1], sizeof bg[0]) && !memcmp(hero[0], hero[1], sizeof hero[0]) && !memcmp(scene[0], scene[1], sn) &&
                  !memcmp(&o[0], &o[1], sizeof o[0]),
              "%s: a bake with the yield hook paints what one without it does", C[c].what);
        if (share > worst) worst = share;
        printf("bake yield %s: %d calls, %.2f ms CPU without the hook, longest stretch without a call %.3f ms (%.0f %%, best of 5 pairs)\n",
               C[c].what, calls, plain, gap, 100 * share);
        CHECK(calls > 400, "%s: %d yield calls, want one before every painted row", C[c].what, calls);
        CHECK(share <= 0.12, "%s: %.0f %% of the bake ran without a yield call, want 12 %% at most", C[c].what, 100 * share);
    }
    printf("bake yield: longest stretch %.0f %% of its bake\n", 100 * worst);
    free(scene[0]);
    free(scene[1]);
}

int main(void)
{
    test_step();
    test_turn();
    test_turn_base();
    test_pick();
    test_bake_kind();
    test_dots();
    test_bake_yield();
    if (failures) {
        printf("faces: %d failures\n", failures);
        return 1;
    }
    printf("faces: all tests passed\n");
    return 0;
}
