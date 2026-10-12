#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bot_behavior.h"
#include "bot_shape.h"
#include "weather_face.h"
#include "wx_moment.h"
#include "wx_paint.h"
#include "wx_temp.h"
#include "wx_wind.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NOW 1782043200LL
#define F (1.0 / WXM_FPS)

static wx_obs_t obs(const char *code, float temp)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = temp, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .moon_up = -1, .observed_at = NOW - 300, .has_obs_age = true, .obs_age_s = 300,
                  .has_utc_offset = true, .utc_offset = 3600, .has_now_rain = true, .has_rain = true, .has_night = true,
                  .has_wind = true, .wind_kmh = 10};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "clouds");
    snprintf(o.code, sizeof o.code, "%s", code);
    return o;
}

static wx_obs_t rain(int intensity, const char *code)
{
    wx_obs_t o = obs(code, 9);
    o.now_rain = intensity;
    return o;
}

static const char *const KIND[WXM_KINDS] = {"none", "glance", "night", "frost", "heat", "wind", "afterrain", "snow", "rain", "storm"};

static wxm_sig_t sig(const wx_obs_t *o, const wxm_sig_t *prev)
{
    wxm_sig_t s;
    wxm_sig_from(o, prev, &s);
    return s;
}

static void test_sig(void)
{
    wx_obs_t o = obs("2", 12);
    wxm_sig_t s = sig(&o, NULL);
    CHECK(s.have && s.precip == 0 && !s.hot && !s.frost && !s.windy, "dry mild calm");
    o = rain(2, "61");
    CHECK(sig(&o, NULL).precip == 1, "rain");
    o = rain(1, "71");
    CHECK(sig(&o, NULL).precip == 2, "snow");
    o = obs("95", 20);
    CHECK(sig(&o, NULL).precip == 3, "storm, even dry now");
    o = obs("61", 9);
    CHECK(sig(&o, NULL).precip == 0, "a rain code without rain now or a spell: dry (wx_rain_from)");

    o = obs("0", 28);
    CHECK(sig(&o, NULL).hot, "28 is hot");
    o.temp_c = 27.4f;
    CHECK(!sig(&o, NULL).hot, "27 is not");
    wxm_sig_t hot = {.have = true, .hot = true};
    o.temp_c = 26;
    CHECK(sig(&o, &hot).hot, "kept down to 26");
    o.temp_c = 25.4f;
    CHECK(!sig(&o, &hot).hot, "25 drops it");
    wxm_sig_t gone = {.have = false, .hot = true};
    o.temp_c = 27;
    CHECK(!sig(&o, &gone).hot, "a previous without data keeps nothing");

    o = obs("0", 0);
    CHECK(sig(&o, NULL).frost, "0 is frost");
    o.temp_c = 0.6f;
    CHECK(!sig(&o, NULL).frost, "1 is not");
    wxm_sig_t frost = {.have = true, .frost = true};
    o.temp_c = 2;
    CHECK(sig(&o, &frost).frost, "kept up to 2");
    o.temp_c = 2.6f;
    CHECK(!sig(&o, &frost).frost, "3 drops it");

    o = obs("2", 12);
    o.wind_kmh = WX_WIND_AUTO_KMH;
    CHECK(sig(&o, NULL).windy, "the Wind face's auto speed");
    o.wind_kmh = WX_WIND_AUTO_KMH - 1;
    CHECK(!sig(&o, NULL).windy, "below it");
    wxm_sig_t windy = {.have = true, .windy = true};
    o.wind_kmh = WX_WIND_KEEP_KMH;
    CHECK(sig(&o, &windy).windy, "kept to the keep speed");
    o.wind_kmh = WX_WIND_KEEP_KMH - 1;
    CHECK(!sig(&o, &windy).windy, "below the keep speed");

    o = obs("2", 12);
    o.stale = true;
    CHECK(!sig(&o, NULL).have, "stale");
    o = obs("2", 12);
    o.age_s = WX_MAX_AGE_S + 1;
    CHECK(!sig(&o, NULL).have, "old");
    o = obs("2", 12);
    o.has_temp = false;
    CHECK(!sig(&o, NULL).have, "no temperature");
    o = obs("2", 12);
    o.enabled = false;
    CHECK(!sig(&o, NULL).have, "weather off");
    CHECK(!sig(NULL, NULL).have, "no data");
}

static void test_change(void)
{
    wxm_sig_t dry = {.have = true}, wet = {.have = true, .precip = 1}, snow = {.have = true, .precip = 2},
              storm = {.have = true, .precip = 3}, none = {0};
    CHECK(wxm_change(&dry, &wet) == WXM_RAIN, "rain starts");
    CHECK(wxm_change(&dry, &snow) == WXM_SNOW, "snow starts");
    CHECK(wxm_change(&dry, &storm) == WXM_STORM, "storm");
    CHECK(wxm_change(&wet, &storm) == WXM_STORM, "rain to storm");
    CHECK(wxm_change(&storm, &wet) == WXM_RAIN, "storm calms to rain");
    CHECK(wxm_change(&snow, &wet) == WXM_RAIN, "snow to rain");
    CHECK(wxm_change(&wet, &dry) == WXM_AFTERRAIN, "rain stops");
    CHECK(wxm_change(&storm, &dry) == WXM_AFTERRAIN, "storm stops");
    CHECK(wxm_change(&snow, &dry) == WXM_NONE, "snow stopping has no moment");
    CHECK(wxm_change(&wet, &wet) == WXM_NONE && wxm_change(&dry, &dry) == WXM_NONE, "no change");
    CHECK(wxm_change(&none, &wet) == WXM_NONE && wxm_change(&wet, &none) == WXM_NONE, "without data");
    wxm_sig_t w = dry, h = dry, f = dry;
    w.windy = h.hot = f.frost = true;
    CHECK(wxm_change(&dry, &w) == WXM_WIND && wxm_change(&dry, &h) == WXM_HEAT && wxm_change(&dry, &f) == WXM_FROST, "rising edges");
    CHECK(wxm_change(&w, &dry) == WXM_NONE && wxm_change(&h, &dry) == WXM_NONE && wxm_change(&f, &dry) == WXM_NONE, "falling edges: none");
    CHECK(wxm_change(&w, &w) == WXM_NONE && wxm_change(&h, &h) == WXM_NONE && wxm_change(&f, &f) == WXM_NONE, "staying windy, hot, frosty: none");
    wxm_sig_t all = {.have = true, .precip = 1, .windy = true, .hot = true, .frost = true};
    CHECK(wxm_change(&dry, &all) == WXM_RAIN, "rain outranks wind, heat, frost");
    all.precip = 0;
    CHECK(wxm_change(&dry, &all) == WXM_WIND, "wind outranks heat and frost");
    all.windy = false;
    CHECK(wxm_change(&dry, &all) == WXM_HEAT, "heat outranks frost");
    wxm_sig_t wet_windy = {.have = true, .windy = true};
    CHECK(wxm_change(&wet, &wet_windy) == WXM_AFTERRAIN, "rain stopping outranks a rising wind");
    wxm_sig_t snow_windy = {.have = true, .precip = 2, .windy = true};
    CHECK(wxm_change(&dry, &snow_windy) == WXM_SNOW, "snow outranks wind");
    CHECK(WXM_STORM > WXM_RAIN && WXM_RAIN > WXM_SNOW && WXM_SNOW > WXM_AFTERRAIN && WXM_AFTERRAIN > WXM_WIND &&
              WXM_WIND > WXM_HEAT && WXM_HEAT > WXM_FROST && WXM_FROST > WXM_NIGHT,
          "rank order");

    CHECK(wxm_holds(WXM_RAIN, &wet) && !wxm_holds(WXM_RAIN, &snow) && !wxm_holds(WXM_RAIN, &dry), "rain holds");
    CHECK(wxm_holds(WXM_SNOW, &snow) && !wxm_holds(WXM_SNOW, &wet), "snow holds");
    CHECK(wxm_holds(WXM_STORM, &storm) && !wxm_holds(WXM_STORM, &wet), "storm holds");
    CHECK(wxm_holds(WXM_AFTERRAIN, &dry) && !wxm_holds(WXM_AFTERRAIN, &wet), "after rain holds while dry");
    CHECK(wxm_holds(WXM_WIND, &w) && !wxm_holds(WXM_WIND, &dry), "wind holds");
    CHECK(wxm_holds(WXM_HEAT, &h) && !wxm_holds(WXM_HEAT, &dry), "heat holds");
    CHECK(wxm_holds(WXM_FROST, &f) && !wxm_holds(WXM_FROST, &dry), "frost holds");
    CHECK(!wxm_holds(WXM_RAIN, &none) && !wxm_holds(WXM_GLANCE, &dry) && !wxm_holds(WXM_NONE, &dry), "no data, periodic kinds");
}

static wx_temp_t s_scratch;

static wxm_moment_t moment(wxm_kind_t k, const wx_obs_t *o, bool quiet)
{
    wxm_moment_t m;
    CHECK(wxm_moment_from(k, o, quiet, &s_scratch, &m), "%s: from", KIND[k]);
    return m;
}

static void test_now_kind(void)
{
    wx_obs_t o = rain(2, "61");
    CHECK(wxm_now_kind(&o, &s_scratch) == WXM_RAIN, "raining");
    o = rain(1, "73");
    CHECK(wxm_now_kind(&o, &s_scratch) == WXM_SNOW, "snowing");
    o = obs("95", 20);
    CHECK(wxm_now_kind(&o, &s_scratch) == WXM_STORM, "storm");
    o = obs("0", 9);
    o.night = true;
    o.has_cloud = true;
    o.cloud = 9;
    CHECK(wxm_now_kind(&o, &s_scratch) == WXM_NIGHT, "clear night");
    o.cloud = 39;
    CHECK(wxm_now_kind(&o, &s_scratch) == WXM_NIGHT, "mostly clear night");
    o.cloud = 40;
    CHECK(wxm_now_kind(&o, &s_scratch) == WXM_GLANCE, "partly cloudy night");
    o.night = false;
    o.cloud = 0;
    CHECK(wxm_now_kind(&o, &s_scratch) == WXM_GLANCE, "clear day");
    o.stale = true;
    CHECK(wxm_now_kind(&o, &s_scratch) == WXM_NONE, "stale");
    CHECK(wxm_now_kind(NULL, &s_scratch) == WXM_NONE, "no data");
}

static void test_text(void)
{
    wx_obs_t o = obs("2", 12);
    wxm_moment_t m = moment(WXM_GLANCE, &o, false);
    CHECK(strcmp(m.text, "12\xC2\xB0 Partly cloudy") == 0 && m.rgb == wx_temp_rgb(12) && m.sun && !m.quiet, "glance: %s", m.text);
    o.has_cloud = true;
    o.cloud = 75;
    m = moment(WXM_GLANCE, &o, true);
    CHECK(strcmp(m.text, "12\xC2\xB0 Cloudy") == 0 && m.sun && m.quiet, "cloudy, quiet: %s", m.text);
    o.cloud = 95;
    m = moment(WXM_GLANCE, &o, false);
    CHECK(strcmp(m.text, "12\xC2\xB0 Overcast") == 0 && !m.sun, "overcast: no sun: %s", m.text);
    o = obs("45", 6);
    m = moment(WXM_GLANCE, &o, false);
    CHECK(strcmp(m.text, "6\xC2\xB0 Fog") == 0 && !m.sun, "fog: no sun: %s", m.text);
    o.has_cloud = true;
    o.cloud = 20;
    CHECK(!moment(WXM_GLANCE, &o, false).sun, "fog under a thin cloud cover: no sun either");
    o = obs("2", 12);
    o.night = true;
    m = moment(WXM_GLANCE, &o, false);
    CHECK(!m.sun, "night: no sun");
    o = obs("2", 12.4f);
    o.uv = 9;
    o.has_uv = true;
    m = moment(WXM_GLANCE, &o, false);
    CHECK(strcmp(m.text, "12\xC2\xB0 Partly cloudy") == 0, "the sky word, not the UV advice: %s", m.text);

    o = obs("0", 9);
    o.night = true;
    o.has_moon = true;
    o.moon_up = 1;
    o.moon_phase = 30;
    o.moon_illum = 61;
    m = moment(WXM_NIGHT, &o, false);
    CHECK(strcmp(m.text, "Clear 9\xC2\xB0") == 0 && m.moon && m.moon_phase == 30 && m.moon_illum == 60 && m.rgb == wx_temp_rgb(9),
          "night with the moon up: %s %d %d", m.text, m.moon_phase, m.moon_illum);
    o.has_cloud = true;
    o.cloud = 75;
    CHECK(moment(WXM_NIGHT, &o, false).moon, "the moon up behind light cloud (the small moon): moon");
    o.has_cloud = false;
    o.moon_up = 0;
    CHECK(!moment(WXM_NIGHT, &o, false).moon, "moon below the horizon: no moon");
    o.moon_up = -1;
    CHECK(!moment(WXM_NIGHT, &o, false).moon, "moon unknown: no moon");
    o.moon_up = 1;
    o.has_moon = false;
    CHECK(!moment(WXM_NIGHT, &o, false).moon, "no moon block: no moon");

    o = obs("0", -3.4f);
    m = moment(WXM_FROST, &o, false);
    CHECK(strcmp(m.text, "Frost -3\xC2\xB0") == 0 && m.rgb == wx_temp_rgb(-3) && m.sun, "frost: %s", m.text);
    o = obs("0", 31);
    m = moment(WXM_HEAT, &o, false);
    CHECK(strcmp(m.text, "Hot 31\xC2\xB0") == 0 && m.rgb == wx_temp_rgb(31) && !m.sun, "heat: %s", m.text);
    o = rain(2, "73");
    o.temp_c = -2;
    m = moment(WXM_SNOW, &o, false);
    CHECK(strcmp(m.text, "Snow -2\xC2\xB0") == 0 && m.rgb == wx_temp_rgb(-2), "snow: %s", m.text);
    o = obs("2", 12);
    o.wind_kmh = 45;
    m = moment(WXM_WIND, &o, false);
    CHECK(strcmp(m.text, "Wind 45 km/h") == 0 && m.rgb == wx_wind_rgb(45), "wind: %s", m.text);
    o.wind_kmh = 5000;
    CHECK(strcmp(moment(WXM_WIND, &o, false).text, "Wind 999 km/h") == 0, "wind clamps");
    o.has_wind = false;
    CHECK(strcmp(moment(WXM_WIND, &o, false).text, "Wind 0 km/h") == 0, "no wind: 0");

    o = rain(2, "61");
    o.rain_until = NOW + 3 * 3600 + 1800;
    m = moment(WXM_RAIN, &o, false);
    CHECK(strcmp(m.text, "Rain until 16:30") == 0 && m.rgb == WXM_BLUE, "rain until (UTC+1): %s", m.text);
    o.rain_until = 0;
    CHECK(strcmp(moment(WXM_RAIN, &o, false).text, "Rain") == 0, "rain without an end");
    o = rain(3, "65");
    CHECK(strcmp(moment(WXM_RAIN, &o, false).text, "Heavy rain") == 0, "heavy rain");
    o = rain(1, "73");
    CHECK(strcmp(moment(WXM_RAIN, &o, false).text, "Rain") == 0, "a rain moment on snow says Rain");
    o = obs("2", 12);
    m = moment(WXM_AFTERRAIN, &o, false);
    CHECK(strcmp(m.text, "Rain stops") == 0 && m.rgb == WXM_BLUE, "after rain");
    m = moment(WXM_STORM, &o, false);
    CHECK(strcmp(m.text, "Storm") == 0 && m.rgb == WXM_STORM_RGB, "storm");

    wxm_moment_t z;
    o.stale = true;
    CHECK(!wxm_moment_from(WXM_GLANCE, &o, false, &s_scratch, &z), "stale: none");
    o = obs("2", 12);
    CHECK(!wxm_moment_from(WXM_NONE, &o, false, &s_scratch, &z) && !wxm_moment_from(WXM_KINDS, &o, false, &s_scratch, &z), "bad kinds");
}

typedef struct {
    wxm_trigger_t tr;
    wxm_in_t in;
    wx_obs_t o;
    wxm_moment_t m;
} rig_t;

static rig_t s_rig;

static void rig(double every_s)
{
    wxm_init(&s_rig.tr);
    s_rig.o = obs("2", 12);
    s_rig.in = (wxm_in_t){.t = 0, .obs = &s_rig.o, .mood = WXM_REST, .shown = true, .on = true, .every_s = (int)every_s};
}

static int run(double until, double dt, wxm_kind_t *last, double *at)
{
    int n = 0;
    for (; s_rig.in.t < until; s_rig.in.t += dt) {
        if (wxm_tick(&s_rig.tr, &s_rig.in, &s_rig.m)) {
            wxm_shown(&s_rig.tr, &s_rig.m, s_rig.in.t);
            n++;
            if (last) *last = s_rig.m.kind;
            if (at) *at = s_rig.in.t;
        }
    }
    return n;
}

static void test_trigger(void)
{
    wxm_kind_t k = WXM_NONE;
    double at = -1;
    rig(1800);
    CHECK(run(1799, 1, NULL, NULL) == 0, "no moment for the first data, none before the interval");
    CHECK(run(1802, 1, &k, &at) == 1 && k == WXM_GLANCE && at == 1800, "periodic glance at 30 min: %s at %.0f", KIND[k], at);
    CHECK(run(1800 + 1801, 1, NULL, NULL) == 0, "the next one is unchanged: skipped");
    s_rig.o.temp_c = 13;
    CHECK(run(1800 * 3 + 2, 1, &k, &at) == 1 && at == 5400, "changed text: shown at 90 min (%.0f)", at);
    CHECK(strcmp(s_rig.m.text, "13\xC2\xB0 Partly cloudy") == 0, "its text");

    rig(1800);
    run(10, 1, NULL, NULL);
    s_rig.o = rain(2, "61");
    CHECK(run(11, 1, &k, &at) == 1 && k == WXM_RAIN && at == 10, "rain starts: at once (%.0f)", at);
    s_rig.o = obs("2", 12);
    CHECK(run(10 + WXM_GAP_S, 1, NULL, NULL) == 0, "rain stops: waits for the 5 min gap");
    CHECK(run(10 + WXM_GAP_S + 2, 1, &k, &at) == 1 && k == WXM_AFTERRAIN && at == 10 + WXM_GAP_S, "then after rain (%.0f)", at);
    s_rig.o = rain(2, "61");
    CHECK(run(10 + WXM_SAME_S, 1, NULL, NULL) == 0, "rain again: the same kind waits 30 min");
    CHECK(run(10 + WXM_SAME_S + 2, 1, &k, &at) == 1 && k == WXM_RAIN && at == 10 + WXM_SAME_S, "then rain (%.0f)", at);

    rig(0);
    run(10, 1, NULL, NULL);
    s_rig.in.mood = WXM_BUSY;
    s_rig.o.wind_kmh = 50;
    CHECK(run(600, 1, NULL, NULL) == 0, "waiting: the change waits");
    s_rig.in.mood = WXM_WORKING;
    CHECK(run(600 + WXM_SETTLE_S - 1, 1, NULL, NULL) == 0, "the mood settles first");
    CHECK(run(605, 1, &k, &at) == 1 && k == WXM_WIND && s_rig.m.quiet && at == 600 + WXM_SETTLE_S, "then wind, quiet (%.0f)", at);

    rig(0);
    run(10, 1, NULL, NULL);
    s_rig.in.mood = WXM_BUSY;
    s_rig.o.wind_kmh = 50;
    CHECK(run(10 + WXM_WAIT_S + 1, 1, NULL, NULL) == 0, "busy past the wait");
    s_rig.in.mood = WXM_REST;
    CHECK(run(10 + WXM_WAIT_S + 100, 1, NULL, NULL) == 0, "a change older than 30 min is dropped");

    rig(0);
    run(10, 1, NULL, NULL);
    s_rig.in.shown = false;
    s_rig.o = rain(2, "61");
    run(20, 1, NULL, NULL);
    s_rig.o = obs("2", 12);
    run(30, 1, NULL, NULL);
    s_rig.o = rain(2, "61");
    run(40, 1, NULL, NULL);
    s_rig.o.temp_c = 31;
    run(50, 1, NULL, NULL);
    s_rig.in.shown = true;
    CHECK(run(50 + WXM_SETTLE_S + 1, 1, &k, &at) == 1 && k == WXM_RAIN && at == 50 + WXM_SETTLE_S,
          "off the page: the highest queued change that still holds (%s), once the page settled (%.0f)", KIND[k], at);
    rig(0);
    run(10, 1, NULL, NULL);
    s_rig.in.shown = false;
    s_rig.o = rain(2, "61");
    run(20, 1, NULL, NULL);
    s_rig.o = obs("2", 31);
    run(30, 1, NULL, NULL);
    s_rig.in.shown = true;
    CHECK(run(40, 1, &k, NULL) == 1 && k == WXM_AFTERRAIN, "rain came and went: after rain (%s)", KIND[k]);
    rig(0);
    run(10, 1, NULL, NULL);
    s_rig.in.shown = false;
    s_rig.o.temp_c = 31;
    run(20, 1, NULL, NULL);
    s_rig.o.temp_c = 12;
    run(30, 1, NULL, NULL);
    s_rig.in.shown = true;
    CHECK(run(100, 1, NULL, NULL) == 0, "heat gone before it showed: dropped");
    rig(0);
    run(10, 1, NULL, NULL);
    s_rig.in.shown = false;
    s_rig.o = rain(2, "61");
    run(20, 1, NULL, NULL);
    s_rig.o.temp_c = 31;
    s_rig.o.wind_kmh = 50;
    run(30, 1, NULL, NULL);
    s_rig.in.shown = true;
    CHECK(run(40, 1, &k, NULL) == 1 && k == WXM_RAIN, "a lower change does not replace a higher one that holds (%s)", KIND[k]);

    rig(1800);
    run(10, 1, NULL, NULL);
    s_rig.in.blocked = true;
    s_rig.o = rain(2, "61");
    CHECK(run(100, 1, NULL, NULL) == 0, "blocked (chase, quiet hours, a moment playing)");
    s_rig.in.blocked = false;
    CHECK(run(102, 1, &k, NULL) == 1 && k == WXM_RAIN, "unblocked: plays");
    s_rig.in.on = false;
    s_rig.o = obs("2", 12);
    CHECK(run(4000, 1, NULL, NULL) == 0, "moments off");
    s_rig.in.on = true;
    s_rig.o.temp_c = 14;
    CHECK(run(4000 + 1799, 1, NULL, NULL) == 0, "turned on again: a full interval first");
    CHECK(run(4000 + 1801, 1, NULL, NULL) == 1, "then the glance");
    s_rig.o.stale = true;
    s_rig.o.temp_c = 15;
    CHECK(run(12000, 1, NULL, NULL) == 0, "stale data: nothing");

    rig(1800);
    run(1000, 1, NULL, NULL);
    s_rig.in.shown = false;
    run(3000, 1, NULL, NULL);
    s_rig.in.shown = true;
    CHECK(run(3000 + 1799, 1, NULL, NULL) == 0, "off the page: the periodic one waits a full interval after coming back");
    CHECK(run(3000 + 1801, 1, &k, NULL) == 1 && k == WXM_GLANCE, "then the glance");
    rig(1800);
    run(1000, 1, NULL, NULL);
    s_rig.in.mood = WXM_BUSY;
    run(1900, 1, NULL, NULL);
    s_rig.in.mood = WXM_REST;
    CHECK(run(1900 + 1799, 1, NULL, NULL) == 0, "busy at the slot: dropped, the next a full interval on");
    CHECK(run(1900 + 1801, 1, NULL, NULL) == 1, "then shown");
    rig(1800);
    s_rig.in.every_s = 0;
    CHECK(run(4000, 1, NULL, NULL) == 0, "every 0: no periodic moment");
    s_rig.in.every_s = 600;
    CHECK(run(4599, 1, NULL, NULL) == 0 && run(4601, 1, NULL, NULL) == 1, "a new interval counts from when it is set");

    rig(1800);
    run(10, 1, NULL, NULL);
    s_rig.o = rain(2, "61");
    run(12, 1, NULL, NULL);
    CHECK(run(1813, 1, NULL, NULL) == 0, "the periodic one after a rain moment with the same text: skipped");
    s_rig.o.rain_until = NOW + 7200;
    CHECK(run(1800 * 2 + 14, 1, &k, NULL) == 1 && k == WXM_RAIN && strstr(s_rig.m.text, "until"), "with an end time: shown");

    rig(1800);
    run(10, 1, NULL, NULL);
    s_rig.o = rain(2, "61");
    for (int i = 0; i < 3; i++, s_rig.in.t += 1)
        CHECK(wxm_tick(&s_rig.tr, &s_rig.in, &s_rig.m) && s_rig.m.kind == WXM_RAIN, "a change not marked shown (no arena) is offered again: %d", i);
    wxm_shown(&s_rig.tr, &s_rig.m, s_rig.in.t);
    CHECK(run(s_rig.in.t + WXM_GAP_S, 1, NULL, NULL) == 0, "once shown it counts");
    rig(1800);
    run(1800, 1, NULL, NULL);
    for (int i = 0; i < 3; i++, s_rig.in.t += 1)
        CHECK(wxm_tick(&s_rig.tr, &s_rig.in, &s_rig.m) && s_rig.m.kind == WXM_GLANCE, "a periodic moment not marked shown is offered again: %d", i);
    rig(1800);
    run(1799, 1, NULL, NULL);
    s_rig.in.bot_mood = 5;
    CHECK(run(1799 + WXM_SETTLE_S, 1, NULL, NULL) == 0, "idle to done (both resting) just before the slot: the mood settles 3 s first");
    CHECK(run(1799 + WXM_SETTLE_S + 2, 1, &k, &at) == 1 && at == 1799 + WXM_SETTLE_S, "then the due glance (%.0f)", at);

    rig(300);
    run(10, 1, NULL, NULL);
    s_rig.o = rain(2, "61");
    s_rig.o.rain_until = NOW + 3600;
    CHECK(run(11, 1, &k, NULL) == 1 && k == WXM_RAIN, "rain starts");
    s_rig.o.rain_until = NOW + 5400;
    CHECK(run(10 + WXM_SAME_S, 1, NULL, NULL) == 0, "a periodic rain with a new end time waits out the same-kind 30 min");
    CHECK(run(10 + WXM_SAME_S + 301, 1, &k, &at) == 1 && k == WXM_RAIN && at == 10 + WXM_SAME_S, "then it shows (%.0f)", at);

    rig(1800);
    run(100, 1, NULL, NULL);
    s_rig.in.every_s = 60;
    CHECK(run(160, 1, NULL, NULL) == 0 && run(162, 1, &k, &at) == 1 && at == 160, "a shorter interval counts from the change (%.0f)", at);
    rig(60);
    run(50, 1, NULL, NULL);
    s_rig.in.every_s = 14400;
    CHECK(run(14449, 1, NULL, NULL) == 0, "a longer one too: the old slot is dropped");
    CHECK(run(14452, 1, NULL, NULL) == 1, "and the new one comes");

    rig(1800);
    s_rig.o = obs("0", 9);
    s_rig.o.night = true;
    CHECK(run(1802, 1, &k, NULL) == 1 && k == WXM_NIGHT, "periodic at a clear night: night");
}

static void test_frames(void)
{
    wx_obs_t o = obs("2", 12);
    wxm_moment_t m = moment(WXM_GLANCE, &o, false);
    wxm_layout_t l;
    wxm_layout(&m, &l);
    wxm_frame_t f;
    static const int OPA[WXM_FRAMES] = {
        0,   0,   0,   43,  85,  128, 170, 213, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 213, 170, 128, 85,  43,  0,   0,   0,   0,
    };
    static const int DY[WXM_FRAMES] = {7, 7, 7, 6, 5, 4, 2, 1, [8 ... 58] = 0, 1, 2, 4, 5, 6, 7, 7, 7, 7};
    for (int k = 0; k < WXM_FRAMES; k++) {
        wxm_frame(&m, &l, k, &f);
        CHECK(f.opa == OPA[k] && f.dy == DY[k], "frame %d: sprite %d / %d px, want %d / %d", k, f.opa, f.dy, OPA[k], DY[k]);
        int text = k == 0 || k == 66 ? 87 : k == 1 || k == 65 ? 171 : k == 67 ? 0 : 255;
        CHECK(f.text_opa == text, "frame %d: text %d, want %d", k, f.text_opa, text);
        CHECK(!f.icon_off, "full: the icon never steps aside");
    }
    m.quiet = true;
    for (int k = 0; k < WXM_FRAMES; k++) {
        wxm_frame(&m, &l, k, &f);
        CHECK(f.icon_off == (k < 67), "quiet frame %d: icon off %d", k, f.icon_off);
    }
    wxm_frame(&m, &l, WXM_FRAMES, &f);
    CHECK(!f.opa && !f.text_opa && !f.icon_off, "after the last frame: nothing");
    wxm_frame(&m, &l, -1, &f);
    CHECK(!f.opa && !f.text_opa && !f.icon_off, "before the first: nothing");
}

static const wxm_kind_t ALL[] = {WXM_GLANCE, WXM_NIGHT, WXM_FROST, WXM_HEAT, WXM_WIND, WXM_AFTERRAIN, WXM_SNOW, WXM_RAIN, WXM_STORM};
#define N_ALL ((int)(sizeof ALL / sizeof ALL[0]))

static wxm_moment_t variant(wxm_kind_t k, int v)
{
    wxm_moment_t m = {.kind = k, .quiet = v & 1, .sun = (v & 2) != 0, .moon = (v & 2) != 0, .moon_phase = 30, .moon_illum = 60};
    snprintf(m.text, sizeof m.text, "x");
    return m;
}

static void test_layout(void)
{
    for (int a = 0; a < N_ALL; a++)
        for (int v = 0; v < 4; v++) {
            wxm_moment_t m = variant(ALL[a], v);
            wxm_layout_t l;
            wxm_layout(&m, &l);
            CHECK(l.nl >= 1 && l.nl <= WXM_LAYERS && l.ne >= l.nl && l.ne <= WXM_ELEMS, "%s: %d layers, %d elements", KIND[m.kind], l.nl, l.ne);
            CHECK(l.total <= WXM_ARENA, "%s: %d B over the arena", KIND[m.kind], l.total);
            int end = 0;
            for (int i = 0; i < l.nl; i++) {
                const wxm_layer_t *y = &l.layer[i];
                CHECK(y->at == end && y->w > 0 && y->h > 0, "%s layer %d packed", KIND[m.kind], i);
                end += y->w * y->h * 3;
                CHECK(y->x >= 0 && y->y >= 0 && y->x + y->w <= WXM_BOX_W && y->y + y->h <= WXM_BOX_H, "%s layer %d inside the box", KIND[m.kind], i);
            }
            CHECK(end == l.total, "%s: total", KIND[m.kind]);
            bool used[WXM_LAYERS] = {0};
            for (int e = 0; e < l.ne; e++) {
                CHECK(l.el[e].layer >= 0 && l.el[e].layer < l.nl, "%s element %d layer", KIND[m.kind], e);
                used[l.el[e].layer] = true;
            }
            for (int i = 0; i < l.nl; i++) CHECK(used[i], "%s layer %d drawn", KIND[m.kind], i);
        }
    wxm_moment_t m = variant(WXM_RAIN, 0);
    wxm_layout_t l;
    wxm_layout(&m, &l);
    CHECK(l.ne == 7 && l.el[1].layer == l.el[0].layer && l.el[3].layer == l.el[0].layer, "rain: four drops share one sprite");
    CHECK(l.el[1].x - l.el[0].x == (int)lroundf(130 * WXM_K), "drop 2 sits 130 design px right: %d", l.el[1].x - l.el[0].x);
    m = variant(WXM_GLANCE, 0);
    wxm_layout(&m, &l);
    CHECK(l.nl == 1, "glance without the sun: the cloud alone");
    m = variant(WXM_NIGHT, 0);
    wxm_layout(&m, &l);
    CHECK(l.nl == 2 && l.ne == 2, "night without the moon: two stars");
    m = variant(WXM_NIGHT, 2);
    wxm_layout(&m, &l);
    CHECK(l.nl == 3 && l.ne == 3, "night with the moon");
}

typedef struct {
    int sprite_frames, text_frames, max_px;
} areas_t;

static areas_t areas_of(const wxm_moment_t *m, const wxm_layout_t *l, const int *seq, int n)
{
    areas_t a = {0};
    wxm_frame_t prev, f;
    bool any = false;
    for (int i = 0; i < n; i++) {
        wxm_frame(m, l, seq[i], &f);
        int d = wxm_diff(l, any ? &prev : NULL, &f);
        CHECK(d != (WXM_DIRTY_SPRITE | WXM_DIRTY_TEXT), "%s%s: frame %d redraws the sprite and the text", KIND[m->kind], m->quiet ? " quiet" : "", seq[i]);
        int px = 0;
        if (d & WXM_DIRTY_SPRITE) a.sprite_frames++, px += WXM_BOX_W * WXM_BOX_H;
        if (d & WXM_DIRTY_TEXT) a.text_frames++, px += WXM_TEXT_W * WXM_TEXT_H;
        if (px > a.max_px) a.max_px = px;
        prev = f;
        any = true;
    }
    return a;
}

static void test_areas(void)
{
    int seq[WXM_FRAMES];
    for (int k = 0; k < WXM_FRAMES; k++) seq[k] = k;
    printf("moment areas per kind (frames redrawing the sprite box / the text line, of %d):", WXM_FRAMES);
    for (int a = 0; a < N_ALL; a++)
        for (int v = 0; v < 4; v++) {
            wxm_moment_t m = variant(ALL[a], v);
            wxm_layout_t l;
            wxm_layout(&m, &l);
            areas_t r = areas_of(&m, &l, seq, WXM_FRAMES);
            CHECK(r.text_frames == 6, "%s: the text redraws on its 6 fade frames: %d", KIND[m.kind], r.text_frames);
            CHECK(r.sprite_frames >= 12 && r.sprite_frames <= WXM_SPRITE_OUT - WXM_SPRITE_FROM, "%s: sprite frames %d", KIND[m.kind], r.sprite_frames);
            if (v == 0 || (v == 2 && (m.kind == WXM_GLANCE || m.kind == WXM_NIGHT))) printf(" %s%s %d/%d", KIND[m.kind], v ? "+" : "", r.sprite_frames, r.text_frames);
            for (int k0 = 0; k0 < WXM_FRAMES; k0++) {
                int cut[WXM_FRAMES + 1], n = 0;
                for (int k = 0; k <= k0; k++) cut[n++] = k;
                for (int k = wxm_abort_frame(k0) + (wxm_abort_frame(k0) == k0); k < WXM_FRAMES; k++) cut[n++] = k;
                areas_of(&m, &l, cut, n);
            }
        }
    printf("\n");
    wxm_moment_t m = variant(WXM_HEAT, 0);
    wxm_layout_t l;
    wxm_layout(&m, &l);
    wxm_frame_t a, b;
    wxm_frame(&m, &l, 30, &a);
    b = a;
    CHECK(wxm_diff(&l, &a, &b) == 0, "the same frame: nothing");
    b.el[0].opa++;
    CHECK(wxm_diff(&l, &a, &b) == WXM_DIRTY_SPRITE, "an element's opacity");
    b = a;
    b.el[1].x++;
    CHECK(wxm_diff(&l, &a, &b) == WXM_DIRTY_SPRITE, "an element's x");
    b = a;
    b.el[1].y--;
    CHECK(wxm_diff(&l, &a, &b) == WXM_DIRTY_SPRITE, "an element's y");
    b = a;
    b.dy = 3;
    CHECK(wxm_diff(&l, &a, &b) == WXM_DIRTY_SPRITE, "the rise");
    b = a;
    b.opa = 200;
    CHECK(wxm_diff(&l, &a, &b) == WXM_DIRTY_SPRITE, "the sprite's opacity");
    b = a;
    b.text_opa = 3;
    CHECK(wxm_diff(&l, &a, &b) == WXM_DIRTY_TEXT, "the text");
    b = a;
    b.icon_off = true;
    CHECK(wxm_diff(&l, &a, &b) == WXM_DIRTY_TEXT, "the icon");
    a.opa = b.opa = 0;
    b = a;
    b.el[0].x += 5;
    b.dy = 1;
    CHECK(wxm_diff(&l, &a, &b) == 0, "hidden sprite: no redraw");
    b = a;
    a.opa = b.opa = 255;
    a.el[0].opa = b.el[0].opa = 0;
    b.el[0].x += 3;
    CHECK(wxm_diff(&l, &a, &b) == 0, "a hidden element moving: no redraw");
    wxm_frame(&m, &l, 0, &b);
    CHECK(wxm_diff(&l, NULL, &b) == WXM_DIRTY_TEXT, "the first frame against nothing: the text");
    CHECK(l.ne == 2, "heat elements");
}

static void test_abort(void)
{
    static const int WANT[WXM_FRAMES] = {66, 65, 64, 63, 62, 61, 60, 59, [8 ... 57] = 58, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67};
    for (int k = 0; k < WXM_FRAMES; k++) CHECK(wxm_abort_frame(k) == WANT[k], "abort at %d: %d, want %d", k, wxm_abort_frame(k), WANT[k]);
    CHECK(wxm_abort_frame(-1) == WXM_FRAMES - 1, "before the first frame");
    wxm_moment_t m = variant(WXM_GLANCE, 1);
    wxm_layout_t l;
    wxm_layout(&m, &l);
    for (int k = 0; k < WXM_SPRITE_IN; k++) {
        wxm_frame_t a, b;
        wxm_frame(&m, &l, k, &a);
        wxm_frame(&m, &l, wxm_abort_frame(k), &b);
        CHECK(a.opa == b.opa && a.dy == b.dy && b.text_opa == a.text_opa,
              "abort at %d keeps the sprite where it is (%d/%d -> %d/%d, text %d -> %d)", k, a.opa, a.dy, b.opa, b.dy, a.text_opa, b.text_opa);
    }
}

static bool inside(const wxm_el_t *e, const wxm_layer_t *y)
{
    return e->x + y->w > 0 && e->y + y->h > 0 && e->x < WXM_BOX_W && e->y < WXM_BOX_H;
}

static void test_motion(void)
{
    wxm_frame_t f;
    wxm_layout_t l;
    wxm_moment_t m = variant(WXM_RAIN, 0);
    wxm_layout(&m, &l);
    for (int k = 0; k < WXM_FRAMES; k++) {
        wxm_frame(&m, &l, k, &f);
        CHECK(f.el[4].opa == (k < 4 ? 255 : 0) && f.el[5].opa == (k >= 4 && k < 6 ? 255 : 0) && f.el[6].opa == (k >= 6 ? 255 : 0),
              "umbrella poses at %d", k);
    }
    for (int k = 12; k <= 36; k += 12) {
        wxm_frame(&m, &l, k, &f);
        CHECK(f.el[0].opa == 0 && f.el[0].x == l.el[0].x + 3 && f.el[0].y == l.el[0].y, "drop 1 at its loop start %d: (%d, %d) %d", k,
              f.el[0].x - l.el[0].x, f.el[0].y - l.el[0].y, f.el[0].opa);
    }
    wxm_frame(&m, &l, 14, &f);
    CHECK(f.el[0].opa == 255 && f.el[0].y == l.el[0].y + 11 && f.el[0].x == l.el[0].x + 2, "drop 1 two frames in: (%d, %d)", f.el[0].x - l.el[0].x, f.el[0].y - l.el[0].y);
    CHECK(f.el[1].y == l.el[1].y + (int)lroundf((12 + 52 * 3.0f / 8) * WXM_K) && f.el[1].opa == 255, "drop 2 is 3 frames ahead: dy %d",
          f.el[1].y - l.el[1].y);
    CHECK(f.el[3].y == l.el[3].y + (int)lroundf((64 + 14 * 0.5f) * WXM_K) && abs(f.el[3].opa - 128) <= 1, "drop 4 is 9 frames ahead: dy %d",
          f.el[3].y - l.el[3].y);
    wxm_frame(&m, &l, 23, &f);
    CHECK(f.el[0].y == l.el[0].y + 64 && abs(f.el[0].opa - 128) <= 1 && f.el[0].x == l.el[0].x - 2, "drop 1 halfway out: (%d, %d) opa %d",
          f.el[0].x - l.el[0].x, f.el[0].y - l.el[0].y, f.el[0].opa);

    m = variant(WXM_STORM, 0);
    wxm_layout(&m, &l);
    int flashes = 0;
    for (int k = 0; k < WXM_FRAMES; k++) {
        wxm_frame(&m, &l, k, &f);
        bool on = (k >= 12 && k < 14) || (k >= 18 && k < 20) || (k >= 24 && k < 26);
        CHECK(f.el[0].opa == (on ? 255 : 0) && f.el[1].opa == (on ? 255 : 0) && f.el[2].opa == (on ? 0 : 255), "storm at %d", k);
        flashes += on;
    }
    CHECK(flashes == 6, "three flashes of two frames");

    m = variant(WXM_AFTERRAIN, 0);
    wxm_layout(&m, &l);
    for (int k = 0; k < WXM_FRAMES; k++) {
        wxm_frame(&m, &l, k, &f);
        int want = k <= 8 ? 0 : k >= 23 ? 255 : (int)lroundf((float)(k - 8) / 15 * 255);
        CHECK(f.el[0].opa == want && f.el[1].opa == 255, "rainbow at %d: %d, want %d", k, f.el[0].opa, want);
    }

    m = variant(WXM_GLANCE, 2);
    wxm_layout(&m, &l);
    int lo = 99, hi = -99;
    for (int k = 0; k < WXM_FRAMES; k++) {
        wxm_frame(&m, &l, k, &f);
        int dx = f.el[1].x - l.el[1].x;
        lo = dx < lo ? dx : lo;
        hi = dx > hi ? dx : hi;
        CHECK(f.el[0].x == l.el[0].x, "the sun stays");
    }
    CHECK(lo == -4 && hi == 4, "the cloud drifts ±4 design px (±3.6 px): %d..%d", lo, hi);
    wxm_frame(&m, &l, 24, &f);
    CHECK(f.el[1].x - l.el[1].x == 4, "right end at 1.6 s");

    m = variant(WXM_WIND, 0);
    wxm_layout(&m, &l);
    wxm_frame(&m, &l, 9, &f);
    static const struct {
        float x, o;
    } GUST9[4] = {{22 + 56 * 0.8f, 0.9f}, {22 * 0.5f, 0.45f}, {78 + 14 * 0.5f, 0.45f}, {22 + 56 * 0.2f, 0.9f}};
    for (int r = 0; r < 4; r++)
        CHECK(f.el[r].x - l.el[r].x == (int)lroundf(GUST9[r].x * WXM_K) && abs(f.el[r].opa - (int)lroundf(GUST9[r].o * 255)) <= 1 &&
                  f.el[r].y == l.el[r].y,
              "gust row %d at 0.6 s (its own delay): dx %d opa %d", r, f.el[r].x - l.el[r].x, f.el[r].opa);
    CHECK(f.el[4].x - l.el[4].x == (int)lroundf((70 + 52 * (0.6f + 8.0f / 15 - 0.8f) / 0.6f) * WXM_K), "the leaf, 8 frames ahead: dx %d",
          f.el[4].x - l.el[4].x);

    m = variant(WXM_HEAT, 0);
    wxm_layout(&m, &l);
    wxm_frame(&m, &l, 0, &f);
    CHECK(f.el[0].opa == 89, "the glow starts at 0.35: %d", f.el[0].opa);
    wxm_frame(&m, &l, 12, &f);
    CHECK(f.el[0].opa == 255, "full at 0.8 s");
    wxm_frame(&m, &l, 24, &f);
    CHECK(f.el[0].opa == 89, "and again at 1.6 s");
    wxm_frame(&m, &l, 6, &f);
    CHECK(f.el[0].opa == (int)lroundf((0.35f + 0.65f * 0.5f) * 255), "halfway up at 0.4 s: %d", f.el[0].opa);

    m = variant(WXM_NIGHT, 2);
    wxm_layout(&m, &l);
    wxm_frame(&m, &l, 12, &f);
    CHECK(f.el[0].opa == 255 && f.el[1].opa == 77 && f.el[2].opa == 255, "twinkle at 0.8 s: %d %d", f.el[1].opa, f.el[2].opa);

    for (int a = 0; a < N_ALL; a++)
        for (int v = 0; v < 4; v++) {
            m = variant(ALL[a], v);
            wxm_layout(&m, &l);
            for (int k = WXM_SPRITE_IN; k <= WXM_HOLD_END; k++) {
                wxm_frame(&m, &l, k, &f);
                for (int e = 0; e < l.ne; e++)
                    if (f.el[e].opa) CHECK(inside(&f.el[e], &l.layer[l.el[e].layer]), "%s element %d at %d inside the box", KIND[m.kind], e, k);
            }
        }
}

static const wxm_cue_t *script(wxm_kind_t k, int *n)
{
    wxm_moment_t m = variant(k, 0);
    return wxm_script(&m, n);
}

typedef struct {
    float t;
    wxm_op_t op;
    float a, b;
    wxm_eyes_t eyes;
} want_t;

static void same_script(wxm_kind_t k, const want_t *w, int nw)
{
    int n;
    const wxm_cue_t *c = script(k, &n);
    CHECK(n == nw, "%s: %d cues, bot2.py has %d", KIND[k], n, nw);
    for (int i = 0; i < n && i < nw; i++)
        CHECK(fabsf(c[i].t - w[i].t) < 1e-5f && c[i].op == w[i].op && fabsf(c[i].a - w[i].a) < 1e-6f && fabsf(c[i].b - w[i].b) < 1e-6f &&
                  c[i].eyes == w[i].eyes,
              "%s cue %d: %g %d %g %g %d, bot2.py %g %d %g %g %d", KIND[k], i, c[i].t, c[i].op, c[i].a, c[i].b, c[i].eyes, w[i].t, w[i].op,
              w[i].a, w[i].b, w[i].eyes);
}

static void test_script_design(void)
{
    const float f = 1.0f / 15, h = 58.0f / 15;
    const want_t glance[] = {{f, WXM_GLIDE, 0, 0.25f, 0}, {2.4f, WXM_BLINK, 1, 0, 0}, {h, WXM_END, 1, 0.3f, 0}};
    const want_t rain[] = {{f, WXM_GLIDE, 0, 0.25f, 0}, {1.6f, WXM_BLINK, 1, 0, 0}, {h, WXM_END, 1, 0.3f, 0}};
    const want_t storm[] = {{f, WXM_GLIDE, 0, 0.18f, 0},      {0.8f, WXM_LID, 0.45f, 0.075f, 0}, {1.0f, WXM_LID, 0.12f, 0.15f, 0},
                            {1.2f, WXM_LID, 0.45f, 0.075f, 0}, {1.4f, WXM_LID, 0.12f, 0.15f, 0}, {1.6f, WXM_LID, 0.45f, 0.075f, 0},
                            {1.8f, WXM_LID, 0.12f, 0.15f, 0},  {2.4f, WXM_LID, 0, 0.4f, 0},     {h, WXM_END, 1, 0.3f, 0}};
    const want_t snow[] = {{2 * f, WXM_EYES, 0, 0, WXM_EYES_ROUND}, {0.4f, WXM_GLIDE, 0, 0.15f, 0}, {1.4f, WXM_GLIDE, -0.15f, 0, 0},
                           {2.4f, WXM_GLIDE, 0.15f, 0.05f, 0},      {3.2f, WXM_GLIDE, 0, 0.12f, 0}, {h, WXM_END, 1, 0.3f, 0}};
    const want_t wind[] = {{f, WXM_SACCADE, 0.38f, 0.1f, 0},     {f, WXM_LID, 0.45f, 0.25f, 0},       {1.2f, WXM_SACCADE, 0.46f, 0.12f, 0},
                           {1.7f, WXM_SACCADE, 0.33f, 0.08f, 0}, {2.5f, WXM_SACCADE, 0.46f, 0.1f, 0}, {3.2f, WXM_SACCADE, 0.36f, 0.12f, 0},
                           {h, WXM_END, 0, 0.3f, 0}};
    const want_t heat[] = {{2 * f, WXM_EYES, 0, 0, WXM_EYES_HAPPY}, {0.4f, WXM_GLIDE, 0, 0.2f, 0}, {h, WXM_END, 1, 0.3f, 0}};
    const want_t night[] = {{f, WXM_GLIDE, -0.1f, -0.15f, 0}, {f, WXM_LID, 0.4f, 0.6f, 0}, {2.2f, WXM_BLINK, 2.2f, 0, 0}, {h, WXM_END, 1, 0.4f, 0}};
    const want_t after[] = {{2 * f, WXM_EYES, 0, 0, WXM_EYES_HAPPY}, {0.4f, WXM_GLIDE, 0, 0.12f, 0}, {1.2f, WXM_HOP, 0, 0, 0}, {h, WXM_END, 1, 0.3f, 0}};
#define SAME(k, w) same_script(k, w, (int)(sizeof w / sizeof w[0]))
    SAME(WXM_GLANCE, glance);
    SAME(WXM_FROST, glance);
    SAME(WXM_RAIN, rain);
    SAME(WXM_STORM, storm);
    SAME(WXM_SNOW, snow);
    SAME(WXM_WIND, wind);
    SAME(WXM_HEAT, heat);
    SAME(WXM_NIGHT, night);
    SAME(WXM_AFTERRAIN, after);
#undef SAME
}

static void test_script(void)
{
    for (int a = 0; a < N_ALL; a++) {
        int n;
        const wxm_cue_t *c = script(ALL[a], &n);
        CHECK(c && n >= 3, "%s: a script", KIND[ALL[a]]);
        for (int i = 1; i < n; i++) CHECK(c[i].t >= c[i - 1].t, "%s: in time order", KIND[ALL[a]]);
        CHECK(c[n - 1].op == WXM_END && fabsf(c[n - 1].t - (float)WXM_HOLD_END / WXM_FPS) < 1e-6f, "%s: ends at the hold's end", KIND[ALL[a]]);
        for (int i = 0; i < n - 1; i++) CHECK(c[i].op != WXM_END && c[i].t > 0 && c[i].t < c[n - 1].t, "%s: one end", KIND[ALL[a]]);
        wxm_moment_t q = variant(ALL[a], 1);
        CHECK(!wxm_script(&q, &n) && n == 0, "%s quiet: no acting", KIND[ALL[a]]);
    }
    int n;
    const wxm_cue_t *c = script(WXM_SNOW, &n);
    CHECK(c[0].op == WXM_EYES && c[0].eyes == WXM_EYES_ROUND && fabsf(c[0].t - 2.0f / 15) < 1e-6f, "snow: round eyes on frame 2");
    c = script(WXM_HEAT, &n);
    CHECK(c[0].op == WXM_EYES && c[0].eyes == WXM_EYES_HAPPY, "heat: happy eyes");
    c = script(WXM_AFTERRAIN, &n);
    CHECK(c[0].eyes == WXM_EYES_HAPPY && c[2].op == WXM_HOP && fabsf(c[2].t - 1.2f) < 1e-6f, "after rain: happy, a hop at 1.2 s");
    c = script(WXM_WIND, &n);
    CHECK(c[0].op == WXM_SACCADE && c[n - 1].a == 0, "wind: saccades, back with a saccade");
    c = script(WXM_NIGHT, &n);
    CHECK(c[1].op == WXM_LID && fabsf(c[1].a - 0.4f) < 1e-6f && c[2].op == WXM_BLINK && fabsf(c[2].a - 2.2f) < 1e-6f && fabsf(c[n - 1].b - 0.4f) < 1e-6f,
          "night: sleepy lids, a slow blink");
    c = script(WXM_STORM, &n);
    int squints = 0;
    for (int i = 0; i < n; i++) squints += c[i].op == WXM_LID && fabsf(c[i].a - 0.45f) < 1e-6f;
    CHECK(squints == 3, "storm: a squint per flash");
    wxm_moment_t g = variant(WXM_GLANCE, 0), r = variant(WXM_RAIN, 0);
    const wxm_cue_t *gc = wxm_script(&g, &n), *rc = wxm_script(&r, &n);
    CHECK(gc[0].op == WXM_GLIDE && fabsf(gc[0].b - 0.25f) < 1e-6f && fabsf(gc[1].t - 2.4f) < 1e-6f && fabsf(rc[1].t - 1.6f) < 1e-6f,
          "glance and rain look up 0.25 and blink at 2.4 / 1.6 s");
    wxm_moment_t fr = variant(WXM_FROST, 0);
    CHECK(wxm_script(&fr, &n) == gc, "frost acts as the glance");
}

#define CX 236.0
#define CY 233.0
#define BODY_R (233.0 * 0.84)
#define HOP_SCALE 0.45
#define EYE_SCALE 1.15

typedef struct {
    double x0, y0, x1, y1;
} fbox_t;

static fbox_t eye_box(const bot_pose_t *p)
{
    bot_stroke_t st[2];
    bot_eye_strokes(p, EYE_SCALE, st);
    fbox_t b = {1e9, 1e9, -1e9, -1e9};
    double k = BODY_R * (p->scale_x + p->scale_y) / 2;
    for (int e = 0; e < 2; e++) {
        double hw = st[e].width * k / 2 + 1;
        for (int i = 0; i < st[e].n; i++) {
            double by = p->offset_y * HOP_SCALE - (1 - p->scale_y) + p->scale_y * st[e].y[i];
            double sx = CX + BODY_R * (p->offset_x + p->scale_x * st[e].x[i]), sy = CY - BODY_R * by;
            b.x0 = fmin(b.x0, sx - hw);
            b.x1 = fmax(b.x1, sx + hw);
            b.y0 = fmin(b.y0, sy - hw);
            b.y1 = fmax(b.y1, sy + hw);
        }
    }
    return b;
}

static int rim_dy_of(const bot_pose_t *p) { return bot_rim_dy(p, BODY_R, HOP_SCALE); }

static int cue_eyes(wxm_eyes_t e) { return e == WXM_EYES_ROUND ? BOT_EYES_ROUND : e == WXM_EYES_HAPPY ? BOT_EYES_HAPPY : -1; }

static void cue(bot_t *b, const wxm_cue_t *c, double t)
{
    switch (c->op) {
    case WXM_GLIDE: bot_act_gaze(b, c->a, c->b, true, t); break;
    case WXM_SACCADE: bot_act_gaze(b, c->a, c->b, false, t); break;
    case WXM_LID: bot_act_lid(b, c->a, c->b, t); break;
    case WXM_BLINK: bot_act_blink(b, c->a, t); break;
    case WXM_EYES: bot_act_eyes(b, cue_eyes(c->eyes), t); break;
    case WXM_HOP: bot_act_hop(b, t); break;
    case WXM_END: bot_act_end(b, c->a > 0, c->b, t); break;
    }
}

static void test_acting(void)
{
    static const bot_mood_t MOODS[] = {BOT_IDLE, BOT_DONE, BOT_SLEEPY, BOT_WORKING};
    double top = 1e9, bottom = -1e9;
    int runs = 0;
    for (int a = 0; a < N_ALL; a++)
        for (int mi = 0; mi < 4; mi++)
            for (int seed = 0; seed < 40; seed++) {
                bool quiet = MOODS[mi] == BOT_WORKING;
                wxm_moment_t m = variant(ALL[a], quiet);
                wxm_layout_t l;
                wxm_layout(&m, &l);
                int n;
                const wxm_cue_t *c = wxm_script(&m, &n);
                bot_t b;
                bot_init(&b, 0x5EED00ULL + (uint64_t)seed * 7919 + (uint64_t)a * 131, 0);
                bot_set_sleep_after(&b, MOODS[mi] == BOT_SLEEPY ? 1 : 0);
                double t = 0, start = 3 + seed * 0.37;
                if (MOODS[mi] != BOT_SLEEPY) bot_set_mood(&b, MOODS[mi], 0.01);
                for (; t < start; t += 1.0 / 60) bot_pose(&b, t);
                bot_mood_t mood = b.mood;
                if (!quiet) bot_act_begin(&b, t);
                double t0 = t + (quiet ? 0 : WXM_PRE_S);
                int i = 0, rim_dy = 0, last_k = -1;
                bool have_rim = false, have_pose = false, any = false;
                bot_pose_t last;
                wxm_frame_t shown;
                for (; t < t0 + (WXM_FRAMES + 30) * F; t += 1.0 / 60) {
                    while (i < n && t - t0 >= c[i].t) cue(&b, &c[i++], t);
                    bot_pose_t p = bot_pose(&b, t);
                    int k = (int)floor((t - t0) * WXM_FPS);
                    wxm_frame_t f;
                    wxm_frame(&m, &l, k, &f);
                    int dy = rim_dy_of(&p);
                    bool squashed = fabs(p.scale_y / p.scale_x - 1) >= 0.015;
                    bool hop = m.kind == WXM_AFTERRAIN && t - t0 >= 1.2 && t - t0 < 1.2 + BOT_HOP_DURATION_S + 0.05;
                    int areas = 0;
                    if (k >= 0 && k < WXM_FRAMES) {
                        bool rim = (have_rim && dy != rim_dy) || squashed;
                        CHECK(!rim || hop, "%s in mood %d, seed %d, frame %d: the outline moves (dy %d -> %d, %s)", KIND[m.kind], mood, seed, k,
                              rim_dy, dy, squashed ? "squashed" : "lean");
                        areas += rim;
                        areas += have_pose && !bot_pose_same(&p, &last, BODY_R);
                        if (k != last_k) {
                            areas += wxm_diff(&l, any ? &shown : NULL, &f) != 0;
                            areas += quiet;
                            shown = f;
                            any = true;
                            last_k = k;
                        }
                        CHECK(areas <= (quiet ? 3 : 2) || hop, "%s in mood %d, seed %d, frame %d: %d redraw areas", KIND[m.kind], mood, seed, k, areas);
                    }
                    rim_dy = dy;
                    have_rim = true;
                    last = p;
                    have_pose = true;
                    fbox_t e = eye_box(&p);
                    if (f.opa > 0) {
                        top = fmin(top, e.y0);
                        CHECK(e.y0 >= WXM_BOX_Y + WXM_BOX_H, "%s in mood %d, seed %d, frame %d: eyes reach y %.1f, into the sprite box", KIND[m.kind],
                              mood, seed, k, e.y0);
                    }
                    if (f.text_opa > 0) {
                        bottom = fmax(bottom, e.y1);
                        CHECK(e.y1 <= WXM_TEXT_Y, "%s in mood %d, seed %d, frame %d: eyes reach y %.1f, into the text line", KIND[m.kind],
                              mood, seed, k, e.y1);
                    }
                }
                CHECK(!b.acting && b.act_eyes == -1, "%s: the act is over", KIND[m.kind]);
                bot_pose_t p = bot_pose(&b, t);
                CHECK(b.mood != mood || p.eyes == (mood == BOT_DONE ? BOT_EYES_HAPPY : BOT_EYES_DASH), "%s in mood %d: the mood's eyes back (%d)",
                      KIND[m.kind], mood, p.eyes);
                CHECK(fabs(p.lid_l - (mood == BOT_SLEEPY ? 0.80 + 0.12 * p.breath : 0)) < 0.02 || b.blinking, "%s: no lid left (%.2f)", KIND[m.kind], p.lid_l);
                runs++;
            }
    printf("moment acting: %d runs, eyes from y %.1f (sprite box ends at %d) to %.1f (text line from %d)\n", runs, top,
           WXM_BOX_Y + WXM_BOX_H, bottom, WXM_TEXT_Y);
}

static void test_acting_order(void)
{
    bot_t b;
    bot_init(&b, 7, 0);
    for (double t = 0; t < 2; t += 0.016) bot_pose(&b, t);
    bot_act_begin(&b, 2);
    CHECK(b.acting && b.gaze_to_x == 0 && fabs(b.gaze_to_y - 0.1) < 1e-9, "begin glides to the viewer");
    for (double t = 2; t < 12; t += 0.016) bot_pose(&b, t);
    CHECK(b.gaze_to_x == 0 && fabs(b.gaze_to_y - 0.1) < 1e-9, "no own saccades while acting");
    bot_act_end(&b, true, 0.3, 12);
    double gx = b.gaze_to_x;
    for (double t = 12; t < 20; t += 0.016) bot_pose(&b, t);
    CHECK(!b.acting && b.gaze_to_x != gx, "own saccades again after the end");
    double tx = b.gaze_to_x, ty = b.gaze_to_y, ts = b.gaze_start;
    bot_act_end(&b, true, 0.3, 21);
    CHECK(!b.acting && b.gaze_to_x == tx && b.gaze_to_y == ty && b.gaze_start == ts, "a second end: nothing");
}

static uint32_t rgb_at(const wxm_layout_t *l, const uint8_t *arena, int layer, float u, float v, int *alpha)
{
    const wxm_layer_t *y = &l->layer[layer];
    int bx = (int)floorf(u * WXM_K + 232 - 233 * WXM_K + 3 - WXM_BOX_X), by = (int)floorf(v * WXM_K + 62 - 84 * WXM_K - WXM_BOX_Y);
    int x = bx - y->x, yy = by - y->y;
    if (x < 0 || yy < 0 || x >= y->w || yy >= y->h) {
        *alpha = -1;
        return 0;
    }
    const uint16_t *px = (const uint16_t *)(arena + y->at);
    const uint8_t *a = arena + y->at + y->w * y->h * 2;
    int i = yy * y->w + x;
    *alpha = a[i];
    uint16_t c = px[i];
    return ((uint32_t)((c >> 11) & 31) << 19) | ((uint32_t)((c >> 5) & 63) << 10) | ((uint32_t)(c & 31) << 3);
}

static uint32_t lerp(uint32_t a, uint32_t b, float f)
{
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
        out |= (uint32_t)lroundf(ca + (cb - ca) * f) << s;
    }
    return out;
}

static bool near(uint32_t a, uint32_t b, int tol)
{
    for (int s = 0; s <= 16; s += 8)
        if (abs((int)((a >> s) & 0xFF) - (int)((b >> s) & 0xFF)) > tol) return false;
    return true;
}

static uint8_t s_arena[WXM_ARENA];

static void paint(wxm_moment_t *m, wxm_layout_t *l)
{
    wxm_layout(m, l);
    memset(s_arena, 0xA5, sizeof s_arena);
    wxm_paint(m, l, s_arena, NULL);
}

#define AT(layer, u, v, rgb, what)                                                                                   \
    do {                                                                                                             \
        int al;                                                                                                      \
        uint32_t c = rgb_at(&l, s_arena, layer, u, v, &al);                                                          \
        CHECK(al > 240 && near(c, rgb, 8), "%s at (%g, %g): %06X alpha %d, want %06X", what, (double)(u), (double)(v), \
              (unsigned)c, al, (unsigned)(rgb));                                                                     \
    } while (0)
#define FAINT(layer, u, v, what)                                                                       \
    do {                                                                                               \
        int al;                                                                                        \
        rgb_at(&l, s_arena, layer, u, v, &al);                                                         \
        CHECK(al >= 0 && al < 48, "%s at (%g, %g): alpha %d, want next to none", what, (double)(u), (double)(v), al); \
    } while (0)
#define CLEAR(layer, u, v, what)                                                                       \
    do {                                                                                               \
        int al;                                                                                        \
        rgb_at(&l, s_arena, layer, u, v, &al);                                                         \
        CHECK(al == 0, "%s at (%g, %g): alpha %d, want clear", what, (double)(u), (double)(v), al);    \
    } while (0)

static void test_paint(void)
{
    wxm_layout_t l;
    wxm_moment_t m = variant(WXM_RAIN, 0);
    paint(&m, &l);
    AT(3, 205, 118, 0xFF8A3D, "open canopy");
    AT(3, 233, 118, 0xD9692A, "open middle panel");
    CLEAR(3, 194, 131, "a scallop bite");
    AT(3, 233, 150, 0xC9D2DC, "the shaft");
    AT(3, 226, 173, 0xC9D2DC, "the hook");
    CLEAR(3, 226, 166, "inside the hook");
    CLEAR(3, 200, 150, "beside the shaft");
    AT(1, 229.5f, 125, 0xFF8A3D, "closed body");
    CLEAR(1, 205, 120, "closed: no canopy");
    AT(2, 215, 130, 0xFF8A3D, "half canopy");
    AT(2, 233, 120, 0xD9692A, "half middle");
    AT(0, 164.5f, 101, WXM_BLUE, "the drop");
    CLEAR(0, 170, 108, "beside the drop");

    m = variant(WXM_STORM, 0);
    paint(&m, &l);
    AT(0, 241, 136, WXM_STORM_RGB, "the bolt");
    AT(0, 247, 125.6f, WXM_STORM_RGB, "the bolt's 2 px outline past its top edge");
    CLEAR(0, 252, 160, "beside the bolt");
    AT(1, 233, 120, 0x8E96A0, "the light cloud");
    AT(2, 233, 120, 0x555D68, "the dark cloud");
    AT(2, 233, 133, 0x6A7480, "the dark cloud's outline at its base");
    CLEAR(2, 233, 138, "under the base");

    m = variant(WXM_GLANCE, 2);
    paint(&m, &l);
    AT(0, 262 - 6.3f, 116 - 6.3f, 0xFFE07A, "the sun's bright side");
    {
        int al;
        uint32_t c = rgb_at(&l, s_arena, 0, 283, 116, &al);
        CHECK(al > 150 && near(c, 0xFFB547, 10), "the sun's rim line: %06X alpha %d", (unsigned)c, al);
        rgb_at(&l, s_arena, 0, 288, 116, &al);
        CHECK(al >= 24 && al <= 38, "the glow at 0.28 fading out past 0.7 of its radius: alpha %d", al);
    }
    AT(1, 233, 150, 0xE6ECF2, "the white cloud");
    AT(1, 218, 161, 0xE6ECF2, "the cloud between its puffs, under no circle");
    AT(1, 233, 164.5f, 0xC4CCD4, "the cloud's outline along its base");
    CLEAR(1, 233, 170, "under the cloud");

    m = variant(WXM_SNOW, 0);
    paint(&m, &l);
    AT(0, 192, 132, 0xD0DCE8, "the flake's centre");
    FAINT(0, 195.9f, 129.75f, "between the flake's arms");
    AT(1, 233, 115, 0xE6ECF2, "the snow cloud");

    m = variant(WXM_WIND, 0);
    paint(&m, &l);
    AT(0, 180, 103, 0xD0DCE8, "streak 1");
    AT(0, 217, 96, 0xD0DCE8, "streak 1's curl");
    CLEAR(0, 210, 96, "inside the curl");
    AT(1, 175, 124, 0xD0DCE8, "streak 2");
    AT(4, 166, 133, 0x8ACF7A, "the leaf");

    m = variant(WXM_HEAT, 0);
    paint(&m, &l);
    AT(1, 233 - 7.2f, 130 - 7.2f, 0xFFE07A, "the hot sun's bright side");
    AT(1, 233, 151, lerp(0xFFB547, 0xF08A3A, (hypotf(7.2f, 28.2f) / 31.2f - 0.6f) / 0.4f), "the hot sun's warm edge");
    AT(1, 268.5f, 130, 0xFFB547, "the ray at 3 o'clock");
    AT(1, 233 + 25, 130 + 25, 0xFFB547, "the ray at 45 degrees");
    CLEAR(1, 233 + 32.3f, 130 + 13.4f, "between two rays");
    int al;
    rgb_at(&l, s_arena, 0, 233, 130, &al);
    CHECK(al >= 110 && al <= 120, "the glow's middle at 0.45: alpha %d", al);
    rgb_at(&l, s_arena, 0, 233 + 45, 130, &al);
    CHECK(al < 10, "the glow fades out at its edge: %d", al);

    m = variant(WXM_NIGHT, 2);
    paint(&m, &l);
    AT(0, 222, 128, lerp(0xE8ECF2, 0xC9D2DC, hypotf(10, 6) / 36), "the lit side (waxing, 60 %)");
    AT(0, 186, 128, 0x000000, "the dark side");
    AT(1, 272, 104, 0xE8ECF2, "star 1");
    CLEAR(1, 277, 99, "star 1's hollow");
    AT(2, 294, 150, 0xE8ECF2, "star 2");
    m.moon_phase = 80;
    paint(&m, &l);
    AT(0, 202, 128, lerp(0xE8ECF2, 0xC9D2DC, hypotf(10, 6) / 36), "waning: lit on the left");
    AT(0, 238, 128, 0x000000, "waning: dark on the right");

    m = variant(WXM_AFTERRAIN, 0);
    paint(&m, &l);
    static const uint32_t BANDS[5] = {0xE86A5A, 0xF0A040, 0xF0D040, 0x5CCB8A, 0x5C9CE0};
    for (int i = 0; i < 5; i++) AT(0, 233, 172 - 63 + 6 * i, BANDS[i], "rainbow band");
    CLEAR(0, 233, 140, "under the rainbow");
    CLEAR(0, 233 - 60, 173.5f, "the bands end at their centre line");
    AT(1, 186, 165, 0xE6ECF2, "left foot");
    AT(1, 280, 165, 0xE6ECF2, "right foot");

    for (int a = 0; a < N_ALL; a++) {
        m = variant(ALL[a], 2);
        wxm_layout(&m, &l);
        static uint8_t first[WXM_ARENA];
        memset(s_arena, 0, sizeof s_arena);
        wxm_paint(&m, &l, s_arena, NULL);
        memcpy(first, s_arena, (size_t)l.total);
        memset(s_arena, 0xFF, sizeof s_arena);
        wxm_paint(&m, &l, s_arena, NULL);
        CHECK(memcmp(first, s_arena, (size_t)l.total) == 0, "%s: the paint does not depend on what the arena held", KIND[m.kind]);
        CHECK(s_arena[l.total] == 0xFF, "%s: nothing past the layout", KIND[m.kind]);
        for (int i = 0; i < l.nl; i++) {
            const wxm_layer_t *y = &l.layer[i];
            const uint8_t *alpha = s_arena + y->at + y->w * y->h * 2;
            int ink = 0, edge = 0;
            for (int p = 0; p < y->w * y->h; p++) ink += alpha[p] > 0;
            for (int x = 0; x < y->w; x++) edge += alpha[x] > 8 && y->y > 0;
            CHECK(ink > 20, "%s layer %d: ink %d", KIND[m.kind], i, ink);
            CHECK(edge == 0, "%s layer %d: cut at its top edge (%d px)", KIND[m.kind], i, edge);
        }
    }
}

int main(void)
{
    bot_shape_init();
    test_sig();
    test_change();
    test_now_kind();
    test_text();
    test_trigger();
    test_frames();
    test_layout();
    test_areas();
    test_abort();
    test_motion();
    test_script();
    test_script_design();
    test_acting_order();
    test_acting();
    test_paint();
    if (failures) {
        printf("moment: %d failure(s)\n", failures);
        return 1;
    }
    printf("moment: all passed\n");
    return 0;
}
