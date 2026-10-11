#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "weather_face.h"
#include "wx_faces.h"
#include "wx_paint.h"
#include "wx_sun.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define MID 1781996400LL
#define OFF 3600
#define DAY 86400LL
#define H(h, m) (MID + (h) * 3600LL + (m) * 60LL)

static wx_obs_t as2_seen(long long now, long long recv)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = 12, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .moon_up = 1, .observed_at = recv - 300, .has_obs_age = true, .obs_age_s = 300,
                  .has_utc_offset = true, .utc_offset = OFF, .sunrise = H(7, 30), .sunset = H(19, 30),
                  .twilight = {H(6, 55), H(7, 13), H(8, 30), H(18, 30), H(19, 47), H(20, 5)}, .has_sun_down = true};
    o.age_s = (double)(now - recv);
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "clear");
    snprintf(o.code, sizeof o.code, "0");
    return o;
}

static wx_obs_t as2_at(long long now) { return as2_seen(now, now); }

static wx_sun_t sun_at(long long now)
{
    wx_obs_t o = as2_at(now);
    wx_sun_t s;
    wx_sun_from(&o, &s);
    return s;
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

static uint32_t rgb_of(uint16_t c)
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (r << 19) | (g << 10) | (b << 3);
}

static void test_as2(void)
{
    wx_sun_t s = sun_at(H(10, 30));
    CHECK(s.on && s.have && s.has_now && s.polar == WX_SUN_NOT_POLAR, "AS2: the gauge and the clock");
    CHECK(s.phase == WX_SUN_DAY && s.hours && s.count == 18 && !strcmp(s.unit, "h left"), "AS2: 9 h left (%d half hours, %s)", s.count,
          s.unit);
    CHECK(!strcmp(s.sub, "golden hour 18:30") && s.sub_rgb == WX_SUN_GOLD_RGB, "AS2: '%s'", s.sub);
    CHECK(!strcmp(s.rise, "07:30") && !strcmp(s.set, "19:30"), "AS2: end labels %s %s", s.rise, s.set);
    CHECK(fabsf(s.frac - 215.0f / 790.0f) < 1e-4f, "AS2: the marker 215 of 790 min into the gauge: %f", s.frac);
    CHECK(wx_sun_start(&s) == H(6, 55) && wx_sun_end(&s) == H(20, 5), "AS2: from dawn to dusk");
    char buf[16];
    const char *u = wx_sun_value(&s, wx_sun_count_n(&s), buf, sizeof buf);
    CHECK(u && !strcmp(buf, "9") && !strcmp(u, "h left"), "AS2: '%s %s'", buf, u ? u : "");
    u = wx_sun_value(&s, 0, buf, sizeof buf);
    CHECK(!strcmp(buf, "0"), "count-up from 0: '%s'", buf);
    u = wx_sun_value(&s, 1, buf, sizeof buf);
    CHECK(!strcmp(buf, "\xC2\xBD"), "half hours on the way: '%s'", buf);
    CHECK(wx_sun_count_n(&s) == 18, "18 steps");
}

static void test_phases(void)
{
    static const struct {
        int h, m, sec;
        int phase;
        bool hours;
        int count;
        const char *unit, *sub;
        uint32_t rgb;
    } C[] = {
        {0, 0, 0, WX_SUN_BEFORE, true, 15, "h", "until sunrise 07:30", WX_SUN_HZN_RGB},
        {5, 0, 0, WX_SUN_BEFORE, true, 5, "h", "until sunrise 07:30", WX_SUN_HZN_RGB},
        {6, 30, 0, WX_SUN_BEFORE, true, 2, "h", "until sunrise 07:30", WX_SUN_HZN_RGB},
        {6, 30, 1, WX_SUN_BEFORE, false, 60, "min", "until sunrise 07:30", WX_SUN_HZN_RGB},
        {7, 29, 1, WX_SUN_BEFORE, false, 1, "min", "until sunrise 07:30", WX_SUN_HZN_RGB},
        {7, 29, 59, WX_SUN_BEFORE, false, 1, "min", "until sunrise 07:30", WX_SUN_HZN_RGB},
        {7, 30, 0, WX_SUN_MORNING, true, 24, "h left", "golden hour to 08:30", WX_SUN_GOLD_RGB},
        {8, 29, 59, WX_SUN_MORNING, true, 22, "h left", "golden hour to 08:30", WX_SUN_GOLD_RGB},
        {8, 30, 0, WX_SUN_DAY, true, 22, "h left", "golden hour 18:30", WX_SUN_GOLD_RGB},
        {18, 29, 59, WX_SUN_DAY, true, 2, "h left", "golden hour 18:30", WX_SUN_GOLD_RGB},
        {18, 30, 0, WX_SUN_EVENING, true, 2, "h left", "sunset 19:30", WX_SUN_HZN_RGB},
        {18, 30, 1, WX_SUN_EVENING, false, 60, "min left", "sunset 19:30", WX_SUN_HZN_RGB},
        {19, 29, 0, WX_SUN_EVENING, false, 1, "min left", "sunset 19:30", WX_SUN_HZN_RGB},
        {19, 29, 59, WX_SUN_EVENING, false, 1, "min left", "sunset 19:30", WX_SUN_HZN_RGB},
        {19, 30, 0, WX_SUN_AFTER, true, 24, "h", "of daylight today", WX_SUN_GREY_RGB},
        {23, 59, 59, WX_SUN_AFTER, true, 24, "h", "of daylight today", WX_SUN_GREY_RGB},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        long long now = H(C[i].h, C[i].m) + C[i].sec;
        wx_sun_t s = sun_at(now);
        CHECK(s.phase == C[i].phase && s.hours == C[i].hours && s.count == C[i].count && !strcmp(s.unit, C[i].unit) &&
                  !strcmp(s.sub, C[i].sub) && s.sub_rgb == C[i].rgb,
              "%02d:%02d:%02d: phase %d, %d %s (%s), '%s' %06X", C[i].h, C[i].m, C[i].sec, s.phase, s.count, s.unit,
              s.hours ? "half hours" : "min", s.sub, (unsigned)s.sub_rgb);
    }
    wx_sun_t s = sun_at(H(19, 30));
    CHECK(s.frac > 0.9f && s.frac < 1, "after sunset the marker is in the blue hour: %f", s.frac);
    CHECK(sun_at(H(20, 5)).frac == 1 && sun_at(H(23, 0)).frac == 1, "at dusk and after: the gauge's end");
    CHECK(sun_at(H(6, 55)).frac == 0 && sun_at(H(1, 0)).frac == 0, "at dawn and before: its start");
}

static void test_round(void)
{
    static const struct {
        long long s;
        bool hours;
        int count;
    } C[] = {
        {-5, false, 1}, {0, false, 1}, {1, false, 1}, {60, false, 1}, {61, false, 2}, {3540, false, 59}, {3541, false, 60},
        {3599, false, 60}, {3600, true, 2}, {5399, true, 2}, {5400, true, 3}, {43200, true, 24}, {86399, true, 47},
        {WX_SUN_COUNT_MAX_S, true, 96}, {WX_SUN_COUNT_MAX_S + 1800, true, 96}, {4000000000000LL, true, 96},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        bool h;
        int c;
        wx_sun_round(C[i].s, &h, &c);
        CHECK(h == C[i].hours && c == C[i].count, "%lld s: %s %d, want %d", C[i].s, h ? "half hours" : "min", c, C[i].count);
    }
}

static wx_sun_t seen(long long now, long long recv, int off)
{
    wx_obs_t o = as2_seen(now, recv);
    o.utc_offset = off;
    wx_sun_t s;
    wx_sun_from(&o, &s);
    return s;
}

static void test_shift(void)
{
    wx_sun_t s = seen(MID + DAY + WX_SUN_ROLL_GRACE_S - 1, H(10, 30), OFF);
    CHECK(s.phase == WX_SUN_AFTER && s.t[WX_SUN_RISE] == H(7, 30), "00:09:59 local: Ember's new view is due, nothing moved yet");
    s = seen(MID + DAY + WX_SUN_ROLL_GRACE_S, H(10, 30), OFF);
    CHECK(s.phase == WX_SUN_BEFORE && s.t[WX_SUN_RISE] == H(7, 30) + DAY && s.t[WX_SUN_DAWN] == H(6, 55) + DAY &&
              s.t[WX_SUN_DUSK] == H(20, 5) + DAY,
          "00:10 local, still the old view: every crossing moves a day");
    CHECK(s.hours && s.count == 14 && !strcmp(s.sub, "until sunrise 07:30") && !strcmp(s.rise, "07:30"),
          "7 h until tomorrow's sunrise, the same clock times (%d)", s.count);
    s = seen(MID + DAY + 3600, MID + DAY + 1, OFF);
    CHECK(s.t[WX_SUN_RISE] == H(7, 30) + DAY && s.phase == WX_SUN_BEFORE,
          "yesterday's times received a second after midnight: the crossings' own day counts, not the receipt's");
    s = seen(H(10, 30) + 2 * DAY, H(10, 30), OFF);
    CHECK(s.t[WX_SUN_RISE] == H(7, 30) + 2 * DAY && s.phase == WX_SUN_DAY && s.count == 18, "two days on: two days moved");
    s = seen(H(10, 30) + WX_SUN_ROLL_DAYS * DAY, H(10, 30), OFF);
    CHECK(s.have && s.t[WX_SUN_RISE] == H(7, 30) + WX_SUN_ROLL_DAYS * DAY, "a week on: still moved");
    s = seen(H(10, 30) + (WX_SUN_ROLL_DAYS + 1) * DAY, H(10, 30), OFF);
    CHECK(!s.have && !s.rise[0] && !s.count && !s.sub[0], "more than a week: no sun times");
    s = seen(H(10, 30) - DAY, H(10, 30) - DAY, OFF);
    CHECK(!s.have, "times for a day ahead of the clock: none");
    s = seen(MID + DAY - 1, H(10, 30), OFF);
    CHECK(s.have && s.phase == WX_SUN_AFTER, "23:59:59 local: the data's own day");
    s = seen(H(23, 0) + WX_SUN_ROLL_GRACE_S - 1, H(10, 30), 2 * OFF);
    CHECK(s.t[WX_SUN_RISE] == H(7, 30) && !strcmp(s.rise, "08:30"), "UTC+2: 00:09:59 local, not moved");
    s = seen(H(23, 0) + WX_SUN_ROLL_GRACE_S, H(10, 30), 2 * OFF);
    CHECK(s.t[WX_SUN_RISE] == H(7, 30) + DAY, "UTC+2: its midnight is 23:00 UTC+1, the offset sets the boundary");
    wx_obs_t edge = as2_at(H(12, 0));
    edge.sunrise = MID - 600;
    edge.sunset = MID + 23 * 3600;
    memset(edge.twilight, 0, sizeof edge.twilight);
    wx_sun_from(&edge, &s);
    CHECK(s.have && s.t[WX_SUN_RISE] == MID - 600 && !strcmp(s.rise, "23:50"),
          "a sunrise just before local midnight belongs to the day of its solar noon: not moved");
    wx_obs_t far = as2_at(1782018000LL);
    far.sunrise = 3867252584400LL;
    far.sunset = 3867252627600LL;
    wx_sun_from(&far, &s);
    CHECK(!s.have && !s.count, "sun times in another millennium (Codex's case): none, no countdown");
    wx_obs_t o = as2_seen(MID + DAY + 600, H(10, 30));
    memset(o.twilight, 0, sizeof o.twilight);
    wx_sun_from(&o, &s);
    CHECK(s.t[WX_SUN_RISE] == H(7, 30) + DAY && !s.t[WX_SUN_DAWN] && !s.t[WX_SUN_DUSK] && wx_sun_start(&s) == H(7, 30) + DAY &&
              wx_sun_end(&s) == H(19, 30) + DAY,
          "a missing crossing stays missing on the moved day");
    o = as2_seen(MID + DAY + 600, H(10, 30));
    o.has_obs_age = false;
    wx_sun_from(&o, &s);
    CHECK(!s.has_now && s.have && s.t[WX_SUN_RISE] == H(7, 30) && s.frac == 0 && !s.sub[0], "no clock: nothing moved, no marker, no sub-line");
    char buf[8];
    CHECK(!wx_sun_value(&s, 3, buf, sizeof buf) && !strcmp(buf, "--") && wx_sun_count_n(&s) == 0, "no clock: '--'");
    CHECK(!strcmp(s.rise, "07:30") && !strcmp(s.set, "19:30"), "the end labels still show");
}

static wx_sun_t with_twilight(long long d0, long long d1, long long ge, long long gs, long long b0, long long b1)
{
    wx_obs_t o = as2_at(H(10, 30));
    long long tw[WX_TWILIGHT] = {d0, d1, ge, gs, b0, b1};
    memcpy(o.twilight, tw, sizeof tw);
    wx_sun_t s;
    wx_sun_from(&o, &s);
    return s;
}

static void test_crossings(void)
{
    wx_sun_t s = with_twilight(0, 0, 0, 0, 0, 0);
    CHECK(s.have && wx_sun_start(&s) == H(7, 30) && wx_sun_end(&s) == H(19, 30), "no twilight: sunrise to sunset");
    CHECK(s.phase == WX_SUN_DAY && !strcmp(s.sub, "sunset 19:30") && s.sub_rgb == WX_SUN_HZN_RGB, "no golden hour: the sunset: '%s'", s.sub);
    CHECK(fabsf(s.frac - 0.25f) < 1e-4f, "10:30 a quarter into 07:30..19:30");
    s = with_twilight(0, 0, H(8, 30), 0, 0, 0);
    CHECK(!strcmp(s.sub, "sunset 19:30"), "no golden_start: the sunset after the morning's golden hour");
    s = with_twilight(H(8, 0), H(7, 13), H(8, 30), H(18, 30), H(19, 47), H(20, 5));
    CHECK(!s.t[WX_SUN_DAWN] && s.t[WX_SUN_BLUE_END] == H(7, 13) && wx_sun_start(&s) == H(7, 13), "a dawn after sunrise is dropped");
    s = with_twilight(H(6, 55), H(6, 50), H(8, 30), H(18, 30), H(19, 47), H(20, 5));
    CHECK(s.t[WX_SUN_DAWN] == H(6, 55) && !s.t[WX_SUN_BLUE_END], "a blue_end before dawn is dropped");
    s = with_twilight(H(6, 55), H(7, 13), H(7, 0), H(18, 30), H(19, 47), H(20, 5));
    CHECK(!s.t[WX_SUN_GOLD_END] && s.t[WX_SUN_GOLD_START] == H(18, 30), "a golden_end before sunrise is dropped");
    s = with_twilight(H(6, 55), H(7, 13), H(19, 40), H(18, 30), H(19, 47), H(20, 5));
    CHECK(!s.t[WX_SUN_GOLD_END] && s.t[WX_SUN_GOLD_START] == H(18, 30), "a golden_end after sunset is dropped");
    s = with_twilight(H(6, 55), H(7, 13), H(8, 30), H(8, 0), H(19, 47), H(20, 5));
    CHECK(s.t[WX_SUN_GOLD_END] == H(8, 30) && !s.t[WX_SUN_GOLD_START], "a golden_start before golden_end is dropped");
    s = with_twilight(H(6, 55), H(7, 13), H(8, 30), H(19, 31), H(19, 47), H(20, 5));
    CHECK(!s.t[WX_SUN_GOLD_START], "a golden_start after sunset is dropped");
    s = with_twilight(H(6, 55), H(7, 13), H(8, 30), H(18, 30), H(19, 29), H(20, 5));
    CHECK(!s.t[WX_SUN_BLUE_START] && s.t[WX_SUN_DUSK] == H(20, 5), "a blue_start before sunset is dropped");
    s = with_twilight(H(6, 55), H(7, 13), H(8, 30), H(18, 30), H(19, 47), H(19, 40));
    CHECK(s.t[WX_SUN_BLUE_START] == H(19, 47) && !s.t[WX_SUN_DUSK] && wx_sun_end(&s) == H(19, 47), "a dusk before blue_start is dropped");
    s = with_twilight(H(7, 30) - WX_SUN_SPAN_MAX_S - 1, H(7, 13), H(8, 30), H(18, 30), H(19, 47), H(19, 30) + WX_SUN_SPAN_MAX_S + 1);
    CHECK(!s.t[WX_SUN_DAWN] && !s.t[WX_SUN_DUSK], "crossings more than 12 h out are dropped");
    s = with_twilight(H(7, 30) - WX_SUN_SPAN_MAX_S, H(7, 13), H(8, 30), H(18, 30), H(19, 47), H(19, 30) + WX_SUN_SPAN_MAX_S);
    CHECK(s.t[WX_SUN_DAWN] && s.t[WX_SUN_DUSK], "12 h out is kept");
    s = with_twilight(H(7, 13), H(7, 13), H(7, 30), H(19, 30), H(19, 30), H(19, 47));
    CHECK(s.t[WX_SUN_DAWN] && s.t[WX_SUN_BLUE_END] && s.t[WX_SUN_GOLD_END] && s.t[WX_SUN_GOLD_START] && s.t[WX_SUN_BLUE_START] &&
              s.t[WX_SUN_DUSK],
          "equal neighbours (5-min rounding) are kept");
    uint32_t c = wx_sun_rgb(&s, H(7, 13));
    CHECK(c == 0x4A4FC8 || c == 0x3E6FD8, "and the colours stay defined: %06X", (unsigned)c);
    s = with_twilight(0, 0, H(5, 0), H(21, 0), 0, 0);
    CHECK(!s.t[WX_SUN_GOLD_END] && !s.t[WX_SUN_GOLD_START], "golden crossings outside the day: dropped");
    s = with_twilight(H(6, 55), H(7, 30), H(8, 30), H(19, 30), H(19, 47), H(20, 5));
    CHECK(s.t[WX_SUN_BLUE_END] == H(7, 30) && s.t[WX_SUN_GOLD_START] == H(19, 30), "a crossing on sunrise or sunset is kept");
    s = with_twilight(H(6, 55), H(7, 31), H(8, 30), H(19, 31), H(19, 29), H(20, 5));
    CHECK(!s.t[WX_SUN_BLUE_END] && !s.t[WX_SUN_GOLD_START] && !s.t[WX_SUN_BLUE_START], "a second past them is not");
}

static void test_polar(void)
{
    wx_obs_t o = as2_at(H(10, 30));
    o.sunrise = o.sunset = 0;
    memset(o.twilight, 0, sizeof o.twilight);
    wx_sun_t s;
    wx_sun_from(&o, &s);
    CHECK(!s.have && s.polar == WX_SUN_POLAR_DAY && !strcmp(s.sub, "Midnight sun") && s.sub_rgb == WX_SUN_GREY_RGB, "polar day: '%s'", s.sub);
    char buf[8];
    CHECK(!wx_sun_value(&s, 0, buf, sizeof buf) && !strcmp(buf, "--"), "no value");
    CHECK(!s.rise[0] && !s.set[0] && s.frac == 0, "no end labels, no marker");
    o.sun_down = true;
    wx_sun_from(&o, &s);
    CHECK(s.polar == WX_SUN_POLAR_NIGHT && !strcmp(s.sub, "Polar night"), "polar night: '%s'", s.sub);
    o.moon_up = 0;
    wx_sun_from(&o, &s);
    CHECK(s.polar == WX_SUN_POLAR_NIGHT, "moon down: the location is still known");
    o.moon_up = -1;
    wx_sun_from(&o, &s);
    CHECK(s.polar == WX_SUN_NOT_POLAR && !s.sub[0], "no moon.up: no location, so nothing to say");
    o.moon_up = 1;
    o.has_sun_down = false;
    wx_sun_from(&o, &s);
    CHECK(s.polar == WX_SUN_NOT_POLAR && !s.sub[0], "no night flag: nothing");
    o = as2_at(H(10, 30));
    o.sunset = 0;
    wx_sun_from(&o, &s);
    CHECK(!s.have && s.polar == WX_SUN_NOT_POLAR, "only a sunrise: no gauge, not polar");
    o = as2_at(H(10, 30));
    o.sunrise = 0;
    wx_sun_from(&o, &s);
    CHECK(!s.have && s.polar == WX_SUN_NOT_POLAR, "only a sunset: no gauge, not polar");
    o = as2_at(H(10, 30));
    o.sunset = o.sunrise;
    wx_sun_from(&o, &s);
    CHECK(!s.have, "sunset not after sunrise: no gauge");
    o.sunset = o.sunrise + DAY;
    wx_sun_from(&o, &s);
    CHECK(!s.have, "a day of 24 h or more: no gauge");
    o.sunset = o.sunrise + DAY - 1;
    wx_sun_from(&o, &s);
    CHECK(s.have, "just under 24 h: the gauge");
    o = as2_at(H(10, 30));
    o.has_utc_offset = false;
    wx_sun_from(&o, &s);
    CHECK(!s.have && !s.polar, "no offset: no clock times, no gauge");
    o = as2_at(H(10, 30));
    o.valid = false;
    wx_sun_from(&o, &s);
    CHECK(s.on && !s.have && !s.has_now && !s.sub[0], "invalid: the face, empty");
    wx_sun_from(NULL, &s);
    CHECK(s.on && !s.have, "no data: the face, empty");
}

static void test_rgb(void)
{
    static const struct {
        int h, m;
        uint32_t rgb;
    } C[] = {
        {6, 55, 0x4A4FC8}, {7, 4, 0x445FD0},  {7, 13, 0x3E6FD8},  {7, 20, 0x746CB6},  {7, 25, 0x9A6A9E},  {7, 30, 0xBA797F},
        {7, 36, 0xE08A5A}, {7, 40, 0xEB904F}, {7, 47, 0xFF9A3D},  {8, 0, 0xFFA842},   {8, 13, 0xFFB547},  {8, 30, 0xC08834},
        {8, 47, 0x815C21}, {10, 30, 0x946A24}, {13, 30, 0xB5832A}, {16, 0, 0x9A6E25},  {18, 13, 0x815C21}, {18, 30, 0xC08834},
        {18, 47, 0xFFB547}, {19, 13, 0xFF9A3D}, {19, 24, 0xE08A5A}, {19, 30, 0xBA797F}, {19, 35, 0x9A6A9E}, {19, 47, 0x3E6FD8},
        {19, 56, 0x445FD0}, {20, 5, 0x4A4FC8},
    };
    wx_sun_t s = sun_at(H(10, 30));
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        uint32_t c = wx_sun_rgb(&s, H(C[i].h, C[i].m));
        CHECK(channel_diff(c, C[i].rgb) <= 1, "%02d:%02d: %06X, AS2's generator %06X", C[i].h, C[i].m, (unsigned)c, (unsigned)C[i].rgb);
    }
    CHECK(wx_sun_rgb(&s, H(3, 0)) == 0x4A4FC8 && wx_sun_rgb(&s, H(23, 0)) == 0x4A4FC8, "outside the gauge: its end colours");
    s = with_twilight(0, 0, 0, 0, 0, 0);
    CHECK(wx_sun_rgb(&s, H(7, 30) - 300) == 0x9A6A9E && wx_sun_rgb(&s, H(7, 0)) == 0x9A6A9E && wx_sun_rgb(&s, H(13, 30)) == 0xB5832A,
          "no twilight: the blush and the day still");
    wx_sun_t none = {0};
    CHECK(wx_sun_rgb(&none, H(10, 0)) == 0xB5832A, "no crossings at all: a defined colour");
    CHECK(wx_sun_frac(&none, H(10, 0)) == 0 && wx_sun_start(&none) == 0 && wx_sun_end(&none) == 0, "and the gauge's start");
    s = with_twilight(H(6, 55), H(7, 13), H(7, 30) + 2 * WX_SUN_BLEND_S, H(18, 30), H(19, 47), H(20, 5));
    uint32_t c = wx_sun_rgb(&s, H(7, 30) + WX_SUN_BLEND_S + 60);
    CHECK(channel_diff(c, WX_SUN_HZN_RGB) <= 4, "a stop on the time of the one before is dropped: %06X", (unsigned)c);
}

static void test_day_edge(void)
{
    for (int d = -2; d <= 2; d++) {
        long long noon = H(13, 30), ge = noon - WX_SUN_BLEND_S + d;
        wx_sun_t s = with_twilight(H(6, 55), H(7, 13), ge, noon + (noon - ge), H(19, 47), H(20, 5));
        uint32_t c = wx_sun_rgb(&s, ge + WX_SUN_BLEND_S);
        CHECK(channel_diff(c, 0xB5832A) <= 1, "golden hour %+d s from 17 min before noon: noon's gold at noon, %06X", d, (unsigned)c);
    }
    wx_sun_t s = with_twilight(H(6, 55), H(7, 13), H(13, 30) - 2 * WX_SUN_BLEND_S, H(13, 30) + 2 * WX_SUN_BLEND_S, H(19, 47), H(20, 5));
    uint32_t c = wx_sun_rgb(&s, H(13, 30) - WX_SUN_BLEND_S);
    CHECK(channel_diff(c, 0x9A6E25) <= 1, "34 min from noon: half way from the edge to noon's gold, %06X", (unsigned)c);
    s = with_twilight(H(6, 55), H(7, 13), H(13, 30) - WX_SUN_BLEND_S / 2, H(13, 30) + WX_SUN_BLEND_S / 2, H(19, 47), H(20, 5));
    c = wx_sun_rgb(&s, H(13, 30) + WX_SUN_BLEND_S / 2);
    CHECK(channel_diff(c, 0xB5832A) <= 1, "golden hour 8 min from noon: noon's gold, not past it, %06X", (unsigned)c);
    s = with_twilight(H(6, 55), H(7, 13), H(13, 30), H(13, 30), H(19, 47), H(20, 5));
    c = wx_sun_rgb(&s, H(13, 30) + WX_SUN_BLEND_S);
    CHECK(channel_diff(c, 0xB5832A) <= 1, "golden hour at noon itself: no division by zero, %06X", (unsigned)c);
}

static void test_hero(void)
{
    wx_sun_t s = sun_at(H(10, 30));
    CHECK(wx_sun_hero(&s), "sun times: the sun");
    s = (wx_sun_t){.on = true, .polar = WX_SUN_POLAR_DAY};
    CHECK(wx_sun_hero(&s), "midnight sun: the sun");
    s.polar = WX_SUN_POLAR_NIGHT;
    CHECK(!wx_sun_hero(&s), "polar night: no sun");
    s.polar = WX_SUN_NOT_POLAR;
    CHECK(!wx_sun_hero(&s), "no data: no sun");
}

static void test_same(void)
{
    wx_sun_t a = sun_at(H(10, 30)), b = sun_at(H(11, 30));
    CHECK(wx_sun_same_scene(&a, &b), "the clock moving: same picture");
    for (int i = 0; i < WX_SUN_N; i++) {
        b = a;
        b.t[i] += 60;
        CHECK(!wx_sun_same_scene(&a, &b), "crossing %d moved: a new picture", i);
    }
    b = a;
    b.utc_offset += 3600;
    CHECK(!wx_sun_same_scene(&a, &b), "a new offset: new end labels");
    b = a;
    b.have = false;
    CHECK(!wx_sun_same_scene(&a, &b), "have");
    b = a;
    b.on = false;
    CHECK(!wx_sun_same_scene(&a, &b), "on");
    wx_sun_t p = {.on = true, .polar = WX_SUN_POLAR_DAY}, q = {.on = true, .polar = WX_SUN_POLAR_NIGHT};
    CHECK(wx_sun_same_scene(&p, &q), "polar day to night: only the sub-line changes");
    wx_sun_t x = {0}, y = {0};
    CHECK(wx_sun_same_scene(&x, &y), "both off: same");
}

static void test_pick(void)
{
    wx_obs_t o = as2_at(H(10, 30));
    wx_temp_t t, shown;
    wx_temp_face(&o, NULL, WXF_AUTO, &t);
    CHECK(t.face == WXF_TEMP && !t.sun.on, "never the auto face");
    wx_temp_face(&o, NULL, WXF_SUN, &t);
    CHECK(t.face == WXF_SUN && t.sun.on && t.sun.have && !t.rain.on && !t.cast.on && !t.wind.on && !t.days.on, "picked: the Sun");
    shown = t;
    o = as2_at(H(11, 30));
    wx_temp_face(&o, &shown, WXF_SUN, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == 0, "an hour later: no bake");
    o = as2_seen(MID + DAY + 60, H(10, 30));
    wx_temp_face(&o, &shown, WXF_SUN, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == 0, "a minute past local midnight: no bake yet");
    wx_temp_t next;
    o = as2_seen(MID + DAY + 60, MID + DAY + 30);
    o.sunrise += DAY;
    o.sunset += DAY;
    for (int i = 0; i < WX_TWILIGHT; i++) o.twilight[i] += DAY;
    wx_temp_face(&o, &shown, WXF_SUN, &next);
    CHECK(wx_temp_bake_kind(&next, &shown) == WX_BAKE_FULL, "Ember's new day: one bake");
    o = as2_seen(MID + DAY + WX_SUN_ROLL_GRACE_S + 60, MID + DAY + WX_SUN_ROLL_GRACE_S + 30);
    o.sunrise += DAY;
    o.sunset += DAY;
    for (int i = 0; i < WX_TWILIGHT; i++) o.twilight[i] += DAY;
    wx_temp_t later;
    wx_temp_face(&o, &next, WXF_SUN, &later);
    CHECK(wx_temp_bake_kind(&later, &next) == 0, "past the grace with the new view: no second bake");
    o = as2_seen(MID + DAY + WX_SUN_ROLL_GRACE_S, H(10, 30));
    wx_temp_face(&o, &shown, WXF_SUN, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == WX_BAKE_FULL, "offline past the grace: the moved day bakes");
    wx_temp_face(&o, &shown, WXF_TEMP, &t);
    CHECK(wx_temp_bake_kind(&t, &shown) == WX_BAKE_FULL && !t.sun.on, "another face: full, and no sun");
    wx_obs_t wet = as2_at(H(10, 30));
    snprintf(wet.condition, sizeof wet.condition, "rain");
    snprintf(wet.code, sizeof wet.code, "63");
    wx_temp_face(&wet, NULL, WXF_SUN, &t);
    CHECK(t.face == WXF_SUN && t.sun.on && !t.rain.on, "picked while it rains: the Sun");
    wx_temp_face(NULL, NULL, WXF_SUN, &t);
    CHECK(t.face == WXF_SUN && t.sun.on && !t.sun.have, "no data: the empty Sun");
}

static void test_frame(void)
{
    wx_sun_frame_t f;
    wx_sun_frame(0, &f);
    CHECK(f.dy == WX_SUN_RISE_PX && f.rays_opa == 0, "start: below, no rays");
    wx_sun_frame(-1, &f);
    CHECK(f.dy == WX_SUN_RISE_PX && f.rays_opa == 0, "before the start");
    wx_sun_frame(NAN, &f);
    CHECK(f.dy == WX_SUN_RISE_PX && f.rays_opa == 0, "NaN");
    wx_sun_frame(WX_ARRIVE_S, &f);
    CHECK(f.dy == 0, "risen at the arrival's end");
    wx_sun_frame(0.1, &f);
    CHECK(f.dy == 51, "rounded: 50.5 px down at 0.1 s (%d)", f.dy);
    wx_sun_frame(0.66, &f);
    CHECK(f.dy == (int)lround(64 * (1 - wx_arrive(0.66))) && f.dy > 0 && f.dy < 16, "eased out: mostly up half way (%d)", f.dy);
    wx_sun_frame(0.72 - 1e-6, &f);
    CHECK(f.rays_opa == 0, "no rays before 0.72 s");
    wx_sun_frame(0.73, &f);
    CHECK(f.rays_opa == 0, "the 15 fps step holding 0.72 s: still none (%d)", f.rays_opa);
    wx_sun_frame(11.0 / 15, &f);
    CHECK(f.rays_opa == 5, "the first step past it: %d", f.rays_opa);
    wx_sun_frame(0.8, &f);
    int a = f.rays_opa;
    wx_sun_frame(0.86, &f);
    CHECK(a == 28 && f.rays_opa == 28, "held within a 15 fps step: %d %d", a, f.rays_opa);
    wx_sun_frame(1.08, &f);
    CHECK(f.rays_opa == 123, "about half at 1.08 s: %d", f.rays_opa);
    wx_sun_frame(1.44, &f);
    CHECK(f.rays_opa == 241 && f.dy == 0, "nearly in at 1.44 s: %d", f.rays_opa);
    wx_sun_frame(22.0 / 15, &f);
    CHECK(f.rays_opa == 255, "all in on the first step past 1.44 s");
    int ldy = 99, lop = -1;
    bool mono = true;
    for (int k = 0; k <= 90; k++) {
        wx_sun_frame(k / 30.0, &f);
        mono &= f.dy <= ldy && f.rays_opa >= lop;
        ldy = f.dy;
        lop = f.rays_opa;
    }
    CHECK(mono, "rises and fades in, never back");
    for (double t = 22.0 / 15; t < 6; t += 0.1) {
        wx_sun_frame(t, &f);
        CHECK(f.dy == 0 && f.rays_opa == 255, "still at %.1f s", t);
    }
}

static bool box_in(wx_box_t b, wx_box_t a) { return b.x0 >= a.x0 && b.y0 >= a.y0 && b.x1 <= a.x1 && b.y1 <= a.y1; }

static void test_layout(void)
{
    wx_sun_layout_t l;
    wx_sun_layout(&l);
    size_t still = (size_t)wx_clouds_max_px(false) * 3;
    CHECK(box_in(l.sun, l.rays), "the sun box inside the rays box: one redraw area");
    CHECK(l.sun.y1 == WX_SUN_CLIP_Y && l.rays.y1 == WX_SUN_CLIP_Y, "both end at the horizon cut");
    CHECK(l.sun.x0 <= WX_SUN_CX - 74 && l.sun.x1 >= WX_SUN_CX + 74 && l.sun.y0 <= WX_SUN_CY - 74, "the sun box holds the glow");
    CHECK(!(l.rays.x0 & 1) && !(l.rays.x1 & 1) && !(l.sun.x0 & 1) && !(l.sun.x1 & 1), "even edges");
    int rays = (l.rays.x1 - l.rays.x0) * (l.rays.y1 - l.rays.y0), sun = (l.sun.x1 - l.sun.x0) * (l.sun.y1 - l.sun.y0);
    CHECK(l.rays_at == 0 && l.sun_at == rays && l.total == rays + 3 * sun && !(l.sun_at & 3), "back to back: A8 then RGB565A8");
    CHECK((size_t)l.total <= still, "fits the still buffer: %d of %zu B", l.total, still);
    printf("sun: rays %dx%d A8, sun %dx%d RGB565A8, %d of %zu B\n", l.rays.x1 - l.rays.x0, l.rays.y1 - l.rays.y0,
           l.sun.x1 - l.sun.x0, l.sun.y1 - l.sun.y0, l.total, still);

    wx_box_t face = {0, 0, WX_FACE, WX_FACE};
    uint8_t *a = calloc(WX_FACE * WX_FACE, 1);
    wx_paint_sun_rays(a, face);
    int outside = 0, below = 0, rim = 0, ink = 0;
    for (int y = 0; y < WX_FACE; y++)
        for (int x = 0; x < WX_FACE; x++) {
            if (!a[y * WX_FACE + x]) continue;
            ink++;
            outside += x < l.rays.x0 || x >= l.rays.x1 || y < l.rays.y0 || y >= l.rays.y1;
            below += y >= WX_SUN_CLIP_Y;
            rim += hypotf(x + 0.5f - 233, y + 0.5f - 233) >= 195;
        }
    CHECK(ink > 3000 && !outside && !below && !rim, "the fan, painted over the whole face, stays in its box (%d px, %d outside, %d below, %d past the rim)",
          ink, outside, below, rim);
    int off = 0, worst = 0;
    for (int y = 0; y < WX_FACE; y++)
        for (int x = 0; x < WX_FACE; x++) {
            float fx = x + 0.5f, fy = y + 0.5f, c = 0;
            for (int k = 0; k < WX_SUN_RAYS; k++) {
                float an = (-168.0f + 13.0f * k) * (float)M_PI / 180.0f, r2 = k % 2 ? 130.0f : 170.0f, hw = k % 2 ? 1.5f : 1.75f;
                float ax = 233 + 54 * cosf(an), ay = 178 + 54 * sinf(an), vx = (r2 - 54) * cosf(an), vy = (r2 - 54) * sinf(an);
                float u = ((fx - ax) * vx + (fy - ay) * vy) / (vx * vx + vy * vy);
                u = u < 0 ? 0 : u > 1 ? 1 : u;
                float d = hypotf(fx - ax - u * vx, fy - ay - u * vy) - hw, cv = 0.5f - d;
                cv = cv < 0 ? 0 : cv > 1 ? 1 : cv;
                if (cv > c) c = cv;
            }
            float g = hypotf(fx - 233, fy - 178) / 170, ga = g <= 0.3f ? 0.55f : g >= 1 ? 0 : 0.55f * (1 - (g - 0.3f) / 0.7f);
            float r = hypotf(fx - 233, fy - 233), m = r <= 0.82f * 195 ? 1 : r >= 195 ? 0 : (195 - r) / (195 - 0.82f * 195);
            float want = c * ga * m;
            int ref = want > 0.002f ? (int)lroundf((want > 1 ? 1 : want) * 255) : 0, got = a[y * WX_FACE + x], e = abs(got - ref);
            off += e > 1;
            if (e > worst) worst = e;
        }
    CHECK(!off, "the fan matches AS2's 13 rays (54 to 170 or 130 px, 3.5 or 3 px wide, 0.55 fading out by 170 px, rim fade): %d px off, worst %d",
          off, worst);
    free(a);
}

static void test_areas(void)
{
    wx_sun_layout_t l;
    wx_sun_layout(&l);
    wx_sun_frame_t prev, f;
    wx_sun_frame(-1, &prev);
    int changed = 0, last = -1, rays = 0;
    for (int k = 0; k <= 30 * 5; k++) {
        wx_sun_frame(k / 30.0, &f);
        if (f.dy != prev.dy || f.rays_opa != prev.rays_opa) {
            changed++;
            last = k;
        }
        rays += f.rays_opa != prev.rays_opa;
        prev = f;
    }
    CHECK(last >= 0 && last <= (int)ceil(WX_SUN_RAYS_IN_S * 30), "nothing moves after the arrival (last change on frame %d)", last);
    CHECK(rays <= (int)ceil((WX_SUN_RAYS_IN_S - WX_SUN_RAYS_ON_S) * WX_SUN_FPS) + 1, "the rays change at 15 fps: %d frames", rays);
    int w = l.rays.x1 - l.rays.x0, h = l.rays.y1 - l.rays.y0;
    printf("sun: 1 area of at most %dx%d (%d px) on %d of the arrival's frames (rays on %d), then none\n", w, h, w * h, changed, rays);
}

static void test_paint(void)
{
    static wx_ring_t ring;
    static uint16_t bg[WX_FACE * WX_FACE], lut[WX_RING_LUT];
    size_t still = (size_t)wx_clouds_max_px(false) * 3;
    uint8_t *m = calloc(1, still), *hero = calloc(1, WX_HERO_BOX * WX_HERO_BOX * 3), *drift = calloc(1, (size_t)wx_clouds_max_px(true) * 3);
    wx_bake_buf_t b = {.bg = bg, .lut = lut, .ring = &ring, .hero = hero, .still = m, .drift = drift};
    wx_obs_t o = as2_at(H(10, 30));
    wx_temp_t t;
    wx_temp_face(&o, NULL, WXF_SUN, &t);
    wx_bake_out_t out = {.hero = {1, 1, 2, 2}, .still = {1, 1, 2, 2}, .drift = {1, 1, 2, 2}};
    memset(bg, 0x55, sizeof bg);
    clock_t t0 = clock();
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    printf("sun: host bake %.2f ms\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    CHECK(out.hero.x1 == 0 && out.still.x1 == 0 && out.drift.x1 == 0, "no hero or cloud sprites");
    CHECK(lut[0] == wx_rgb565(0x4A4FC8) && lut[WX_RING_LUT - 1] == wx_rgb565(0x4A4FC8), "the ring starts and ends in the blue hour");
    CHECK(channel_diff(rgb_of(lut[(WX_RING_LUT - 1) / 2]), 0xB5832A) <= 8, "noon in the middle: %06X", (unsigned)rgb_of(lut[(WX_RING_LUT - 1) / 2]));
    for (int i = 0; i < 2; i++) {
        float x, y;
        wx_gauge_xy(wx_sun_frac(&t.sun, i ? H(19, 30) : H(7, 30)), &x, &y);
        float dx = (x - 233) / 222, dy = (y - 233) / 222;
        int px = (int)(233 + 204.5f * dx), py = (int)(233 + 204.5f * dy);
        CHECK(channel_diff(rgb_of(bg[py * WX_FACE + px]), WX_SUN_HZN_RGB) <= 48, "the %s tick: %06X", i ? "sunset" : "sunrise",
              (unsigned)rgb_of(bg[py * WX_FACE + px]));
        px = (int)(233 + 190 * dx);
        py = (int)(233 + 190 * dy);
        CHECK(bg[py * WX_FACE + px] == 0, "the tick ends at r 199");
    }
    CHECK(bg[WX_SUN_HZ_Y * WX_FACE + 233] == wx_rgb565(0x9AA4B0) && bg[WX_SUN_HZ_Y * WX_FACE + 130] == wx_rgb565(0x9AA4B0),
          "the horizon");
    CHECK(bg[WX_SUN_HZ_Y * WX_FACE + 118] == 0 && bg[(WX_SUN_HZ_Y + 4) * WX_FACE + 233] == 0, "4.5 px wide, 125..341");
    CHECK(bg[(WX_SUN_HZ_Y + 8) * WX_FACE + 188] == wx_rgb565(0x5A5A5A) && bg[(WX_SUN_HZ_Y + 8) * WX_FACE + 278] == wx_rgb565(0x5A5A5A) &&
              bg[(WX_SUN_HZ_Y + 8) * WX_FACE + 233] == 0,
          "two dashes under it");
    CHECK(bg[150 * WX_FACE + 233] == 0 && bg[300 * WX_FACE + 233] == 0, "the sun and the rays are not in the background");
    float dx, dy;
    wx_face_dot_xy(WXF_SUN, WXF_N, &dx, &dy);
    CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_ON), "the Sun dot is white");
    wx_face_dot_xy(WXF_DAYS, WXF_N, &dx, &dy);
    CHECK(bg[(int)dy * WX_FACE + (int)dx] == wx_rgb565(WX_DOT_OFF), "the others grey");

    wx_sun_layout_t l;
    wx_sun_layout(&l);
    int rw = l.rays.x1 - l.rays.x0, sw = l.sun.x1 - l.sun.x0, sh = l.sun.y1 - l.sun.y0;
    const uint8_t *ra = m + l.rays_at;
    CHECK(ra[(100 - l.rays.y0) * rw + 233 - l.rays.x0] > 60, "the upright ray: %d", ra[(100 - l.rays.y0) * rw + 233 - l.rays.x0]);
    float a6 = (-168.0f + 6.5f * 13.0f) * (float)M_PI / 180.0f;
    int bx = (int)(233 + 90 * cosf(a6)), by = (int)(178 + 90 * sinf(a6));
    CHECK(ra[(by - l.rays.y0) * rw + bx - l.rays.x0] == 0, "dark between two rays");
    CHECK(ra[(150 - l.rays.y0) * rw + 233 - l.rays.x0] == 0, "no ray inside the sun");
    const uint16_t *sp = (const uint16_t *)(m + l.sun_at);
    const uint8_t *sa = m + l.sun_at + (size_t)sw * sh * 2;
    int c = (170 - l.sun.y0) * sw + 233 - l.sun.x0;
    CHECK(sa[c] >= 230 && sa[c] <= 240 && channel_diff(rgb_of(sp[c]), 0xFFD36B) <= 16, "the sun's core: %06X a %d", (unsigned)rgb_of(sp[c]), sa[c]);
    int g = (178 - 60 - l.sun.y0) * sw + 233 - l.sun.x0;
    CHECK(sa[g] > 0 && sa[g] < 40, "the glow at r 60: a %d", sa[g]);
    CHECK(sa[2] == 0, "the corner clear");
    CHECK(sa[(sh - 1) * sw + 233 - l.sun.x0] > 200, "the sun reaches the horizon cut");

    o.sunrise = o.sunset = 0;
    wx_temp_face(&o, NULL, WXF_SUN, &t);
    wx_bake(&t, WX_BAKE_FULL, &b, &out);
    CHECK(lut[0] == wx_rgb565(0x262626) && lut[WX_RING_LUT / 2] == wx_rgb565(0x262626), "no sun times: the grey track");
    float x, y;
    wx_gauge_xy(0, &x, &y);
    int px = (int)(233 + 204.5f * (x - 233) / 222), py = (int)(233 + 204.5f * (y - 233) / 222);
    CHECK(bg[py * WX_FACE + px] == 0, "and no ticks");
    CHECK(bg[WX_SUN_HZ_Y * WX_FACE + 233] == wx_rgb565(0x9AA4B0), "the horizon stays");
    free(m);
    free(hero);
    free(drift);
}

int main(void)
{
    test_as2();
    test_phases();
    test_round();
    test_shift();
    test_crossings();
    test_polar();
    test_rgb();
    test_day_edge();
    test_hero();
    test_same();
    test_pick();
    test_frame();
    test_layout();
    test_areas();
    test_paint();
    if (failures) {
        printf("sun: %d failure(s)\n", failures);
        return 1;
    }
    printf("sun: all passed\n");
    return 0;
}
