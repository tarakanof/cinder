#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_cast.h"
#include "wx_faces.h"
#include "wx_paint.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define DAY 1782000000LL
#define LOCAL(h, m) (DAY + (h) * 3600LL + (m) * 60LL - 3600)
#define NOW LOCAL(14, 30)

static wx_obs_t obs_at(long long now)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = 12, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .moon_up = -1, .observed_at = now - 300, .has_obs_age = true, .obs_age_s = 300,
                  .has_rain = true, .has_utc_offset = true, .utc_offset = 3600, .has_now_rain = true};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "clouds");
    snprintf(o.code, sizeof o.code, "3");
    return o;
}

static wx_obs_t dry_with(long long start, long long end, int in, int kind)
{
    wx_obs_t o = obs_at(NOW);
    o.rain_n = 1;
    o.rain_spell[0] = (wx_spell_t){start, end, in, kind};
    return o;
}

static wx_obs_t wet_until(long long until)
{
    wx_obs_t o = obs_at(NOW);
    snprintf(o.condition, sizeof o.condition, "rain");
    snprintf(o.code, sizeof o.code, "63");
    o.now_rain = 2;
    o.rain_until = until;
    o.rain_n = 2;
    o.rain_spell[0] = (wx_spell_t){LOCAL(14, 0), until, 2, 0};
    o.rain_spell[1] = (wx_spell_t){LOCAL(20, 0), LOCAL(21, 0), 3, 0};
    return o;
}

static bool near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void test_round(void)
{
    static const struct {
        int min;
        bool hours;
        int count;
        const char *txt;
    } C[] = {
        {45, false, 9, "45"},  {150, true, 5, "2\xC2\xBD"}, {90, true, 3, "1\xC2\xBD"}, {57, false, 11, "55"},
        {58, true, 2, "1"},    {0, false, 1, "5"},           {2, false, 1, "5"},         {3, false, 1, "5"},
        {8, false, 2, "10"},   {75, true, 3, "1\xC2\xBD"},   {74, true, 2, "1"},         {719, true, 24, "12"},
        {105, true, 4, "2"},   {30, false, 6, "30"},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        bool h;
        int n;
        char buf[12];
        wx_cast_round(C[i].min * 60LL, &h, &n);
        wx_cast_count_text(h, n, buf, sizeof buf);
        CHECK(h == C[i].hours && n == C[i].count && !strcmp(buf, C[i].txt), "%d min: %s %d '%s', want '%s'", C[i].min,
              h ? "h" : "min", n, buf, C[i].txt);
    }
    bool h;
    int n;
    wx_cast_round(-50, &h, &n);
    CHECK(!h && n == 1, "a past time counts as 5 min");
    char buf[12];
    wx_cast_count_text(true, 1, buf, sizeof buf);
    CHECK(!strcmp(buf, "\xC2\xBD"), "half an hour: '%s'", buf);
    wx_cast_count_text(true, 0, buf, sizeof buf);
    CHECK(!strcmp(buf, "0"), "count-up start: '%s'", buf);

    wx_cast_t c = {.has_count = true, .hours = true, .count = 5};
    static const char *const STEPS[] = {"0", "\xC2\xBD", "1", "1\xC2\xBD", "2", "2\xC2\xBD"};
    CHECK(wx_cast_count_n(&c) == 5, "2½ h counts up in 5 steps, as AD4");
    for (int k = 0; k <= 5; k++) {
        wx_cast_step_text(&c, k, buf, sizeof buf);
        CHECK(!strcmp(buf, STEPS[k]), "AD4 step %d: '%s'", k, buf);
    }
    c = (wx_cast_t){.has_count = true, .hours = false, .count = 9};
    wx_cast_step_text(&c, 3, buf, sizeof buf);
    CHECK(wx_cast_count_n(&c) == 9 && !strcmp(buf, "15"), "45 min counts in 5-min steps: '%s'", buf);
    wx_cast_step_text(&c, 99, buf, sizeof buf);
    CHECK(!strcmp(buf, "45"), "clamped at the value");
    c.has_count = false;
    CHECK(wx_cast_count_n(&c) == 0, "no countdown, no steps");
}

static void test_canvases(void)
{
    wx_cast_t c;
    wx_obs_t o = dry_with(LOCAL(17, 0), LOCAL(19, 0), 1, 0);
    wx_cast_from(&o, NULL, &c);
    char buf[12];
    wx_cast_count_text(c.hours, c.count, buf, sizeof buf);
    CHECK(c.on && c.have && !c.wet && c.has_count && !strcmp(buf, "2\xC2\xBD") && c.hours, "AD4: 2½ h ('%s')", buf);
    CHECK(c.variant == 1 && !c.snow, "AD4: light, one white cloud");
    CHECK(!strcmp(c.sub, "#5C9CE0 light rain# at 17:00"), "AD4 sub-line: '%s'", c.sub);
    CHECK(!strcmp(c.label, "17") && near(c.label_deg, 150, 0.01f), "AD4 dial label '%s' at %.2f", c.label, c.label_deg);
    CHECK(near(c.now_deg, 75, 0.01f) && near(c.to_deg - c.now_deg, 78, 0.01f), "AD4: the drop travels 78 degrees (%.2f)",
          c.to_deg - c.now_deg);
    CHECK(c.narc == 1 && c.arc[0].a == 300 && c.arc[0].n == 120 && c.arc[0].intensity == 1, "AD4 arc %d+%d",
          c.arc[0].a, c.arc[0].n);
    float x, y;
    int al;
    wx_cast_label_at(c.label_deg, &x, &y, &al);
    CHECK(near(x, 327.0f, 0.05f) && near(y, 395.81f, 0.05f) && al == 0, "AD4 label at %.2f,%.2f (%d)", x, y, al);
    wx_cast_ring_xy(c.now_deg, &x, &y);
    CHECK(near(x, 447.44f, 0.01f) && near(y, 175.54f, 0.01f), "AD4 marker %.2f,%.2f", x, y);

    o = dry_with(LOCAL(15, 15), LOCAL(16, 30), 3, 0);
    wx_cast_from(&o, NULL, &c);
    wx_cast_count_text(c.hours, c.count, buf, sizeof buf);
    CHECK(!c.hours && !strcmp(buf, "45") && c.variant == 3, "AD3: 45 min, heavy ('%s')", buf);
    CHECK(!strcmp(c.label, "15:15") && near(c.to_deg - c.now_deg, 24, 0.01f), "AD3: label '%s', travel %.2f", c.label,
          c.to_deg - c.now_deg);
    CHECK(!strcmp(c.sub, "#5C9CE0 heavy rain# at 15:15"), "AD3 sub-line: '%s'", c.sub);
    wx_cast_label_at(c.label_deg, &x, &y, &al);
    CHECK(al == -1 && near(x, 233 + 198 * sinf(97.5f * (float)M_PI / 180), 0.01f), "AD3: label right-aligned at 3 o'clock");

    o = dry_with(LOCAL(16, 0), LOCAL(17, 30), 2, 0);
    wx_cast_from(&o, NULL, &c);
    wx_cast_count_text(c.hours, c.count, buf, sizeof buf);
    CHECK(c.hours && !strcmp(buf, "1\xC2\xBD") && c.variant == 2 && !strcmp(c.label, "16"), "AD5: 1½ h, moderate");
    CHECK(near(c.to_deg - c.now_deg, 48, 0.01f), "AD5: travel %.2f", c.to_deg - c.now_deg);
    CHECK(!strcmp(c.sub, "#5C9CE0 rain# at 16:00"), "AD5 sub-line: '%s'", c.sub);
}

static void test_states(void)
{
    wx_cast_t c;
    wx_obs_t o = wet_until(LOCAL(15, 40));
    wx_cast_from(&o, NULL, &c);
    char buf[12];
    wx_cast_count_text(c.hours, c.count, buf, sizeof buf);
    CHECK(c.wet && c.has_count && c.hours && !strcmp(buf, "1") && c.variant == 2, "wet: counts down to the stop ('%s')", buf);
    CHECK(!strcmp(c.sub, "#5C9CE0 rain# until 15:40") && !strcmp(c.label, "15:40"), "wet sub-line '%s', label '%s'", c.sub,
          c.label);
    CHECK(near(c.to_deg, c.now_deg + 35 - 1.5f, 0.01f), "wet: the drop lands just before the stop");
    CHECK(c.narc == 2 && c.arc[0].a == 150 && c.arc[0].n == 70 && c.arc[1].a == 480 && c.arc[1].n == 60 &&
              c.arc[1].intensity == 3,
          "wet arcs: from now (%d+%d), and the later heavy spell (%d+%d)", c.arc[0].a, c.arc[0].n, c.arc[1].a, c.arc[1].n);

    o.rain_until = 0;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.wet && !c.has_count && !strcmp(c.sub, "Rain") && c.variant == 2, "wet with no stop known: the word ('%s')", c.sub);
    o = wet_until(LOCAL(15, 40));
    o.now_rain = 0;
    o.rain_until = 0;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.wet && !c.has_count && !strcmp(c.sub, "Rain"), "a pause inside a spell is still wet (Ember's merged gap)");
    o = wet_until(LOCAL(14, 25));
    o.rain_spell[0].end = LOCAL(14, 25);
    wx_cast_from(&o, NULL, &c);
    CHECK(!c.wet && c.has_count && c.variant == 3 && !strcmp(c.label, "20"), "until passed on the knob's clock: dry, next spell");

    o = obs_at(NOW);
    wx_cast_from(&o, NULL, &c);
    CHECK(c.on && c.have && c.dry && !c.wet && !c.has_count && c.variant == 1 && !c.snow && c.narc == 0 &&
              !strcmp(c.sub, "No rain for 12 h") && !c.label[0],
          "spells []: dry for 12 h, the light cloud ('%s')", c.sub);
    o.has_rain = false;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.on && !c.have && !c.dry && !c.has_count && c.variant == 0 && !strcmp(c.sub, "No forecast"),
          "no rain block: no forecast");
    o = obs_at(NOW);
    o.has_utc_offset = false;
    wx_cast_from(&o, NULL, &c);
    CHECK(!c.have && !strcmp(c.sub, "No forecast"), "no offset: no dial times");
    o = obs_at(NOW);
    o.stale = true;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.on && !c.have && !c.dry && c.variant == 0 && !c.sub[0], "stale: grey, no sub-line");
    o = obs_at(NOW);
    o.age_s = 7200;
    wx_cast_from(&o, NULL, &c);
    CHECK(!c.have, "old data");
    o = obs_at(NOW);
    o.has_obs_age = false;
    wx_cast_from(&o, NULL, &c);
    CHECK(!c.have, "no knob clock");
    wx_cast_from(NULL, NULL, &c);
    CHECK(c.on && !c.have && !c.has_count && !c.dry, "no data");

    o = dry_with(LOCAL(18, 0), LOCAL(19, 0), 3, 1);
    wx_cast_from(&o, NULL, &c);
    CHECK(c.snow && c.variant == 3 && !strcmp(c.sub, "#5C9CE0 heavy snow# at 18:00"), "snow: '%s'", c.sub);

    o = dry_with(LOCAL(16, 0), LOCAL(17, 0), 1, 0);
    o.rain_n = 2;
    o.rain_spell[1] = o.rain_spell[0];
    o.rain_spell[0] = (wx_spell_t){LOCAL(22, 0), LOCAL(23, 0), 3, 0};
    wx_cast_from(&o, NULL, &c);
    CHECK(c.variant == 1 && !strcmp(c.label, "16"), "the earliest spell counts, whatever the order");

    o = wet_until(LOCAL(15, 40));
    o.rain_spell[0].intensity = 3;
    wx_cast_t heavy;
    wx_cast_from(&o, NULL, &heavy);
    o.rain_spell[0].intensity = 2;
    wx_cast_from(&o, &heavy, &c);
    CHECK(c.variant == 3 && strstr(c.sub, "heavy rain"), "wet: a falling peak keeps the shown heavy clouds");
    o.rain_spell[0].intensity = 1;
    o.rain_spell[0].kind = 1;
    wx_cast_from(&o, &heavy, &c);
    CHECK(c.variant == 1 && c.snow, "a new kind takes its own intensity");
    o = dry_with(LOCAL(16, 0), LOCAL(17, 0), 1, 0);
    wx_cast_from(&o, &heavy, &c);
    CHECK(c.variant == 1, "dry: no ratchet");
}

static void test_arcs(void)
{
    wx_obs_t o = dry_with(LOCAL(23, 0), LOCAL(4, 0) + 86400, 2, 0);
    wx_cast_t c, d;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.narc == 1 && c.arc[0].a == 660 && c.arc[0].n == 180, "a spell past 12 h is cut at now + 12 h (%d+%d)", c.arc[0].a,
          c.arc[0].n);
    o = wet_until(LOCAL(15, 40));
    wx_cast_from(&o, NULL, &c);
    wx_obs_t later = o;
    later.obs_age_s += 120;
    wx_cast_from(&later, NULL, &d);
    CHECK(wx_cast_same_ring(&c, &d) && !near(c.now_deg, d.now_deg, 0.1f), "2 min later: the marker moves, the dial stays");
    later.obs_age_s += 240;
    wx_cast_from(&later, NULL, &d);
    CHECK(!wx_cast_same_ring(&c, &d) && d.arc[0].a == 155, "past the 5-min step the wet arc starts later: a ring bake");
    o = dry_with(LOCAL(17, 0), LOCAL(19, 0), 1, 0);
    wx_cast_from(&o, NULL, &c);
    later = o;
    later.obs_age_s += 1800;
    wx_cast_from(&later, NULL, &d);
    CHECK(wx_cast_same_ring(&c, &d), "dry: the future arcs stay as time passes");
    o = dry_with(LOCAL(14, 0), LOCAL(14, 20), 1, 0);
    wx_cast_from(&o, NULL, &c);
    CHECK(c.narc == 0 && !c.has_count, "an ended spell draws nothing");
    o = dry_with(LOCAL(14, 0), LOCAL(14, 33), 1, 0);
    o.obs_age_s += 60;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.wet && c.narc == 1 && c.arc[0].a == 150 && c.arc[0].n == 3, "14:31: the spell ending 14:33 is lit (%d arcs)", c.narc);
    o.obs_age_s += 120;
    wx_cast_from(&o, NULL, &c);
    CHECK(!c.wet && c.narc == 0, "14:33: a spell ending exactly now off the 5-min grid is not lit (%d arcs)", c.narc);
    o.obs_age_s += 60;
    wx_cast_from(&o, NULL, &c);
    CHECK(!c.wet && c.dry && c.narc == 0, "14:34: a spell ending off the 5-min grid is not lit after it ended (%d arcs)", c.narc);
}

static void test_expired_until(void)
{
    wx_obs_t o = obs_at(NOW);
    snprintf(o.condition, sizeof o.condition, "rain");
    snprintf(o.code, sizeof o.code, "63");
    o.now_rain = 2;
    o.rain_until = LOCAL(14, 5);
    o.rain_n = 2;
    o.rain_spell[0] = (wx_spell_t){LOCAL(13, 0), LOCAL(14, 5), 2, 0};
    o.rain_spell[1] = (wx_spell_t){LOCAL(14, 20), LOCAL(14, 40), 3, 0};
    wx_cast_t c;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.wet && c.variant == 3 && !c.has_count && !strcmp(c.sub, "Heavy rain"),
          "a later spell covering now stays wet after the old until passed: '%s'", c.sub);
    wx_rain_t r;
    wx_rain_from(&o, &r);
    CHECK(r.on && !r.dry && r.intensity == 3 && !r.has_until && !strcmp(r.sub, "Heavy rain"),
          "Rain now agrees: wet, no past 'until' ('%s')", r.sub);
    wx_temp_t t;
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_RAIN, "the auto face is Rain now");
    o.rain_spell[1] = (wx_spell_t){LOCAL(14, 50), LOCAL(15, 10), 3, 0};
    wx_cast_from(&o, NULL, &c);
    wx_rain_from(&o, &r);
    CHECK(!c.wet && c.has_count && !r.on, "until passed and no spell covering now: dry in both faces");
    o.rain_spell[1] = (wx_spell_t){LOCAL(14, 0), LOCAL(14, 40), 3, 0};
    o.rain_n = 2;
    wx_cast_from(&o, NULL, &c);
    wx_rain_from(&o, &r);
    CHECK(!c.wet && !r.on, "a covering spell that began before the passed until does not revive it");
}

static void test_wet_paths(void)
{
    wx_obs_t o = obs_at(NOW);
    o.now_rain = 2;
    o.rain_until = LOCAL(15, 0);
    wx_cast_t c;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.wet && c.has_count && c.variant == 2 && !c.hours && c.count == 6, "wet by now.rain alone, no spell listed");
    snprintf(o.condition, sizeof o.condition, "snow");
    snprintf(o.code, sizeof o.code, "73");
    o.now_rain = 1;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.wet && c.snow && c.variant == 1 && strstr(c.sub, "light snow"), "no spell covering now: snow from the condition");
    o = dry_with(NOW, LOCAL(15, 0), 1, 0);
    o.rain_spell[0].end = NOW;
    o.rain_spell[0].start = LOCAL(14, 0);
    wx_cast_from(&o, NULL, &c);
    CHECK(!c.wet && c.narc == 0, "a spell ending exactly now does not cover now");
    o = dry_with(LOCAL(15, 0), LOCAL(15, 0) + 60, 1, 0);
    wx_cast_from(&o, NULL, &c);
    CHECK(c.narc == 1 && c.arc[0].n == 1, "a one-minute spell keeps its arc");
    o.rain_spell[0].end = LOCAL(15, 0) + 59;
    wx_cast_from(&o, NULL, &c);
    CHECK(c.narc == 0, "under a minute draws nothing");

    wx_obs_t w = wet_until(LOCAL(15, 40));
    w.rain_spell[0].intensity = 1;
    wx_cast_t light;
    wx_cast_from(&w, NULL, &light);
    w.rain_spell[0].intensity = 3;
    wx_cast_from(&w, &light, &c);
    CHECK(light.variant == 1 && c.variant == 3 && strstr(c.sub, "heavy rain"), "wet rain getting heavier takes the heavier clouds");
}

static void test_hold_key(void)
{
    wx_obs_t o = dry_with(LOCAL(15, 50), LOCAL(16, 30), 2, 0);
    wx_temp_t shown, t;
    wx_temp_face(&o, NULL, WXF_TEMP, &shown);
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "80 min with Temperature on screen: no hold, Temperature");
    wx_temp_face(&o, NULL, WXF_RAIN, &shown);
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "80 min with Rain now on screen: Temperature");
    o = dry_with(LOCAL(15, 30), LOCAL(16, 30), 2, 0);
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_CAST, "exactly 60 min: the forecast");
    o = dry_with(LOCAL(15, 30) + 1, LOCAL(16, 30), 2, 0);
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "a second past 60 min: Temperature");
    wx_temp_face(&o, NULL, WXF_CAST, &shown);
    o = dry_with(LOCAL(16, 0), LOCAL(16, 30), 2, 0);
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_CAST, "exactly 90 min while shown: held");
    o = dry_with(LOCAL(16, 0) + 1, LOCAL(16, 30), 2, 0);
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "past 90 min: released");
}

static void test_dry_horizon(void)
{
    wx_obs_t o = dry_with(LOCAL(15, 0), LOCAL(5, 0) + 86400, 2, 0);
    wx_cast_t c, d;
    wx_cast_from(&o, NULL, &c);
    bool same = true;
    for (int m = 5; m <= 25; m += 5) {
        wx_obs_t later = o;
        later.obs_age_s += m * 60;
        wx_cast_from(&later, NULL, &d);
        same &= wx_cast_same_ring(&c, &d);
    }
    CHECK(same, "dry, a spell running past now + 12 h: no dial bake every 5 min");
    wx_obs_t late = obs_at(LOCAL(14, 50));
    late.rain_n = 1;
    late.rain_spell[0] = (wx_spell_t){LOCAL(2, 30) + 86400, LOCAL(3, 30) + 86400, 2, 0};
    wx_cast_from(&late, NULL, &d);
    CHECK(d.has_count && d.narc == 1 && d.arc[0].a == 150 && d.arc[0].n >= 20,
          "a spell starting in the last hour before now + 12 h keeps its arc (%d arcs)", d.narc);
    late.obs_age_s += 120;
    wx_cast_t e;
    wx_cast_from(&late, NULL, &e);
    CHECK(d.arc[0].n == 25 && e.narc == 1 && e.arc[0].a == 150 && e.arc[0].a + e.arc[0].n == 175,
          "its far end is the 5-min step after now + 12 h: 02:55 at 14:50 and 14:52 (%d+%d)", e.arc[0].a, e.arc[0].n);
    wx_obs_t step = obs_at(LOCAL(14, 50));
    step.rain_n = 1;
    step.rain_spell[0] = (wx_spell_t){LOCAL(2, 0) + 86400, LOCAL(3, 0) + 86400, 2, 0};
    wx_cast_from(&step, NULL, &d);
    CHECK(d.narc == 1 && d.arc[0].a == 120 && d.arc[0].n == 55, "a spell starting exactly on the hour step keeps its arc (%d arcs)", d.narc);
    step.rain_spell[0] = (wx_spell_t){LOCAL(1, 59) + 86400, LOCAL(3, 0) + 86400, 2, 0};
    wx_cast_from(&step, NULL, &d);
    CHECK(d.narc == 1 && d.arc[0].a == 119 && d.arc[0].n == 1,
          "an off-grid spell a minute before the hour step keeps the hourly far end (%d arcs, %d+%d)", d.narc, d.arc[0].a, d.arc[0].n);
    wx_obs_t win = late;
    win.obs_age_s = 300;
    wx_cast_t w0;
    wx_cast_from(&win, NULL, &w0);
    bool cadence = true;
    for (int m = 1; m <= 4; m++) {
        win.obs_age_s = 300 + m * 60;
        wx_cast_from(&win, NULL, &e);
        cadence &= wx_cast_same_ring(&w0, &e);
    }
    win.obs_age_s = 300 + 5 * 60;
    wx_cast_from(&win, NULL, &e);
    CHECK(cadence && !wx_cast_same_ring(&w0, &e), "in the last-hour window the dial bakes every 5 min, not every minute");
    wx_obs_t two = obs_at(LOCAL(14, 50));
    two.rain_n = 2;
    two.rain_spell[0] = (wx_spell_t){LOCAL(1, 0) + 86400, LOCAL(2, 10) + 86400, 1, 0};
    two.rain_spell[1] = (wx_spell_t){LOCAL(2, 45) + 86400, LOCAL(4, 0) + 86400, 3, 0};
    wx_cast_from(&two, NULL, &d);
    CHECK(d.narc == 2 && d.arc[0].a == 60 && d.arc[0].n == 70 && d.arc[1].a == 165 && d.arc[1].n == 10,
          "a spell crossing the hour step keeps its rain past the hour while a later one runs to the marker (%d+%d, %d+%d)",
          d.arc[0].a, d.arc[0].n, d.arc[1].a, d.arc[1].n);
    CHECK(c.narc == 1 && c.arc[0].a == 180 && c.arc[0].n == 660, "the far end stops at the hour of now + 12 h (%d+%d)", c.arc[0].a,
          c.arc[0].n);
}

static void test_auto(void)
{
    wx_temp_t t, shown;
    wx_obs_t o = dry_with(LOCAL(15, 15), LOCAL(16, 0), 2, 0);
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_CAST && t.cast.on && !t.rain.on && t.cast.has_count, "dry, rain in 45 min: the forecast is the auto face");
    o = dry_with(LOCAL(15, 50), LOCAL(16, 30), 2, 0);
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP && !t.cast.on, "rain in 80 min: Temperature");
    wx_temp_face(&o, NULL, WXF_AUTO, &shown);
    shown.face = WXF_CAST;
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_CAST, "80 min while the forecast shows: it stays (hysteresis)");
    o = dry_with(LOCAL(16, 10), LOCAL(17, 0), 2, 0);
    wx_temp_face(&o, &shown, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "100 min: Temperature again");
    o = wet_until(LOCAL(15, 40));
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_RAIN && !t.cast.on, "wet: Rain now wins");
    o = dry_with(LOCAL(15, 0), LOCAL(16, 0), 2, 0);
    o.stale = true;
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "stale: never the forecast");
    o.stale = false;
    o.has_utc_offset = false;
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP, "no offset: never the auto forecast");
    CHECK(!wx_cast_soon(NULL, true), "no data");

    o = wet_until(LOCAL(15, 40));
    wx_temp_face(&o, NULL, WXF_CAST, &t);
    CHECK(t.face == WXF_CAST && t.cast.on && !t.rain.on && t.cast.wet, "picked while wet: the countdown to the stop");
    o = obs_at(NOW);
    wx_temp_face(&o, NULL, WXF_CAST, &t);
    CHECK(t.face == WXF_CAST && t.cast.have && t.cast.dry && !t.cast.has_count, "picked while dry for 12 h");
    o = dry_with(LOCAL(15, 15), LOCAL(16, 0), 2, 0);
    wx_temp_face(&o, NULL, WXF_TEMP, &t);
    CHECK(t.face == WXF_TEMP && !t.cast.on, "Temperature picked over an auto forecast carries no forecast");
    wx_temp_face(&o, NULL, WXF_RAIN, &t);
    CHECK(t.face == WXF_RAIN && t.rain.on && t.rain.dry && !t.cast.on, "Rain now picked over an auto forecast: dry");
}

static void test_bake_kind(void)
{
    wx_temp_t a, b;
    wx_obs_t o = dry_with(LOCAL(17, 0), LOCAL(19, 0), 1, 0);
    wx_temp_face(&o, NULL, WXF_CAST, &a);
    wx_temp_face(&o, NULL, WXF_TEMP, &b);
    CHECK(wx_temp_bake_kind(&a, &b) == WX_BAKE_FULL && wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "face change: full");
    wx_obs_t later = o;
    later.obs_age_s += 900;
    wx_temp_face(&later, &a, WXF_CAST, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == 0, "the countdown and the marker move without a bake");
    later.rain_spell[0].end = LOCAL(20, 0);
    wx_temp_face(&later, &a, WXF_CAST, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == WX_BAKE_RING, "a longer spell: the dial only");
    later.rain_spell[0].intensity = 2;
    wx_temp_face(&later, &a, WXF_CAST, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "another intensity: other clouds, a full scene");
    later = o;
    later.rain_spell[0].kind = 1;
    wx_temp_face(&later, &a, WXF_CAST, &b);
    CHECK(wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL, "snow: a full scene");
    later = obs_at(NOW);
    wx_temp_face(&later, &a, WXF_CAST, &b);
    CHECK(b.cast.dry && b.cast.variant == a.cast.variant && wx_temp_bake_kind(&b, &a) == WX_BAKE_FULL &&
              wx_temp_bake_kind(&a, &b) == WX_BAKE_FULL,
          "the rain leaves the 12 h, or comes back light: the same cloud, the drops go or come in a full scene");
    later.obs_age_s += 900;
    wx_temp_face(&later, &b, WXF_CAST, &a);
    CHECK(wx_temp_bake_kind(&a, &b) == 0, "dry for 12 h as time passes: no bake");
}

static void test_dry(void)
{
    char buf[12];
    uint32_t rgb = 0;
    wx_obs_t o = obs_at(NOW);
    wx_cast_t c;
    wx_cast_from(&o, NULL, &c);
    CHECK(wx_cast_count_n(&c) == 24, "dry counts up in half hours to 12: %d", wx_cast_count_n(&c));
    const char *u = wx_cast_value(&c, 99, buf, sizeof buf, &rgb);
    CHECK(u && !strcmp(u, "h") && !strcmp(buf, "12+") && rgb == WX_CAST_DRY_RGB, "dry: '%s' %s #%06X", buf, u ? u : "-",
          (unsigned)rgb);
    wx_cast_value(&c, 24, buf, sizeof buf, &rgb);
    CHECK(!strcmp(buf, "12+"), "the last step is 12+: '%s'", buf);
    wx_cast_value(&c, 23, buf, sizeof buf, &rgb);
    CHECK(!strcmp(buf, "11\xC2\xBD") && rgb == WX_CAST_DRY_RGB, "the step before: '%s'", buf);
    wx_cast_value(&c, 0, buf, sizeof buf, &rgb);
    CHECK(!strcmp(buf, "0"), "the count-up starts at 0: '%s'", buf);
    CHECK(wx_cast_sides(c.variant) == 1 && wx_cast_sheet(c.variant, 0), "dry: the light cloud's layout");
    CHECK(!wx_cast_soon(&o, false) && !wx_cast_soon(&o, true), "dry for 12 h is never the auto face");

    o.rain_n = 1;
    o.rain_spell[0] = (wx_spell_t){LOCAL(12, 0), LOCAL(14, 0), 2, 0};
    wx_cast_from(&o, NULL, &c);
    CHECK(c.dry && c.narc == 0 && !strcmp(c.sub, "No rain for 12 h"), "a spell that ended: dry, unlit dial");
    o.rain_spell[0] = (wx_spell_t){LOCAL(12, 0), NOW, 2, 0};
    wx_cast_from(&o, NULL, &c);
    CHECK(c.dry && !c.wet, "a spell ending exactly now: dry");

    o = dry_with(LOCAL(17, 0), LOCAL(19, 0), 2, 0);
    wx_cast_from(&o, NULL, &c);
    u = wx_cast_value(&c, 99, buf, sizeof buf, &rgb);
    CHECK(!c.dry && u && !strcmp(u, "h") && !strcmp(buf, "2\xC2\xBD") && rgb == WX_CAST_RGB, "a spell due: the countdown in blue");
    o = dry_with(LOCAL(15, 15), LOCAL(16, 0), 2, 0);
    wx_cast_from(&o, NULL, &c);
    u = wx_cast_value(&c, 99, buf, sizeof buf, &rgb);
    CHECK(u && !strcmp(u, "min") && !strcmp(buf, "45") && rgb == WX_CAST_RGB, "minutes: '%s'", buf);

    o = wet_until(LOCAL(15, 40));
    o.rain_until = 0;
    wx_cast_from(&o, NULL, &c);
    u = wx_cast_value(&c, 99, buf, sizeof buf, &rgb);
    CHECK(!c.dry && !u && !strcmp(buf, "--") && rgb == WX_CAST_GREY_RGB, "wet without a stop: '--' grey");
    o = obs_at(NOW);
    o.has_rain = false;
    wx_cast_from(&o, NULL, &c);
    u = wx_cast_value(&c, 99, buf, sizeof buf, &rgb);
    CHECK(!c.dry && !u && !strcmp(buf, "--") && rgb == WX_CAST_GREY_RGB && wx_cast_count_n(&c) == 0, "no forecast: '--' grey");
    o = obs_at(NOW);
    o.stale = true;
    wx_cast_from(&o, NULL, &c);
    u = wx_cast_value(&c, 99, buf, sizeof buf, &rgb);
    CHECK(!u && !strcmp(buf, "--") && rgb == WX_CAST_GREY_RGB, "stale: '--' grey");
    wx_cast_from(NULL, NULL, &c);
    u = wx_cast_value(&c, 99, buf, sizeof buf, &rgb);
    CHECK(!u && !strcmp(buf, "--") && rgb == WX_CAST_GREY_RGB, "no data: '--' grey");
}

static void test_motion(void)
{
    CHECK(wx_cast_ease(0) == 0 && wx_cast_ease(1) == 1 && wx_cast_ease(-1) == 0 && wx_cast_ease(2) == 1, "ease ends");
    double prev = 0;
    bool mono = true;
    for (int i = 1; i <= 100; i++) {
        double v = wx_cast_ease(i / 100.0);
        mono &= v >= prev - 1e-9;
        prev = v;
    }
    CHECK(mono, "ease is monotonic");
    CHECK(wx_cast_ease(0.5) > 0.6 && wx_cast_ease(0.5) < 0.8, "ease(0.5) = %.3f, the bezier's", wx_cast_ease(0.5));
    wx_cast_t c = {.now_deg = 75, .to_deg = 153, .has_count = true};
    CHECK(wx_cast_drop_deg(&c, 0) == 75 && wx_cast_drop_deg(&c, WX_CAST_SLIDE_S) == 153 && wx_cast_drop_deg(&c, 5) == 153,
          "the drop slides from now to the rain");
    CHECK(wx_cast_drop_opa(0) == 0 && abs(wx_cast_drop_opa(0.09) - 128) <= 1 && wx_cast_drop_opa(0.5) == 255 &&
              abs(wx_cast_drop_opa(1.08) - 128) <= 1 && wx_cast_drop_opa(1.2) == 0 && wx_cast_drop_opa(9) == 0 && wx_cast_drop_opa(NAN) == 0,
          "drop fade: in over 0.18 s, out from 0.96 to 1.2 s (canvas 0-3 %%, 16-20 %%)");
    CHECK(wx_cast_gate_opa(0.5) == 0 && wx_cast_gate_opa(0.96) == 0 && abs(wx_cast_gate_opa(1.05) - 128) <= 1 && wx_cast_gate_opa(1.2) == 255,
          "rain gate 16-19 %%");
    CHECK(wx_cast_label_opa(0.9) == 0 && abs(wx_cast_label_opa(1.14) - 128) <= 1 && wx_cast_label_opa(1.4) == 255, "label 16-22 %%");

    static const int PERIOD[3][2] = {{120, 120}, {120, 135}, {120, 0}};
    for (int v = 1; v <= 3; v++)
        for (int s = 0; s < wx_cast_sides(v); s++) {
            int lo = 99, hi = -99, jump = 0;
            for (int k = 0; k < 600; k++) {
                int x = wx_cast_breathe(v, s, k), n = wx_cast_breathe(v, s, k + 1);
                lo = x < lo ? x : lo;
                hi = x > hi ? x : hi;
                jump = abs(n - x) > jump ? abs(n - x) : jump;
                if (PERIOD[v - 1][s]) CHECK(x == wx_cast_breathe(v, s, k + PERIOD[v - 1][s]), "breathe %d/%d periodic", v, s);
            }
            CHECK(lo == -4 && hi == 4 && jump == 1, "breathe %d/%d: -4..4 px in 1 px steps (%d..%d, %d)", v, s, lo, hi, jump);
        }
    CHECK(wx_cast_breathe(2, 1, 0) != wx_cast_breathe(2, 0, 0), "the right moderate cloud runs 4 s ahead");
    int glo = 255, ghi = 0;
    for (int k = 0; k < 96; k++) {
        CHECK(wx_cast_gloom(1, k) == 255 && wx_cast_gloom(2, k) == 255, "only heavy pulses");
        int g = wx_cast_gloom(3, k);
        glo = g < glo ? g : glo;
        ghi = g > ghi ? g : ghi;
    }
    CHECK(ghi == 255 && glo >= 178 && glo <= 182, "heavy pulse 1 -> 0.7 (%d..%d)", glo, ghi);

    for (int v = 1; v <= 3; v++)
        for (int s = 0; s < wx_cast_sides(v); s++) {
            const wx_cast_sheet_t *sh = wx_cast_sheet(v, s);
            int frames = 2 * sh->tile_frames;
            bool ok = true;
            for (int k = 0; k < 3 * frames; k++) {
                int off = wx_cast_sheet_off(sh, false, k);
                ok &= off >= 0 && off < 2 * sh->h && off == wx_cast_sheet_off(sh, false, k + frames);
                ok &= wx_cast_sheet_off(sh, true, 2 * k) == wx_cast_sheet_off(sh, true, 2 * k + 2 * frames);
            }
            CHECK(ok, "sheet %d/%d loops every %d frames, snow every %d", v, s, frames, 2 * frames);
            CHECK(wx_cast_sheet_off(sh, false, -sh->delay_frames + frames) == 0, "sheet %d/%d delay %d frames", v, s, sh->delay_frames);
        }
    const wx_cast_sheet_t *lt = wx_cast_sheet(1, 0);
    bool half = true, differs = false;
    for (int k = 0; k < 120; k++) {
        half &= wx_cast_sheet_off(lt, true, 2 * k) == wx_cast_sheet_off(lt, false, k);
        differs |= wx_cast_sheet_off(lt, true, k) != wx_cast_sheet_off(lt, false, k);
    }
    CHECK(lt->delay_frames == 0 && half && differs, "snow falls at half speed");
    CHECK(wx_cast_sides(0) == 0 && wx_cast_sides(1) == 1 && wx_cast_sides(2) == 2 && wx_cast_sides(3) == 2 && !wx_cast_sheet(1, 1) &&
              !wx_cast_sheet(0, 0),
          "sides per variant");
}

static void test_labels(void)
{
    float x, y;
    int al;
    wx_cast_label_at(90, &x, &y, &al);
    CHECK(al == -1 && near(x, 431, 0.01f) && near(y, 233, 0.01f), "3 o'clock: right edge inside the ring");
    wx_cast_label_at(270, &x, &y, &al);
    CHECK(al == 1 && near(x, 35, 0.01f), "9 o'clock: left edge");
    wx_cast_label_at(0, &x, &y, &al);
    CHECK(al == 0 && near(y, 45, 0.01f), "12 o'clock: centred at r 188");
    wx_cast_label_at(405, &x, &y, &al);
    CHECK(al == 0, "45 degrees (wrapped) is centred");
    wx_cast_label_at(-90, &x, &y, &al);
    CHECK(al == 1, "negative angles wrap");
}

static int box_px(wx_box_t b) { return (b.x1 - b.x0) * (b.y1 - b.y0); }
static int box_h(wx_box_t b) { return b.y1 - b.y0; }

static bool inside(wx_box_t a, wx_box_t b) { return a.x0 >= b.x0 && a.y0 >= b.y0 && a.x1 <= b.x1 && a.y1 <= b.y1; }

static void test_layout(void)
{
    int still = wx_clouds_max_px(false) * 3;
    for (int v = 0; v <= 3; v++) {
        wx_cast_layout_t l;
        wx_cast_layout(v, &l);
        CHECK(l.sides == wx_cast_sides(v), "variant %d sides", v);
        CHECK(l.total <= still, "variant %d: layers %d B fit the still-cloud buffer %d B", v, l.total, still);
        int px = 0;
        for (int s = 0; s < l.sides; s++) {
            const wx_cast_sheet_t *sh = wx_cast_sheet(v, s);
            wx_box_t cb = l.cloud[s], wide = {cb.x0 - 4, cb.y0, cb.x1 + 4, cb.y1};
            CHECK(inside(wide, l.box[s]) && inside(sh->box, l.box[s]), "variant %d side %d: one box holds the breathing cloud and its sheet", v, s);
            CHECK(inside(l.box[s], (wx_box_t){0, 0, WX_FACE, WX_FACE}) && box_h(sh->box) <= 2 * sh->h, "variant %d side %d inside", v, s);
            CHECK(!(cb.x0 & 1) && !(cb.y0 & 1), "even sprite origin");
            px += box_px(l.box[s]);
        }
        if (v) printf("cast: variant %d, %d side(s), layers %d of %d B, cloud+sheet boxes %d px\n", v, l.sides, l.total, still, px);
    }
    wx_cast_layout_t a, b;
    wx_cast_layout(2, &a);
    wx_cast_layout(3, &b);
    CHECK(a.box[0].x1 <= a.box[1].x0 && b.box[0].x1 <= b.box[1].x0, "the two sides never overlap: two areas, never one joined box");
}

static void test_areas(void)
{
    for (int v = 1; v <= 3; v++) {
        wx_cast_layout_t l;
        wx_cast_layout(v, &l);
        int max_areas = 0, frames_with_cloud = 0, px_total = 0, n = 0;
        int pdx[2] = {99, 99}, popa[2] = {-1, -1}, poff[2] = {-1, -1};
        for (int k = 0; k < 900; k++) {
            int areas = 0;
            for (int s = 0; s < l.sides; s++) {
                const wx_cast_sheet_t *sh = wx_cast_sheet(v, s);
                int dx = wx_cast_breathe(v, s, k), opa = wx_cast_gloom(v, k), off = wx_cast_sheet_off(sh, false, k);
                bool cloud = dx != pdx[s] || opa != popa[s], sheet = off != poff[s];
                if (cloud) frames_with_cloud++;
                if (cloud || sheet) {
                    areas++;
                    px_total += cloud ? box_px(l.box[s]) : box_px(sh->box);
                }
                pdx[s] = dx;
                popa[s] = opa;
                poff[s] = off;
            }
            if (k > 0) {
                max_areas = areas > max_areas ? areas : max_areas;
                n++;
            }
        }
        CHECK(max_areas <= l.sides, "variant %d: at most %d areas per 15 fps frame (%d)", v, l.sides, max_areas);
        printf("cast: variant %d, steady %d area(s) per frame, avg %d px redrawn, cloud steps on %d of 900 frames\n", v, max_areas,
               px_total / n, frames_with_cloud);
    }
}

static uint16_t ring_px(const uint16_t *bg, float deg)
{
    float x, y;
    wx_cast_ring_xy(deg, &x, &y);
    return bg[(int)y * WX_FACE + (int)x];
}

static void test_paint(void)
{
    static wx_ring_t ring;
    static uint16_t bg[WX_FACE * WX_FACE], lut[WX_RING_LUT];
    size_t still = (size_t)wx_clouds_max_px(false) * 3;
    uint8_t *m = calloc(1, still), *hero = calloc(1, WX_HERO_BOX * WX_HERO_BOX * 3), *drift = calloc(1, (size_t)wx_clouds_max_px(true) * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .still = m, .drift = drift};
    wx_obs_t o = dry_with(LOCAL(17, 0), LOCAL(19, 0), 1, 0);
    wx_temp_t t;
    wx_temp_face(&o, NULL, WXF_CAST, &t);
    wx_bake_out_t out = {.hero = {1, 1, 2, 2}, .still = {1, 1, 2, 2}, .drift = {1, 1, 2, 2}};
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    CHECK(out.hero.x1 == 0 && out.still.x1 == 0 && out.drift.x1 == 0, "no hero or cloud-band sprites");
    CHECK(ring_px(bg, 165) == wx_rgb565(0x4F81B7) && ring_px(bg, 195) == wx_rgb565(0x4F81B7), "AD4: 17-19 h lit light");
    CHECK(ring_px(bg, 152.5f) == wx_rgb565(0x4F81B7), "the lit arc starts with the 17 h cell (151.5)");
    CHECK(ring_px(bg, 105) == wx_rgb565(0x262626) && ring_px(bg, 225) == wx_rgb565(0x262626), "other hours grey");
    CHECK(ring_px(bg, 150) == 0 && ring_px(bg, 0) == 0 && ring_px(bg, 180) == 0, "gaps between the hour cells");
    wx_obs_t ow = dry_with(LOCAL(23, 0), LOCAL(1, 0) + 86400, 3, 0);
    wx_temp_face(&ow, NULL, WXF_CAST, &t);
    wx_bake(&t, WX_BAKE_RING, &b, &out);
    wx_ring_blit(&ring, bg);
    CHECK(ring_px(bg, 345) == wx_rgb565(0x3D7BF5) && ring_px(bg, 15) == wx_rgb565(0x3D7BF5),
          "a spell across 12 o'clock lights the cells on both sides");
    wx_temp_face(&o, NULL, WXF_CAST, &t);
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    float dx, dy;
    wx_face_dot_xy(WXF_CAST, WXF_N, &dx, &dy);
    CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_ON), "the forecast's face dot is white");
    wx_face_dot_xy(WXF_TEMP, WXF_N, &dx, &dy);
    CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_OFF), "the others grey");

    for (int v = 1; v <= 3; v++) {
        wx_obs_t ov = dry_with(LOCAL(17, 0), LOCAL(19, 0), v, 0);
        wx_temp_face(&ov, NULL, WXF_CAST, &t);
        memset(m, 0, still);
        clock_t t0 = clock();
        wx_bake(&t, WX_BAKE_FULL, &b, &out);
        double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        t0 = clock();
        wx_bake(&t, WX_BAKE_RING, &b, &out);
        printf("cast: variant %d host bake full %.2f ms, dial %.2f ms\n", v, ms, (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
        wx_cast_layout_t l;
        wx_cast_layout(v, &l);
        for (int s = 0; s < l.sides; s++) {
            wx_box_t cb = l.cloud[s];
            int w = cb.x1 - cb.x0, h = cb.y1 - cb.y0, ink = 0, edge = 0;
            const uint8_t *a = m + l.cloud_at[s] + (size_t)w * h * 2;
            for (int i = 0; i < w * h; i++) ink += a[i] > 128;
            for (int y = 0; y < h; y++) edge += a[y * w] > 0 || a[y * w + w - 1] > 0;
            CHECK(ink > 1500, "variant %d side %d: a cloud (%d px)", v, s, ink);
            CHECK(v == 1 || edge == 0, "variant %d side %d: the rim fade ends inside the sprite", v, s);
            const wx_cast_sheet_t *sh = wx_cast_sheet(v, s);
            int sw = sh->box.x1 - sh->box.x0, lit = 0;
            for (int i = 0; i < sw * 2 * sh->h; i++) lit += m[l.strip[s] + i] > 0;
            CHECK(lit > 20 * sh->n, "variant %d side %d: the strip has its streaks (%d px)", v, s, lit);
            for (int n = 0; n < sh->n; n++) {
                const wx_streak_t *d = &sh->a[n];
                int x = (int)(d->x - d->slant / 2) - sh->box.x0, y = (int)(d->y + d->len / 2);
                CHECK(m[l.strip[s] + y * sw + x] > 100, "variant %d side %d streak %d drawn", v, s, n);
            }
            CHECK(m[l.top[s]] > 150 && m[l.top[s]] > m[l.top[s] + (l.ramp_h[s] - 1) * sw] &&
                      m[l.bottom[s] + (l.ramp_h[s] - 1) * sw] > m[l.bottom[s]],
                  "variant %d side %d: the fade ramps", v, s);
        }
    }

    static const float P[4][2] = {{-15, 0}, {-9, 1.5f}, {-6.5f, 7}, {0, 7}};
    double worst = 0;
    for (int i = 0; i < WX_DROP_HW_N; i++) {
        double x = -15.0 + 15.0 * i / (WX_DROP_HW_N - 1), lo = 0, hi = 1;
        for (int it = 0; it < 60; it++) {
            double u = (lo + hi) / 2, v = 1 - u;
            double bx = v * v * v * P[0][0] + 3 * v * v * u * P[1][0] + 3 * v * u * u * P[2][0] + u * u * u * P[3][0];
            if (bx < x) lo = u;
            else hi = u;
        }
        double u = (lo + hi) / 2, v = 1 - u;
        double y = v * v * v * P[0][1] + 3 * v * v * u * P[1][1] + 3 * v * u * u * P[2][1] + u * u * u * P[3][1];
        worst = fabs(y - wx_drop_hw[i]) > worst ? fabs(y - wx_drop_hw[i]) : worst;
    }
    CHECK(worst < 1e-4, "the drop's tail table follows the canvas bezier (worst %.6f px)", worst);
    static uint16_t px[WX_DROP_BOX * WX_DROP_BOX];
    static uint8_t al[WX_DROP_BOX * WX_DROP_BOX];
    wx_img_t im = {.px = px, .a = al, .w = WX_DROP_BOX, .h = WX_DROP_BOX};
    clock_t t0 = clock();
    for (int i = 0; i < 100; i++) wx_paint_drop(&im, (float)i);
    printf("cast: drop paint %.3f ms per frame (host)\n", (double)(clock() - t0) * 10.0 / CLOCKS_PER_SEC);
    wx_box_t db = wx_paint_drop(&im, 90);
    float hx, hy;
    wx_cast_ring_xy(90, &hx, &hy);
    CHECK(db.x1 - db.x0 == WX_DROP_BOX && db.x0 == im.x0 && hx >= db.x0 + 16 && hx <= db.x0 + 18, "the drop's box holds its head");
    int cx = (int)hx - im.x0, cy = (int)hy - im.y0;
    CHECK(al[cy * WX_DROP_BOX + cx] == 255 && px[cy * WX_DROP_BOX + cx] == wx_rgb565(WX_CAST_RGB), "blue head");
    CHECK(al[(cy - 10) * WX_DROP_BOX + cx] == 255 && al[(cy + 11) * WX_DROP_BOX + cx] == 0,
          "at 3 o'clock the tail points back up the ring, the head leads down");
    CHECK(al[cy * WX_DROP_BOX + cx + 7] > 200 && px[cy * WX_DROP_BOX + cx + 7] == 0 && al[cy * WX_DROP_BOX + cx + 9] == 0,
          "a black stroke round the head");
    CHECK(al[0] == 0 && al[WX_DROP_BOX * WX_DROP_BOX - 1] == 0, "corners clear");
    free(m);
    free(hero);
    free(drift);
}

int main(void)
{
    test_round();
    test_canvases();
    test_states();
    test_arcs();
    test_expired_until();
    test_wet_paths();
    test_hold_key();
    test_dry_horizon();
    test_auto();
    test_bake_kind();
    test_dry();
    test_motion();
    test_labels();
    test_layout();
    test_areas();
    test_paint();
    if (failures) {
        printf("cast: %d failure(s)\n", failures);
        return 1;
    }
    printf("cast: all passed\n");
    return 0;
}
