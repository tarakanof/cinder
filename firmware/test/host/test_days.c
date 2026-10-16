#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_days.h"
#include "wx_faces.h"
#include "wx_paint.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NOW 1782043200LL
#define MON 1782082800LL
#define DAY 86400LL

static void set_day(wx_obs_t *o, int i, long long t, int face, int lo, int hi, int pop)
{
    o->day[i] = (wx_day_t){.t = t, .face = (signed char)face, .pop = (signed char)pop, .min_c = (short)lo, .max_c = (short)hi};
    if (o->day_n < i + 1) o->day_n = i + 1;
}

static void link_ends(wx_obs_t *o)
{
    for (int i = 0; i + 1 < o->day_n; i++) o->day[i].end = o->day[i + 1].t;
}

static wx_obs_t base(void)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = 12, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .moon_up = -1, .observed_at = NOW - 300, .has_obs_age = true, .obs_age_s = 300,
                  .has_utc_offset = true, .utc_offset = 3600};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "clear");
    snprintf(o.code, sizeof o.code, "0");
    return o;
}

static wx_obs_t an1(void)
{
    wx_obs_t o = base();
    set_day(&o, 0, MON + 4 * DAY, WX_PARTLY_CLOUDY, 8, 15, 10);
    set_day(&o, 1, MON + 5 * DAY, WX_RAIN, 9, 13, 70);
    set_day(&o, 2, MON + 6 * DAY, WX_CLEAR_DAY, 6, 17, 0);
    link_ends(&o);
    o.observed_at = MON + 3 * DAY + 50000;
    return o;
}

static wx_obs_t fixture(void)
{
    wx_obs_t o = base();
    set_day(&o, 0, MON, WX_OVERCAST, 7, 16, 32);
    set_day(&o, 1, MON + DAY, WX_RAIN, 11, 17, 19);
    set_day(&o, 2, MON + 2 * DAY, WX_SNOW, -4, 3, 40);
    link_ends(&o);
    return o;
}

static void test_weekday(void)
{
    static const struct {
        long long t;
        bool has;
        int off, want;
    } C[] = {
        {MON, true, 3600, 1},
        {MON + 3600, false, 0, 1},
        {MON + 3600, true, 0, 1},
        {MON + 3600 + 18000, true, -18000, 1},
        {MON + 3600 - 50400, true, 50400, 1},
        {MON + 3600 + 43200, true, -43200, 1},
        {MON + 3600 - 46800, true, 46800, 1},
        {MON + 3600 - 46800, false, 0, 0},
        {MON + 3600 - 46800, false, 46800, 0},
        {MON + 3600 - 43100, false, 0, 1},
        {-DAY, false, 0, 3},
        {MON + 6 * DAY, true, 3600, 0},
        {MON + 5 * DAY, true, 3600, 6},
        {MON + 2 * DAY, true, 3600, 3},
        {3600 - 3600, true, 3600, 4},
        {MON + 7 * DAY, true, 3600, 1},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        int w = wx_weekday(C[i].t, C[i].has, C[i].off);
        CHECK(w == C[i].want, "case %zu: t %lld offset %d (%s): weekday %d, want %d", i, C[i].t, C[i].off, C[i].has ? "known" : "unknown",
              w, C[i].want);
    }
    static const char *const N[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (int d = 0; d < 7; d++) CHECK(!strcmp(wx_weekday_name(d), N[d]), "name %d: %s", d, wx_weekday_name(d));
    CHECK(!strcmp(wx_weekday_name(-1), "Sat") && !strcmp(wx_weekday_name(7), "Sun") && !strcmp(wx_weekday_name(-15), "Sat"),
          "names wrap both ways");
}

static void test_icons(void)
{
    static const struct {
        int face, icon;
        bool wet;
    } C[] = {{WX_CLEAR_DAY, WX_ICON_SUN, false},   {WX_CLEAR_NIGHT, WX_ICON_SUN, false}, {WX_PARTLY_CLOUDY, WX_ICON_PARTLY, false},
             {WX_OVERCAST, WX_ICON_CLOUD, false},  {WX_FOG, WX_ICON_FOG, false},          {WX_RAIN, WX_ICON_RAIN, true},
             {WX_SNOW, WX_ICON_SNOW, true},        {WX_STORM, WX_ICON_STORM, true},       {-1, WX_ICON_CLOUD, false},
             {WX_FACE_COUNT, WX_ICON_CLOUD, false}};
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        int icon = (int)wx_days_icon(C[i].face);
        CHECK(icon == C[i].icon && wx_days_wet(icon) == C[i].wet, "face %d: icon %d wet %d, want %d %d", C[i].face, icon,
              wx_days_wet(icon), C[i].icon, C[i].wet);
    }
    CHECK(!wx_days_wet(WX_ICONS) && !wx_days_wet(-1), "nothing else is wet");
}

static void test_from(void)
{
    wx_obs_t o = an1();
    wx_days_t d;
    wx_days_from(&o, &d);
    CHECK(d.on && d.have && d.n == 3, "AN1: three rows");
    static const wx_days_row_t WANT[3] = {{5, WX_ICON_PARTLY, -1, 8, 15}, {6, WX_ICON_RAIN, 70, 9, 13}, {0, WX_ICON_SUN, -1, 6, 17}};
    for (int i = 0; i < 3; i++)
        CHECK(d.row[i].wday == WANT[i].wday && d.row[i].icon == WANT[i].icon && d.row[i].pop == WANT[i].pop && d.row[i].lo == WANT[i].lo &&
                  d.row[i].hi == WANT[i].hi,
              "AN1 row %d: %s icon %d pop %d %d..%d", i, wx_weekday_name(d.row[i].wday), d.row[i].icon, d.row[i].pop, d.row[i].lo,
              d.row[i].hi);
    CHECK(d.amin == 6 && d.amax == 17, "one axis over the rows: %d..%d", d.amin, d.amax);

    o = fixture();
    wx_days_from(&o, &d);
    CHECK(d.n == 3 && d.row[0].wday == 1 && d.row[1].wday == 2 && d.row[2].wday == 3, "the fixture: Mon, Tue, Wed");
    CHECK(d.row[0].pop == -1 && d.row[1].pop == 19 && d.row[2].pop == 40, "the chance only on wet days: %d %d %d", d.row[0].pop,
          d.row[1].pop, d.row[2].pop);
    CHECK(d.amin == -4 && d.amax == 17, "axis across a frosty day: %d..%d", d.amin, d.amax);
    o.day[1].pop = -1;
    o.day[2].face = WX_STORM;
    o.day[2].pop = 120;
    wx_days_from(&o, &d);
    CHECK(d.row[1].pop == -1 && d.row[2].pop == 100 && d.row[2].icon == WX_ICON_STORM, "no chance known: none; over 100: 100");
    o.day[1].pop = 0;
    wx_days_from(&o, &d);
    CHECK(d.row[1].pop == 0, "a wet day's 0 %% shows");
    o.day[0].face = WX_FOG;
    o.day[0].pop = 50;
    wx_days_from(&o, &d);
    CHECK(d.row[0].pop == -1, "fog is not wet: no chance");

    o = fixture();
    o.day_n = 2;
    wx_days_from(&o, &d);
    CHECK(d.have && d.n == 2 && d.amin == 7 && d.amax == 17, "two days: two rows, their own axis");
    o.day_n = 1;
    o.day[0].min_c = o.day[0].max_c = 12;
    wx_days_from(&o, &d);
    CHECK(d.have && d.n == 1 && d.amin == 11 && d.amax == 13, "one flat day: the axis widens by a degree each way");
    o.day_n = 0;
    wx_days_from(&o, &d);
    CHECK(d.on && !d.have && d.n == 0, "no days: on, no rows");
    o.day_n = -3;
    wx_days_from(&o, &d);
    CHECK(!d.have && d.n == 0, "a negative count: no rows");
    o = fixture();
    o.day_n = 9;
    wx_days_from(&o, &d);
    CHECK(d.n == WX_DAYS, "a count past the array: %d rows", d.n);

    o = fixture();
    o.day[0].min_c = 150;
    o.day[0].max_c = -150;
    wx_days_from(&o, &d);
    CHECK(d.row[0].lo == -99 && d.row[0].hi == 99, "clamped to -99..99, in order: %d..%d", d.row[0].lo, d.row[0].hi);

    const wx_obs_t *none[] = {NULL};
    wx_days_from(none[0], &d);
    CHECK(d.on && !d.have, "no data");
    o = fixture();
    o.valid = false;
    wx_days_from(&o, &d);
    CHECK(d.on && !d.have, "invalid data");
    o = fixture();
    o.enabled = false;
    wx_days_from(&o, &d);
    CHECK(d.on && !d.have, "weather off");
    o = fixture();
    o.stale = true;
    o.age_s = 7200;
    wx_days_from(&o, &d);
    CHECK(d.have && d.n == 3, "stale or old data keeps its days that have not ended");
}

static void test_ended(void)
{
    wx_obs_t o = fixture();
    wx_days_t d;
    o.observed_at = MON + DAY - 1 - 300;
    wx_days_from(&o, &d);
    CHECK(d.n == 3 && d.row[0].wday == 1, "Monday's last second: Monday shows");
    o.observed_at = MON + DAY - 300;
    wx_days_from(&o, &d);
    CHECK(d.n == 2 && d.row[0].wday == 2 && d.row[1].wday == 3 && d.amin == -4 && d.amax == 17, "Tuesday: Monday is gone");
    o.observed_at = MON + DAY + 1800 - 300;
    o.day[0].end = MON + DAY + 3600;
    wx_days_from(&o, &d);
    CHECK(d.n == 3 && d.row[0].wday == 1, "a day ends at its end (here an hour late, a 25 h day)");
    o.day[0].end = MON + DAY;
    o.observed_at = MON + 3 * DAY - 1 - 300;
    wx_days_from(&o, &d);
    CHECK(d.n == 1 && d.row[0].wday == 3, "the last day lasts 24 h");
    o.observed_at = MON + 3 * DAY - 300;
    wx_days_from(&o, &d);
    CHECK(!d.have && d.n == 0, "every day over: no rows");
    o.age_s = -1e9;
    wx_days_from(&o, &d);
    CHECK(!d.have, "a negative age does not move the clock back");
    o = fixture();
    o.observed_at = MON + 3 * DAY;
    o.has_obs_age = false;
    wx_days_from(&o, &d);
    CHECK(d.n == 3, "the knob's clock unknown: every day shows");
    o.has_obs_age = true;
    o.observed_at = 0;
    wx_days_from(&o, &d);
    CHECK(d.n == 3, "no observation time: every day shows");
    o = fixture();
    o.observed_at = MON - DAY;
    o.obs_age_s = 300;
    o.age_s = 2 * DAY;
    wx_days_from(&o, &d);
    CHECK(d.n == 2 && d.row[0].wday == 2, "the knob's own age counts: Monday ended while the view was not refreshed");
}

static void test_boundaries(void)
{
    wx_obs_t o = fixture();
    wx_days_t d;
    o.day_n = 2;
    o.day[0].end = MON + DAY;
    o.day[1] = o.day[2];
    o.day[1].end = MON + 3 * DAY;
    o.observed_at = MON + DAY + 1800 - 300;
    wx_days_from(&o, &d);
    CHECK(d.n == 1 && d.row[0].wday == 3, "Tuesday skipped by the parser: Monday still ends at Tuesday's midnight (%d rows)", d.n);
    o.day[0].end = 0;
    wx_days_from(&o, &d);
    CHECK(d.n == 1 && d.row[0].wday == 3, "an unknown end: 24 h, not the next kept day's start");

    static const struct {
        long long t, next;
        int off;
        const char *what;
    } DST[] = {
        {1774738800LL, 1774821600LL, 3600, "Belgrade 2026-03-29, 23 h"},
        {1792879200LL, 1792969200LL, 7200, "Belgrade 2026-10-25, 25 h"},
    };
    for (size_t k = 0; k < sizeof DST / sizeof DST[0]; k++) {
        wx_obs_t b = base();
        b.utc_offset = DST[k].off;
        set_day(&b, 0, DST[k].t, WX_CLEAR_DAY, 5, 10, -1);
        set_day(&b, 1, DST[k].next, WX_CLEAR_DAY, 6, 11, -1);
        b.day[0].end = DST[k].next;
        b.day[1].end = DST[k].next + 86400;
        b.observed_at = DST[k].next - 1 - 300;
        wx_days_from(&b, &d);
        CHECK(d.n == 2 && d.row[0].wday == 0, "%s: shown to its last second", DST[k].what);
        b.observed_at = DST[k].next - 300;
        wx_days_from(&b, &d);
        CHECK(d.n == 1 && d.row[0].wday == 1, "%s: gone at local midnight", DST[k].what);
    }
}

static void test_same(void)
{
    wx_obs_t o = fixture();
    wx_days_t a, b;
    wx_days_from(&o, &a);
    b = a;
    CHECK(wx_days_same(&a, &b), "same");
    b.row[2].lo = -5;
    CHECK(!wx_days_same(&a, &b), "a low");
    b = a;
    b.row[0].hi = 17;
    CHECK(!wx_days_same(&a, &b), "a high");
    b = a;
    b.row[1].pop = 20;
    CHECK(!wx_days_same(&a, &b), "a chance");
    b = a;
    b.row[1].icon = WX_ICON_STORM;
    CHECK(!wx_days_same(&a, &b), "an icon");
    b = a;
    b.row[0].wday = 2;
    CHECK(!wx_days_same(&a, &b), "a weekday");
    b = a;
    b.n = 2;
    CHECK(!wx_days_same(&a, &b), "a row less");
    b = a;
    b.amin = -5;
    CHECK(!wx_days_same(&a, &b), "the axis start");
    b = a;
    b.amax = 18;
    CHECK(!wx_days_same(&a, &b), "the axis end");
    b = a;
    b.have = false;
    CHECK(!wx_days_same(&a, &b), "rows going");
    b = a;
    b.on = false;
    CHECK(!wx_days_same(&a, &b), "the face");
    a.n = b.n = 1;
    b = a;
    b.on = true;
    b.row[2].lo = 50;
    CHECK(wx_days_same(&a, &b), "rows past n do not count");

    wx_temp_t t, s;
    wx_temp_face(&o, NULL, WXF_DAYS, &t);
    s = t;
    CHECK(wx_temp_bake_kind(&t, &s) == 0, "nothing changed: no bake");
    o.temp_c = 30;
    o.has_wind = true;
    o.wind_kmh = 60;
    wx_temp_face(&o, &s, WXF_DAYS, &t);
    CHECK(wx_temp_bake_kind(&t, &s) == 0, "the current weather is not on this face");
    o.day[1].max_c = 18;
    wx_temp_face(&o, &s, WXF_DAYS, &t);
    CHECK(wx_temp_bake_kind(&t, &s) == WX_BAKE_FULL, "a forecast change: a full scene bake");
    wx_temp_face(&o, &s, WXF_WIND, &t);
    CHECK(wx_temp_bake_kind(&t, &s) == WX_BAKE_FULL && wx_temp_bake_kind(&s, &t) == WX_BAKE_FULL, "to and from another face");
}

static void test_pick(void)
{
    wx_obs_t o = fixture();
    wx_temp_t t;
    wx_temp_face(&o, NULL, WXF_DAYS, &t);
    CHECK(t.face == WXF_DAYS && t.days.on && t.days.have && !t.rain.on && !t.cast.on && !t.wind.on, "picked: Next days only");
    static const char *const COND[][2] = {{"rain", "63"}, {"storm", "95"}, {"clear", "0"}, {"snow", "73"}};
    for (size_t i = 0; i < sizeof COND / sizeof COND[0]; i++) {
        wx_obs_t w = fixture();
        snprintf(w.condition, sizeof w.condition, "%s", COND[i][0]);
        snprintf(w.code, sizeof w.code, "%s", COND[i][1]);
        w.has_wind = true;
        w.wind_kmh = 80;
        w.has_rain = w.has_now_rain = true;
        w.now_rain = i == 0 ? 2 : 0;
        w.rain_n = 1;
        w.rain_spell[0] = (wx_spell_t){NOW + 600, NOW + 3600, 2, 0};
        wx_temp_face(&w, NULL, WXF_AUTO, &t);
        CHECK(t.face != WXF_DAYS && !t.days.on, "%s: the auto face is never Next days (%d)", COND[i][0], t.face);
        wx_temp_face(&w, NULL, WXF_DAYS, &t);
        CHECK(t.face == WXF_DAYS && t.days.on && !t.rain.on && !t.cast.on && !t.wind.on, "%s: picked, Next days on any weather",
              COND[i][0]);
    }
    wx_temp_t s = t;
    wx_temp_face(&o, &s, WXF_TEMP, &t);
    CHECK(t.face == WXF_TEMP && !t.days.on, "picking away clears the days");
    wx_temp_face(&o, &s, WXF_WIND, &t);
    CHECK(t.face == WXF_WIND && !t.days.on, "and for the wind");
    wx_temp_face(NULL, NULL, WXF_DAYS, &t);
    CHECK(t.face == WXF_DAYS && t.days.on && !t.days.have, "no data: on, no rows");
}

static void test_rows_y(void)
{
    CHECK(wx_days_row_y(3, 0) == 143 && wx_days_row_y(3, 1) == 228 && wx_days_row_y(3, 2) == 313, "three rows as AN1");
    CHECK(wx_days_row_y(2, 0) == 186 && wx_days_row_y(2, 1) == 270, "two rows centred on the middle one: %d %d", wx_days_row_y(2, 0),
          wx_days_row_y(2, 1));
    CHECK(wx_days_row_y(1, 0) == 228, "one row in the middle");
}

static bool near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void test_span(void)
{
    wx_obs_t o = an1();
    wx_days_t d;
    wx_days_from(&o, &d);
    float x0, x1;
    wx_days_span(&d, 0, 104, &x0, &x1);
    CHECK(near(x0, 2 * 104 / 11.0f, 0.01f) && near(x1, 9 * 104 / 11.0f, 0.01f), "Fri 8..15 on 6..17: %.2f..%.2f", x0, x1);
    wx_days_span(&d, 1, 104, &x0, &x1);
    CHECK(near(x0, 3 * 104 / 11.0f, 0.01f) && near(x1, 7 * 104 / 11.0f, 0.01f), "Sat 9..13: %.2f..%.2f", x0, x1);
    wx_days_span(&d, 2, 104, &x0, &x1);
    CHECK(near(x0, 0, 0.01f) && near(x1, 104, 0.01f), "Sun spans the axis: %.2f..%.2f", x0, x1);
    wx_days_span(&d, 2, 60, &x0, &x1);
    CHECK(near(x0, 0, 0.01f) && near(x1, 60, 0.01f), "on a narrower bar: %.2f..%.2f", x0, x1);
    d.row[1].lo = d.row[1].hi = 11;
    wx_days_span(&d, 1, 104, &x0, &x1);
    CHECK(near(x0, 5 * 104 / 11.0f - 5, 0.01f) && near(x1, 5 * 104 / 11.0f + 5, 0.01f), "a flat day: a dot of the bar's height, centred: %.2f..%.2f",
          x0, x1);
    d.row[1].lo = 13;
    d.row[1].hi = 14;
    wx_days_span(&d, 1, 104, &x0, &x1);
    CHECK(near(x1 - x0, WX_DAYS_BAR_H, 0.01f) && near((x0 + x1) / 2, 7.5f * 104 / 11, 0.01f), "a span under 10 px widens about its centre");
    d.row[1].lo = d.row[1].hi = 6;
    wx_days_span(&d, 1, 104, &x0, &x1);
    CHECK(near(x0, 0, 0.01f) && near(x1, 10, 0.01f), "a dot at the axis start stays on the bar: %.2f..%.2f", x0, x1);
    d.row[1].lo = d.row[1].hi = 17;
    wx_days_span(&d, 1, 104, &x0, &x1);
    CHECK(near(x0, 94, 0.01f) && near(x1, 104, 0.01f), "and at its end: %.2f..%.2f", x0, x1);
    d.row[1].lo = 6;
    d.row[1].hi = 7;
    wx_days_span(&d, 1, 104, &x0, &x1);
    CHECK(near(x0, 0, 0.001f) && near(x1, 10, 0.001f), "a short span half a pixel past the start: %.3f..%.3f", x0, x1);
    d.row[1].lo = 16;
    d.row[1].hi = 17;
    wx_days_span(&d, 1, 104, &x0, &x1);
    CHECK(near(x0, 94, 0.001f) && near(x1, 104, 0.001f), "and past the end: %.3f..%.3f", x0, x1);
    wx_obs_t f = fixture();
    f.day_n = 1;
    f.day[0].min_c = f.day[0].max_c = 4;
    wx_days_from(&f, &d);
    wx_days_span(&d, 0, 104, &x0, &x1);
    CHECK(near(x0, 47, 0.01f) && near(x1, 57, 0.01f), "one flat day alone: a centred dot: %.2f..%.2f", x0, x1);
}

static void test_widths(void)
{
    CHECK(wx_days_deg_w(15) == 13 + 21 + 15 && wx_days_deg_w(0) == 24 + 15 && wx_days_deg_w(-5) == 21 + 21 + 15 &&
              wx_days_deg_w(-20) == 21 + 21 + 24 + 15 && wx_days_deg_w(99) == 22 + 22 + 15 && wx_days_deg_w(-99) == 21 + 44 + 15 &&
              wx_days_deg_w(100) == 13 + 24 + 24 + 15,
          "temperature widths (the font test holds them to font_face_36)");
    CHECK(wx_days_name_w(3) > wx_days_name_w(1) && wx_days_name_w(1) > WX_DAYS_DAY_W && wx_days_name_w(5) < WX_DAYS_DAY_W &&
              wx_days_name_w(-4) == wx_days_name_w(3) && wx_days_name_w(10) == wx_days_name_w(3),
          "name widths wrap; Wed the widest");
}

static void test_cols(void)
{
    wx_obs_t o = an1();
    wx_days_t d;
    wx_days_cols_t c;
    wx_days_from(&o, &d);
    wx_days_cols(&d, &c);
    CHECK(c.x0 == 57 && c.day_x == 57 && c.day_w == 60 && c.icon_x == 127 && c.lo_x == 177 && c.lo_w == 54 && c.bar_x == 241 &&
              c.bar_w == 104 && c.hi_x == 355 && c.hi_w == 54 && c.x1 == 409,
          "AN1's columns: %d %d %d %d+%d %d+%d %d+%d", c.day_x, c.day_w, c.icon_x, c.lo_x, c.lo_w, c.bar_x, c.bar_w, c.hi_x, c.hi_w);
    o = fixture();
    wx_days_from(&o, &d);
    wx_days_cols(&d, &c);
    int day = wx_days_name_w(3), lo = wx_days_deg_w(-4);
    CHECK(c.day_w == day && c.lo_w == lo && c.hi_w == 54 && c.bar_w == 104 - (day - 60) - (lo - 54),
          "Wed and -4: wider name and low columns, the bar gives way: %d %d %d", c.day_w, c.lo_w, c.bar_w);
    CHECK(c.x1 - c.x0 == 352 && c.x0 == 57, "the row keeps AN1's width while the bar can give: %d..%d", c.x0, c.x1);
    CHECK(c.icon_x == c.day_x + c.day_w + 10 && c.lo_x == c.icon_x + 50 && c.bar_x == c.lo_x + c.lo_w + 10 && c.hi_x == c.bar_x + c.bar_w + 10 &&
              c.x1 == c.hi_x + c.hi_w,
          "10 px gaps");
    o.day[0].min_c = -25;
    o.day[0].max_c = -12;
    wx_days_from(&o, &d);
    wx_days_cols(&d, &c);
    CHECK(c.lo_w == wx_days_deg_w(-25) && c.hi_w == wx_days_deg_w(-12) && c.bar_w == 104 - (day - 60) - (c.lo_w - 54) - (c.hi_w - 54) &&
              c.x0 == 57 && c.x1 == 409,
          "two-digit frost both ends: wider columns, a shorter bar, the same row: %d..%d bar %d", c.x0, c.x1, c.bar_w);
    int min_bar = 999;
    for (int wd = 0; wd < 7; wd++)
        for (int lo = -99; lo <= 99; lo++) {
            wx_days_t w = {.on = true, .have = true, .n = 2, .amin = -99, .amax = 99};
            w.row[0] = (wx_days_row_t){(signed char)wd, WX_ICON_SUN, -1, (short)lo, (short)lo};
            w.row[1] = (wx_days_row_t){(signed char)(wd + 3), WX_ICON_SUN, -1, -48, -44};
            wx_days_cols(&w, &c);
            if (c.bar_w < min_bar) min_bar = c.bar_w;
            CHECK(c.x0 == 57 && c.x1 == 409, "%s %d: the row keeps 352 px", wx_weekday_name(wd), lo);
        }
    CHECK(min_bar == 34, "the shortest bar: %d px", min_bar);
    o.day_n = 0;
    wx_days_from(&o, &d);
    wx_days_cols(&d, &c);
    CHECK(c.x0 == 57 && c.bar_w == 104, "no rows: AN1's columns");
}

static void test_layout(void)
{
    size_t scene = (size_t)wx_scene_px() * 3;
    wx_obs_t o = an1();
    wx_days_t d;
    wx_days_layout_t l;
    wx_days_from(&o, &d);
    wx_days_layout(&d, &l);
    CHECK(l.n == 3 && l.y[0] == 143 && l.y[2] == 313, "rows");
    CHECK(l.sprite.x0 <= l.c.icon_x - 2 && l.sprite.x1 >= l.c.bar_x + l.c.bar_w + 2 && l.sprite.y0 == 0 &&
              l.sprite.y1 == 2 * WX_DAYS_ROW_UP && (l.sprite.x0 & 1) == 0 && (l.sprite.x1 & 1) == 0,
          "the sprite holds the icon and the bar, even edges: %d..%d", l.sprite.x0, l.sprite.x1);
    int px = (l.sprite.x1 - l.sprite.x0) * (l.sprite.y1 - l.sprite.y0) * 3;
    CHECK(l.at[0] == 0 && l.at[1] == px && l.at[2] == 2 * px && l.total == 3 * px, "sprites back to back");
    CHECK(l.area.x0 <= l.c.x0 - 2 && l.area.x1 >= l.c.x1 + 2 && l.area.y0 == 143 - WX_DAYS_ROW_UP &&
              l.area.y1 == 313 - WX_DAYS_ROW_UP + WX_DAYS_ROW_H + WX_DAYS_SLIDE,
          "the redraw box: every row, the slide");
    CHECK(l.area.y1 < 425 && l.area.y0 > 100, "the box stays clear of the face dots");
    for (int lo = -40; lo <= 45; lo += 5)
        for (int span = 0; span <= 30; span += 10) {
            wx_obs_t w = fixture();
            for (int i = 0; i < 3; i++) {
                w.day[i].min_c = (short)(lo - 10 * i);
                w.day[i].max_c = (short)(lo + span);
            }
            wx_days_from(&w, &d);
            wx_days_layout(&d, &l);
            CHECK((size_t)l.total <= scene, "%d..%d: the sprites (%d B) fit the scene buffer (%zu B)", lo, lo + span, l.total, scene);
            CHECK(l.area.x0 >= 0 && l.area.x1 <= WX_FACE, "%d..%d: the box inside the face", lo, lo + span);
        }
    o.day_n = 0;
    wx_days_from(&o, &d);
    wx_days_layout(&d, &l);
    CHECK(l.n == 0 && l.total == 0 && l.area.x1 == 0, "no rows: nothing to lay out");
    d.n = 7;
    d.have = true;
    wx_days_layout(&d, &l);
    CHECK(l.n == WX_DAYS, "never more rows than the array");
    o = an1();
    o.day_n = 1;
    wx_days_from(&o, &d);
    wx_days_layout(&d, &l);
    CHECK(l.n == 1 && l.y[0] == 228 && l.area.y0 == 228 - WX_DAYS_ROW_UP && l.at[0] == 0, "one row");
}

static void test_arrive(void)
{
    int opa, dy;
    wx_days_arrive(0, 0, &opa, &dy);
    CHECK(opa == 0 && dy == 12, "row 0 at 0 s: hidden, 12 px down");
    wx_days_arrive(0, -1, &opa, &dy);
    CHECK(opa == 0 && dy == 12, "before the start");
    wx_days_arrive(0, 0.42, &opa, &dy);
    CHECK(opa == 255 && dy == 0, "row 0 in at 0.42 s");
    wx_days_arrive(0, 0.21, &opa, &dy);
    CHECK(opa == 223 && dy == 2, "row 0 half way: eased out (%d, %d)", opa, dy);
    wx_days_arrive(1, 0.08, &opa, &dy);
    CHECK(opa == 0 && dy == 12, "row 1 starts 80 ms later");
    wx_days_arrive(1, 0.09, &opa, &dy);
    CHECK(opa > 0 && opa < 30 && dy == 11, "then moves (%d, %d)", opa, dy);
    wx_days_arrive(1, 0.499, &opa, &dy);
    CHECK(opa == 255 && dy == 0, "row 1 in at 0.5 s");
    wx_days_arrive(2, 0.16, &opa, &dy);
    CHECK(opa == 0 && dy == 12, "row 2 starts at 160 ms");
    wx_days_arrive(2, 0.5, &opa, &dy);
    CHECK(opa < 255, "row 2 still fading in at 0.5 s (%d, %d)", opa, dy);
    wx_days_arrive(2, 0.58, &opa, &dy);
    CHECK(opa == 255 && dy == 0, "row 2 in at 0.58 s");
    int last = -1, lastdy = 99;
    bool mono = true;
    for (int k = 0; k <= 40; k++) {
        wx_days_arrive(2, k / 30.0, &opa, &dy);
        mono &= opa >= last && dy <= lastdy;
        last = opa;
        lastdy = dy;
    }
    CHECK(mono, "fades in and rises, never back");
    for (int i = 0; i < 3; i++)
        for (double t = WX_ARRIVE_S; t < 5; t += 0.25) {
            wx_days_arrive(i, t, &opa, &dy);
            CHECK(opa == 255 && dy == 0, "row %d still after the arrival", i);
        }
}

static bool box_in(wx_box_t b, wx_box_t a) { return b.x0 >= a.x0 && b.y0 >= a.y0 && b.x1 <= a.x1 && b.y1 <= a.y1; }

static void test_areas(void)
{
    for (int n = 1; n <= 3; n++) {
        wx_obs_t o = an1();
        o.day_n = n;
        wx_days_t d;
        wx_days_layout_t l;
        wx_days_from(&o, &d);
        wx_days_layout(&d, &l);
        int bw = l.area.x1 - l.area.x0, bh = l.area.y1 - l.area.y0;
        wx_days_frame_t prev, f;
        wx_days_frame(&d, -1, &prev);
        int changed = 0, last = -1;
        bool inside = true;
        for (int k = 0; k <= 30 * 5; k++) {
            wx_days_frame(&d, k / 30.0, &f);
            bool dirty = false;
            for (int i = 0; i < l.n; i++) {
                if (f.opa[i] == prev.opa[i] && f.dy[i] == prev.dy[i] && f.lo[i] == prev.lo[i] && f.hi[i] == prev.hi[i]) continue;
                dirty = true;
                inside &= box_in(wx_days_row_box(&l, i, prev.dy[i]), l.area) && box_in(wx_days_row_box(&l, i, f.dy[i]), l.area);
            }
            if (dirty) {
                changed++;
                last = k;
            }
            prev = f;
        }
        CHECK(inside, "%d rows: every row box, old and new, inside the one redraw box", n);
        CHECK(last >= 0 && last <= (int)ceil(WX_ARRIVE_S * 30), "%d rows: nothing changes after the arrival (last change at frame %d)", n, last);
        CHECK(bw == 362 && bh == WX_DAYS_ROW_H + WX_DAYS_SLIDE + (n - 1) * 85 - (n == 2 ? 1 : 0),
              "%d rows: the box %dx%d", n, bw, bh);
        printf("days: %d rows, 1 area of %dx%d (%d px) on %d of the arrival's frames, then none\n", n, bw, bh, bw * bh, changed);
        wx_box_t r = wx_days_row_box(&l, 0, 0);
        CHECK(r.x0 == l.area.x0 && r.x1 == l.area.x1 && r.y0 == l.y[0] - WX_DAYS_ROW_UP && r.y1 == r.y0 + WX_DAYS_ROW_H,
              "a row is as wide as the box, not the face");
        CHECK(wx_days_row_box(&l, 0, 7).y0 == r.y0 + 7, "the slide moves it down");
        CHECK(l.sprite.x0 >= l.area.x0 && l.sprite.x1 <= l.area.x1 && l.c.x0 >= l.area.x0 && l.c.x1 <= l.area.x1, "the row's content inside it");
    }
    wx_obs_t o = an1();
    wx_days_t d;
    wx_days_from(&o, &d);
    wx_days_frame_t f;
    wx_days_frame(&d, 0, &f);
    CHECK(f.opa[0] == 0 && f.dy[0] == 12 && f.lo[0] == 0 && f.hi[0] == 0, "frame 0: hidden, down, counts at 0");
    wx_days_frame(&d, 10, &f);
    for (int i = 0; i < 3; i++)
        CHECK(f.opa[i] == 255 && f.dy[i] == 0 && f.lo[i] == d.row[i].lo && f.hi[i] == d.row[i].hi, "row %d settled", i);
    wx_days_frame(&d, 0.5, &f);
    CHECK(f.lo[1] == wx_count_value(9, wx_count_step(wx_count_n(9), 0.5)) && f.hi[2] == wx_count_value(17, wx_count_step(wx_count_n(17), 0.5)),
          "the counts on the Temperature face's ease");
}

static uint32_t rgb_of(uint16_t c)
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (r << 19) | (g << 10) | (b << 3);
}

static const uint8_t *s_m;
static const wx_days_layout_t *s_l;

static void sprite_px(int row, float fx, float fy, uint16_t *c, int *a)
{
    int w = s_l->sprite.x1 - s_l->sprite.x0, h = s_l->sprite.y1 - s_l->sprite.y0;
    int x = (int)fx - s_l->sprite.x0, y = (int)fy - (s_l->y[row] - WX_DAYS_ROW_UP);
    const uint8_t *m = s_m + s_l->at[row];
    if (x < 0 || y < 0 || x >= w || y >= h) {
        *c = 0;
        *a = -1;
        return;
    }
    *c = ((const uint16_t *)m)[y * w + x];
    *a = m[(size_t)w * h * 2 + (size_t)(y * w + x)];
}

static int channel_diff(uint32_t a, uint32_t b)
{
    int m = 0;
    for (int s = 0; s <= 16; s += 8) {
        int d = abs((int)((a >> s) & 0xFF) - (int)((b >> s) & 0xFF));
        if (d > m) m = d;
    }
    return m;
}

static void test_paint(void)
{
    static wx_ring_t ring;
    static uint16_t bg[WX_FACE * WX_FACE], lut[WX_RING_LUT];
    size_t scene = (size_t)wx_scene_px() * 3;
    uint8_t *m = calloc(1, scene), *hero = calloc(1, WX_HERO_BOX * WX_HERO_BOX * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .scene = m};
    wx_obs_t o = an1();
    wx_temp_t t;
    wx_temp_face(&o, NULL, WXF_DAYS, &t);
    wx_bake_out_t out = {.hero = {1, 1, 2, 2}, .nclouds = 1};
    memset(bg, 0x55, sizeof bg);
    clock_t t0 = clock();
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    printf("days: host bake %.2f ms\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    CHECK(out.hero.x1 == 0 && out.nclouds == 0, "no hero or cloud sprites");
    CHECK(bg[11 * WX_FACE + 233] == wx_rgb565(0x262626) && bg[(int)(233 + 222) * WX_FACE + 233] == wx_rgb565(0x262626),
          "the grey ring at r 222");
    CHECK(bg[(233 - 222 - 4) * WX_FACE + 233] == 0 && bg[(233 - 222 + 4) * WX_FACE + 233] == 0 && bg[(233 - 222 + 2) * WX_FACE + 233] != 0 &&
              bg[(233 - 222 - 2) * WX_FACE + 233] != 0,
          "6 px wide, not the 12 px gauge");
    CHECK(bg[228 * WX_FACE + 233] == 0 && bg[0] == 0, "black elsewhere: rows are sprites");
    float dx, dy;
    wx_face_dot_xy(WXF_DAYS, WXF_N, &dx, &dy);
    CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_ON), "the Next days dot is white");
    wx_face_dot_xy(WXF_WIND, WXF_N, &dx, &dy);
    CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_OFF), "the others grey");

    wx_days_layout_t l;
    wx_days_layout(&t.days, &l);
    s_m = m;
    s_l = &l;
    uint16_t c;
    int a;
    for (int i = 0; i < 3; i++) {
        float y = (float)l.y[i] + 0.5f, x0, x1;
        wx_days_span(&t.days, i, l.c.bar_w, &x0, &x1);
        x0 += (float)l.c.bar_x;
        x1 += (float)l.c.bar_x;
        sprite_px(i, l.c.bar_x + 1.5f, y, &c, &a);
        bool track_left = x0 > l.c.bar_x + 8;
        if (track_left) CHECK(c == wx_rgb565(0x262626) && a == 255, "row %d: the track left of the span", i);
        sprite_px(i, l.c.bar_x + l.c.bar_w - 1.5f, y, &c, &a);
        if (x1 < l.c.bar_x + l.c.bar_w - 8) CHECK(c == wx_rgb565(0x262626) && a == 255, "row %d: the track right of the span", i);
        sprite_px(i, x0 + 5.5f, y, &c, &a);
        uint32_t want = wx_temp_rgb((float)t.days.row[i].lo + (t.days.row[i].hi - t.days.row[i].lo) * 5.0f / (x1 - x0));
        CHECK(a == 255 && channel_diff(rgb_of(c), want) <= 12, "row %d: the span's left end in the low's colour: %06X, want %06X", i,
              (unsigned)rgb_of(c), (unsigned)want);
        sprite_px(i, x1 - 5.5f, y, &c, &a);
        want = wx_temp_rgb((float)t.days.row[i].hi - (t.days.row[i].hi - t.days.row[i].lo) * 5.0f / (x1 - x0));
        CHECK(a == 255 && channel_diff(rgb_of(c), want) <= 12, "row %d: its right end in the high's colour: %06X, want %06X", i,
              (unsigned)rgb_of(c), (unsigned)want);
        sprite_px(i, (float)l.c.bar_x + 50, y - 6, &c, &a);
        CHECK(a == 0, "row %d: 10 px tall", i);
        sprite_px(i, (float)l.c.bar_x + 50, y + 4, &c, &a);
        CHECK(a > 0, "row %d: 10 px tall, down to its bottom", i);
        sprite_px(i, (float)l.c.lo_x + 20, y, &c, &a);
        CHECK(a == 0, "row %d: the low's column stays clear", i);
    }
    float ix = (float)l.c.icon_x;
    sprite_px(2, ix + 20.5f, l.y[2] - 20 + 20.5f, &c, &a);
    CHECK(a == 255 && channel_diff(rgb_of(c), 0xFFE07A) <= 12, "Sun: the disc's hot centre %06X", (unsigned)rgb_of(c));
    sprite_px(2, ix + 30.5f, l.y[2] - 20 + 20.5f, &c, &a);
    CHECK(a > 240 && channel_diff(rgb_of(c), 0xF08A3A) <= 12, "Sun: its orange rim %06X", (unsigned)rgb_of(c));
    sprite_px(2, ix + 20.5f, l.y[2] - 20 + 3.5f, &c, &a);
    CHECK(a > 128 && channel_diff(rgb_of(c), 0xFFB547) <= 8, "Sun: the top ray");
    sprite_px(2, ix + 20.5f, l.y[2] - 20 + 7.5f, &c, &a);
    CHECK(a == 0, "Sun: rays apart from the disc");
    sprite_px(0, ix + 14.5f, l.y[0] - 20 + 12.5f, &c, &a);
    CHECK(a == 255 && channel_diff(rgb_of(c), 0xFFE07A) <= 40, "Partly: the sun up left");
    sprite_px(0, ix + 21.5f, l.y[0] - 20 + 28.5f, &c, &a);
    CHECK(a == 255 && c == wx_rgb565(0xE6ECF2), "Partly: the white cloud");
    sprite_px(1, ix + 19.5f, l.y[1] - 20 + 15.5f, &c, &a);
    CHECK(a == 255 && c == wx_rgb565(0x8E96A0), "Rain: the grey cloud");
    sprite_px(1, ix + 12.5f, l.y[1] - 20 + 30.5f, &c, &a);
    CHECK(a > 128 && c == wx_rgb565(0x5C9CE0), "Rain: a blue drop");
    for (int i = 0; i < 3; i++) {
        sprite_px(i, ix + 2.5f, l.y[i] - 20 + 38.5f, &c, &a);
        CHECK(a == 0, "row %d: the icon's corner clear", i);
    }

    static const struct {
        int face, x, y;
        uint32_t rgb;
        const char *what;
    } ICON[] = {
        {WX_OVERCAST, 19, 22, 0x8E96A0, "Cloud: grey, low in its square"},
        {WX_OVERCAST, 19, 8, 0, "Cloud: nothing above it"},
        {WX_OVERCAST, 19, 30, 0xA8B0BA, "Cloud: the base line runs on under the middle puff"},
        {WX_RAIN, 19, 24, 0xA8B0BA, "Rain: the base line runs on under the middle puff"},
        {WX_PARTLY_CLOUDY, 21, 33, 0xC4CCD4, "Partly: the base line runs on under the middle puff"},
        {WX_FOG, 20, 29, 0x9AA4B0, "Fog: the first mist line"},
        {WX_FOG, 20, 34, 0x9AA4B0, "Fog: the second"},
        {WX_SNOW, 20, 34, 0xD0DCE8, "Snow: a flake"},
        {WX_STORM, 19, 15, 0x555D68, "Storm: the dark cloud"},
        {WX_STORM, 20, 31, 0xF0D040, "Storm: the bolt"},
        {WX_STORM, 13, 28, 0, "Storm: no drops"},
        {WX_RAIN, 19, 31, 0x5C9CE0, "Rain: the middle drop"},
        {WX_RAIN, 11, 33, 0x5C9CE0, "Rain: the first drop slants left"},
    };
    for (size_t k = 0; k < sizeof ICON / sizeof ICON[0]; k++) {
        o = an1();
        o.day[0].face = (signed char)ICON[k].face;
        wx_temp_face(&o, NULL, WXF_DAYS, &t);
        wx_bake(&t, WX_BAKE_FULL, &b, &out);
        wx_days_layout(&t.days, &l);
        sprite_px(0, (float)l.c.icon_x + ICON[k].x + 0.5f, l.y[0] - 20 + ICON[k].y + 0.5f, &c, &a);
        if (ICON[k].rgb)
            CHECK(a > 128 && channel_diff(rgb_of(c), ICON[k].rgb) <= 8, "%s: %06X alpha %d", ICON[k].what, (unsigned)rgb_of(c), a);
        else
            CHECK(a == 0, "%s: alpha %d", ICON[k].what, a);
    }

    o = an1();
    o.day[1].min_c = o.day[1].max_c = 11;
    wx_temp_face(&o, NULL, WXF_DAYS, &t);
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    wx_days_layout(&t.days, &l);
    float x0, x1;
    wx_days_span(&t.days, 1, l.c.bar_w, &x0, &x1);
    sprite_px(1, l.c.bar_x + (x0 + x1) / 2, l.y[1] + 0.5f, &c, &a);
    CHECK(a == 255 && channel_diff(rgb_of(c), wx_temp_rgb(11)) <= 8, "a flat day: a dot in its colour");
    sprite_px(1, l.c.bar_x + x1 + 1.5f, l.y[1] + 0.5f, &c, &a);
    CHECK(c == wx_rgb565(0x262626), "the track beside the dot");

    o.day_n = 0;
    wx_temp_face(&o, NULL, WXF_DAYS, &t);
    memset(m, 0x77, scene);
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    CHECK(bg[11 * WX_FACE + 233] == wx_rgb565(0x262626) && m[0] == 0x77, "no rows: the ring and dots, no sprite written");
    free(m);
    free(hero);
}

int main(void)
{
    test_weekday();
    test_icons();
    test_from();
    test_ended();
    test_boundaries();
    test_same();
    test_pick();
    test_rows_y();
    test_span();
    test_widths();
    test_cols();
    test_layout();
    test_arrive();
    test_areas();
    test_paint();
    if (failures) {
        printf("days: %d failure(s)\n", failures);
        return 1;
    }
    printf("days: all passed\n");
    return 0;
}
