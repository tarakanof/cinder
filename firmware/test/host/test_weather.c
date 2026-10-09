#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bot_raster.h"
#include "weather_face.h"
#include "weather_scene.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct {
    const char *provider, *condition, *code;
    int night;
    wx_face_t face;
    wx_intensity_t in;
    int rime;
} map_case_t;

static void test_mapping_table(void)
{
    static const map_case_t C[] = {
        {"open-meteo", "clear", "0", 0, WX_CLEAR_DAY, WX_INT_NONE, 0},
        {"open-meteo", "clear", "0", 1, WX_CLEAR_NIGHT, WX_INT_NONE, 0},
        {"open-meteo", "clear", "0", -1, WX_CLEAR_DAY, WX_INT_NONE, 0},
        {"open-meteo", "clouds", "1", 0, WX_PARTLY_CLOUDY, WX_INT_NONE, 0},
        {"open-meteo", "clouds", "2", 1, WX_PARTLY_CLOUDY, WX_INT_NONE, 0},
        {"open-meteo", "clouds", "3", 0, WX_OVERCAST, WX_INT_NONE, 0},
        {"open-meteo", "clouds", "3", 1, WX_OVERCAST, WX_INT_NONE, 0},
        {"open-meteo", "fog", "45", 0, WX_FOG, WX_INT_NONE, 0},
        {"open-meteo", "fog", "48", 0, WX_FOG, WX_INT_NONE, 1},
        {"open-meteo", "rain", "51", 0, WX_RAIN, WX_INT_LIGHT, 0},
        {"open-meteo", "rain", "57", 0, WX_RAIN, WX_INT_LIGHT, 0},
        {"open-meteo", "rain", "61", 0, WX_RAIN, WX_INT_MODERATE, 0},
        {"open-meteo", "rain", "63", 0, WX_RAIN, WX_INT_MODERATE, 0},
        {"open-meteo", "rain", "65", 0, WX_RAIN, WX_INT_HEAVY, 0},
        {"open-meteo", "rain", "67", 0, WX_RAIN, WX_INT_HEAVY, 0},
        {"open-meteo", "rain", "80", 0, WX_RAIN, WX_INT_MODERATE, 0},
        {"open-meteo", "rain", "82", 0, WX_RAIN, WX_INT_HEAVY, 0},
        {"open-meteo", "snow", "71", 0, WX_SNOW, WX_INT_LIGHT, 0},
        {"open-meteo", "snow", "73", 0, WX_SNOW, WX_INT_MODERATE, 0},
        {"open-meteo", "snow", "75", 0, WX_SNOW, WX_INT_HEAVY, 0},
        {"open-meteo", "snow", "77", 0, WX_SNOW, WX_INT_LIGHT, 0},
        {"open-meteo", "snow", "86", 0, WX_SNOW, WX_INT_HEAVY, 0},
        {"open-meteo", "storm", "95", 0, WX_STORM, WX_INT_MODERATE, 0},
        {"open-meteo", "storm", "99", 1, WX_STORM, WX_INT_HEAVY, 0},
        {"open-meteo", "clouds", "42", 0, WX_OVERCAST, WX_INT_NONE, 0},
        {"open-meteo", "rain", "", 0, WX_RAIN, WX_INT_MODERATE, 0},
        {"open-meteo", "clear", NULL, 1, WX_CLEAR_NIGHT, WX_INT_NONE, 0},
        {"", "snow", "", 0, WX_SNOW, WX_INT_MODERATE, 0},
        {"", "storm", "", 0, WX_STORM, WX_INT_MODERATE, 0},
        {"", "fog", "", 0, WX_FOG, WX_INT_NONE, 0},
        {"", "weird", "", 0, WX_OVERCAST, WX_INT_NONE, 0},
        {"met-no", "clear", "clearsky_day", -1, WX_CLEAR_DAY, WX_INT_NONE, 0},
        {"met-no", "clear", "clearsky_night", -1, WX_CLEAR_NIGHT, WX_INT_NONE, 0},
        {"met-no", "clear", "clearsky_night", 0, WX_CLEAR_DAY, WX_INT_NONE, 0},
        {"met-no", "clear", "fair_polartwilight", -1, WX_CLEAR_NIGHT, WX_INT_NONE, 0},
        {"met-no", "clouds", "partlycloudy_day", -1, WX_PARTLY_CLOUDY, WX_INT_NONE, 0},
        {"met-no", "clouds", "cloudy", -1, WX_OVERCAST, WX_INT_NONE, 0},
        {"met-no", "fog", "fog", -1, WX_FOG, WX_INT_NONE, 0},
        {"met-no", "rain", "lightrainshowers_day", -1, WX_RAIN, WX_INT_LIGHT, 0},
        {"met-no", "rain", "rain", -1, WX_RAIN, WX_INT_MODERATE, 0},
        {"met-no", "rain", "heavyrain", -1, WX_RAIN, WX_INT_HEAVY, 0},
        {"met-no", "snow", "lightsleet", -1, WX_RAIN, WX_INT_LIGHT, 0},
        {"met-no", "snow", "heavysnow", -1, WX_SNOW, WX_INT_HEAVY, 0},
        {"met-no", "snow", "snowshowers_night", -1, WX_SNOW, WX_INT_MODERATE, 0},
        {"met-no", "storm", "heavyrainandthunder", -1, WX_STORM, WX_INT_HEAVY, 0},
        {"met-no", "storm", "rainshowersandthunder_day", -1, WX_STORM, WX_INT_MODERATE, 0},
        {"met-no", "clouds", "somethingnew", -1, WX_OVERCAST, WX_INT_NONE, 0},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        wx_look_t l = wx_look_from(C[i].provider, C[i].condition, C[i].code, C[i].night);
        CHECK(l.face == C[i].face && l.intensity == C[i].in && l.rime == (C[i].rime != 0),
              "%s/%s/%s night=%d -> %s int %d rime %d, want %s int %d rime %d", C[i].provider, C[i].condition,
              C[i].code ? C[i].code : "(null)", C[i].night, wx_face_name(l.face), l.intensity, l.rime,
              wx_face_name(C[i].face), C[i].in, C[i].rime);
    }
    CHECK(wx_look_from("open-meteo", "clouds", "2", 1).night, "partly cloudy at night should set night");
}

static void test_time_and_night(void)
{
    CHECK(wx_minute_of_day("2026-09-26T10:30:00+02:00") == 630, "minute of day");
    CHECK(wx_minute_of_day("2026-10-03T00:00:00Z") == 0, "midnight");
    CHECK(wx_minute_of_day("2026-10-03T23:59:59-07:00") == 1439, "23:59");
    CHECK(wx_minute_of_day("garbage") == -1 && wx_minute_of_day(NULL) == -1 && wx_minute_of_day("2026-10-03T25:00") == -1,
          "bad input -> -1");
    int rise = 6 * 60 + 35, set = 18 * 60 + 20;
    CHECK(wx_is_night(65, rise, set) == 1, "01:05 is night (live sample: sun pair from the day before)");
    CHECK(wx_is_night(rise, rise, set) == 0, "sunrise minute is day");
    CHECK(wx_is_night(12 * 60, rise, set) == 0, "noon is day");
    CHECK(wx_is_night(set, rise, set) == 1, "sunset minute is night");
    CHECK(wx_is_night(23 * 60, rise, set) == 1, "23:00 night");
    CHECK(wx_is_night(-1, rise, set) == -1 && wx_is_night(600, -1, set) == -1, "unknown");
    CHECK(wx_is_night(2 * 60, 22 * 60, 10 * 60) == 0 && wx_is_night(15 * 60, 22 * 60, 10 * 60) == 1,
          "wrapped day (sunset before sunrise on the clock)");
}

static wx_obs_t sample_obs(void)
{
    wx_obs_t o;
    memset(&o, 0, sizeof o);
    o.valid = o.enabled = o.has_temp = true;
    o.temp_c = 16.6f;
    strcpy(o.provider, "open-meteo");
    strcpy(o.condition, "clear");
    strcpy(o.code, "0");
    o.now_min = 18 * 60 + 10;
    o.rise_min = 6 * 60 + 35;
    o.set_min = 18 * 60 + 20;
    return o;
}

static void test_look_from_obs(void)
{
    wx_obs_t o = sample_obs();
    wx_look_t l = wx_look_from_obs(&o);
    CHECK(l.face == WX_CLEAR_DAY && !l.still, "18:10 clear day");
    o.age_s = 15 * 60;
    l = wx_look_from_obs(&o);
    CHECK(l.face == WX_CLEAR_NIGHT && !l.still, "age moves the clock past sunset");
    o.age_s = WX_MAX_AGE_S + 1;
    CHECK(wx_look_from_obs(&o).still, "too old -> still");
    o = sample_obs();
    o.stale = true;
    CHECK(wx_look_from_obs(&o).still, "stale -> still");
    o = sample_obs();
    o.enabled = false;
    CHECK(wx_look_from_obs(&o).still, "disabled -> still");
    o = sample_obs();
    o.valid = false;
    CHECK(wx_look_from_obs(&o).still && wx_look_from_obs(NULL).still, "no data -> still");
    o = sample_obs();
    o.rise_min = o.set_min = -1;
    CHECK(wx_look_from_obs(&o).face == WX_CLEAR_DAY, "no sun times -> day");
    o = sample_obs();
    strcpy(o.condition, "storm");
    strcpy(o.code, "95");
    o.severe = true;
    l = wx_look_from_obs(&o);
    CHECK(l.face == WX_STORM && l.severe, "severe passes through");
}

static void test_sprites(void)
{
    static wx_stroke_t st[WX_STROKES_MAX];
    static bot_raster_scratch_t scratch;
    static uint8_t mask[256 * 256], tmp[256 * 256];
    static int segs[WX_STROKE_PTS];
    for (int i = 0; i < WX_STROKE_PTS; i++) segs[i] = i;
    long total = 0;
    for (int id = 0; id < WX_SPR_COUNT; id++) {
        wx_sprite_size_t z = wx_sprite_size(id);
        CHECK(z.w > 0 && z.h > 0 && z.frames > 0 && z.w * z.h <= (int)sizeof mask, "sprite %d size", id);
        CHECK(z.w <= WX_SKY_W && z.h <= WX_SKY_H, "sprite %d fits the sky", id);
        total += (long)z.w * z.h * z.frames;
        for (int f = 0; f < z.frames; f++) {
            int n = wx_sprite_strokes(id, f, st);
            CHECK(n >= 0 && n <= WX_STROKES_MAX, "sprite %d stroke count %d", id, n);
            memset(mask, 0, (size_t)(z.w * z.h));
            for (int k = 0; k < n; k++) {
                CHECK(st[k].n >= 2 && st[k].n <= WX_STROKE_PTS, "sprite %d.%d stroke %d points %d", id, f, k, st[k].n);
                float m = st[k].hw + 0.5f;
                for (int p = 0; p < st[k].n; p++)
                    CHECK(st[k].x[p] - m >= 0 && st[k].x[p] + m <= z.w && st[k].y[p] - m >= 0 && st[k].y[p] + m <= z.h,
                          "sprite %d.%d stroke %d point (%.2f,%.2f) hw %.2f outside %dx%d", id, f, k, st[k].x[p],
                          st[k].y[p], st[k].hw, z.w, z.h);
                int pw = z.w + 8, ph = z.h + 8, outside = 0;
                bot_raster_stroke(&scratch, NULL, tmp, pw, ph, -4, -4, st[k].x, st[k].y, st[k].n - 1, segs, st[k].hw,
                                  0xFFFFFF, 1.0f);
                for (int y = 0; y < ph; y++)
                    for (int x = 0; x < pw; x++) {
                        uint8_t a = tmp[y * pw + x];
                        int sx = x - 4, sy = y - 4;
                        if (sx < 0 || sy < 0 || sx >= z.w || sy >= z.h) outside += a != 0;
                        else if (a > mask[sy * z.w + sx]) mask[sy * z.w + sx] = a;
                    }
                CHECK(outside == 0, "sprite %d.%d stroke %d: %d px of ink outside the sprite", id, f, k, outside);
            }
            wx_sprite_fill(id, f, mask);
            int ink = 0;
            for (int q = 0; q < z.w * z.h; q++) ink += mask[q] != 0;
            CHECK(ink > 0, "sprite %d frame %d is empty", id, f);
        }
    }
    printf("  sprite masks: %ld bytes total\n", total);
    CHECK(total < 200 * 1024, "sprite masks %ld bytes, want < 200 KB", total);
}

static void check_frame(const wx_scene_t *s, const char *what)
{
    static wx_draw_t d[WX_MAX_DRAW];
    int n = wx_scene_draw(s, d, WX_MAX_DRAW);
    CHECK(n > 0 && n < WX_MAX_DRAW, "%s: %d draw commands", what, n);
    for (int i = 0; i < n; i++) {
        wx_sprite_size_t z = wx_sprite_size(d[i].sprite);
        CHECK(d[i].sprite < WX_SPR_COUNT && d[i].frame < z.frames, "%s: bad sprite %d frame %d", what, d[i].sprite,
              d[i].frame);
        CHECK(d[i].x >= 0 && d[i].y >= 0 && d[i].x + z.w <= WX_SKY_W && d[i].y + z.h <= WX_SKY_H,
              "%s t=%.2f: sprite %d at (%d,%d) %dx%d leaves the %dx%d canvas", what, s->t, d[i].sprite, d[i].x, d[i].y,
              z.w, z.h, WX_SKY_W, WX_SKY_H);
    }
    for (int i = 0; i < s->np; i++) {
        const wx_particle_t *p = &s->p[i];
        wx_sprite_size_t z = wx_sprite_size(p->sprite);
        CHECK(p->x - p->amp >= 0 && p->x + p->amp + z.w <= WX_SKY_W && p->y >= 0 && p->y + z.h <= WX_SKY_H,
              "%s: particle %d at (%.1f,%.1f) outside", what, i, p->x, p->y);
    }
}

static void test_particles_stay_inside(void)
{
    static wx_scene_t s;
    for (int face = 0; face < WX_FACE_COUNT; face++)
        for (int in = WX_INT_NONE; in <= WX_INT_HEAVY; in++)
            for (int night = 0; night < 2; night++)
                for (uint32_t seed = 1; seed <= 4; seed++) {
                    char what[64];
                    snprintf(what, sizeof what, "%s int %d night %d seed %u", wx_face_name((wx_face_t)face), in, night,
                             (unsigned)seed);
                    wx_look_t l = {.face = (wx_face_t)face, .intensity = (wx_intensity_t)in, .night = night != 0};
                    wx_scene_init(&s, seed * 7919);
                    wx_scene_set_look(&s, &l);
                    uint32_t r = seed;
                    int frames = 0;
                    check_frame(&s, what);
                    for (double t = 0; t < 180; ) {
                        r = r * 1103515245u + 12345u;
                        double dt = 0.010 + 0.012 * ((r >> 16) & 1023) / 1023.0;
                        if (((r >> 8) & 255) == 0) dt = 0.7;
                        t += dt;
                        if (wx_scene_tick(&s, dt)) {
                            frames++;
                            check_frame(&s, what);
                        }
                    }
                    CHECK(frames > 100, "%s: only %d frames in 3 min", what, frames);
                }
}

static void test_counts_and_pacing(void)
{
    static wx_scene_t s;
    static wx_draw_t d[WX_MAX_DRAW];
    const wx_intensity_t ins[3] = {WX_INT_LIGHT, WX_INT_MODERATE, WX_INT_HEAVY};
    int prev = 0;
    for (int i = 0; i < 3; i++) {
        wx_look_t l = {.face = WX_RAIN, .intensity = ins[i]};
        wx_scene_init(&s, 5);
        wx_scene_set_look(&s, &l);
        int drops = 0, n = wx_scene_draw(&s, d, WX_MAX_DRAW);
        for (int k = 0; k < n; k++) drops += d[k].sprite == WX_SPR_RAIN;
        CHECK(drops > prev, "rain intensity %d: %d drops, want more than %d", ins[i], drops, prev);
        prev = drops;
    }

    struct { wx_face_t f; double lo, hi; } P[] = {
        {WX_RAIN, 11.5, 12.5}, {WX_STORM, 11.5, 12.5}, {WX_SNOW, 9.5, 10.5},
        {WX_CLEAR_DAY, 3.5, 4.5}, {WX_FOG, 3.5, 4.5}, {WX_CLEAR_NIGHT, 3.5, 4.5},
    };
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
        wx_look_t l = {.face = P[i].f, .intensity = WX_INT_MODERATE};
        wx_scene_init(&s, 9);
        wx_scene_set_look(&s, &l);
        wx_scene_tick(&s, 0.016);
        int frames = 0;
        for (int k = 0; k < 60 * 60; k++) frames += wx_scene_tick(&s, 1.0 / 60);
        double fps = frames / 60.0;
        CHECK(fps >= P[i].lo && fps <= P[i].hi, "%s: %.2f fps, want %.1f-%.1f", wx_face_name(P[i].f), fps, P[i].lo,
              P[i].hi);
    }

    wx_look_t still = {.face = WX_RAIN, .intensity = WX_INT_MODERATE, .still = true};
    wx_scene_init(&s, 3);
    wx_scene_set_look(&s, &still);
    int frames = 0;
    for (int k = 0; k < 600; k++) frames += wx_scene_tick(&s, 1.0 / 60);
    CHECK(frames == 1, "still face drew %d frames, want 1", frames);
    wx_scene_invalidate(&s);
    CHECK(wx_scene_tick(&s, 0.016), "invalidate forces a frame");
    int n = wx_scene_draw(&s, d, WX_MAX_DRAW);
    bool grey = true;
    for (int k = 0; k < n; k++) if (d[k].rgb != 0 && d[k].rgb != 0x5A5A5A) grey = false;
    CHECK(n > 0 && grey, "still face draws grey");

    wx_look_t rain = {.face = WX_RAIN, .intensity = WX_INT_MODERATE};
    wx_scene_init(&s, 3);
    CHECK(wx_scene_set_look(&s, &rain), "first look applies");
    CHECK(!wx_scene_set_look(&s, &rain), "same look is a no-op");
}

static void test_storm_flashes(void)
{
    static wx_scene_t s;
    wx_look_t l = {.face = WX_STORM, .intensity = WX_INT_MODERATE};
    wx_scene_init(&s, 77);
    wx_scene_set_look(&s, &l);
    int flashes = 0;
    bool was = false;
    for (int k = 0; k < 120 * 60; k++) {
        if (!wx_scene_tick(&s, 1.0 / 60)) continue;
        bool f = wx_scene_flash(&s);
        if (f && !was) flashes++;
        was = f;
    }
    CHECK(flashes >= 12 && flashes <= 45, "storm: %d flashes in 2 min", flashes);
}

static void test_age_text(void)
{
    static const struct {
        double age_s;
        const char *want;
    } C[] = {
        {-5, "just now"}, {0, "just now"}, {59.9, "just now"}, {60, "1 min ago"}, {119, "1 min ago"},
        {3599, "59 min ago"}, {3600, "1 h ago"}, {7200, "2 h ago"}, {7199, "1 h ago"}, {86399, "23 h ago"},
        {86400, "1 d ago"}, {3 * 86400 + 5, "3 d ago"}, {1e12, "999 d ago"},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        char buf[16];
        int n = wx_age_text(C[i].age_s, buf, sizeof buf);
        CHECK(strcmp(buf, C[i].want) == 0 && n == (int)strlen(buf), "%.1f s -> '%s', want '%s'", C[i].age_s, buf,
              C[i].want);
    }
    char nan_buf[16];
    wx_age_text(NAN, nan_buf, sizeof nan_buf);
    CHECK(strcmp(nan_buf, "just now") == 0, "NaN -> just now");
    char tiny[4];
    CHECK(wx_age_text(7200, tiny, sizeof tiny) == 7 && strcmp(tiny, "2 h") == 0, "truncates, returns the full length");
}

int main(void)
{
    test_mapping_table();
    test_time_and_night();
    test_look_from_obs();
    test_sprites();
    test_particles_stay_inside();
    test_counts_and_pacing();
    test_storm_flashes();
    test_age_text();
    if (failures) {
        printf("weather: %d failure(s)\n", failures);
        return 1;
    }
    printf("weather: all tests passed\n");
    return 0;
}
