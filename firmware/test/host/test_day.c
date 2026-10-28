#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "day.h"
#include "knob_view.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *dirs[2];

static char *load(const char *name)
{
    char path[1024];
    FILE *f = NULL;
    for (int i = 0; i < 2 && !f; i++) {
        snprintf(path, sizeof path, "%s/%s", dirs[i], name);
        f = fopen(path, "rb");
    }
    if (!f) {
        failures++;
        printf("FAIL: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    size_t got = s ? fread(s, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!s) return NULL;
    s[got] = 0;
    return s;
}

static day_block_t fixture(const char *name)
{
    char *j = load(name);
    knob_view_t *v = calloc(1, sizeof *v);
    day_block_t b;
    memset(&b, 0, sizeof b);
    CHECK(j && v && knob_view_parse(j, v), "%s parses as a knob view", name);
    CHECK(v && v->has_day, "%s has a day block", name);
    if (v) b = v->day;
    free(v);
    free(j);
    return b;
}

#define HM(h, m) ((h) * 60 + (m))

static void test_time(void)
{
    CHECK(day_hhmm_parse("00:00") == 0 && day_hhmm_parse("23:59") == 1439 && day_hhmm_parse("09:52") == HM(9, 52),
          "HH:MM");
    const char *bad[] = {"24:00", "9:52", "09:5", "09:60", "0952", "09-52", "09:52 ", "", "ab:cd", NULL};
    for (int i = 0; bad[i]; i++) CHECK(day_hhmm_parse(bad[i]) == DAY_NONE, "bad %s", bad[i]);
    CHECK(day_hhmm_parse(NULL) == DAY_NONE, "NULL");
    char buf[8];
    day_hhmm(HM(0, 5), buf, sizeof buf);
    CHECK(strcmp(buf, "00:05") == 0, "%s", buf);
    day_hhmm(DAY_NONE, buf, sizeof buf);
    CHECK(strcmp(buf, "--:--") == 0, "%s", buf);
    CHECK(day_fwd(HM(23, 0), HM(1, 0)) == 120 && day_fwd(HM(1, 0), HM(23, 0)) == 1320 && day_fwd(5, 5) == 0, "fwd");
    CHECK(day_step(HM(23, 45), 1, 15) == 0 && day_step(0, -1, 15) == HM(23, 45), "wraps at midnight");
    CHECK(day_step(HM(23, 50), 1, 15) == 0 && day_step(HM(23, 50), -1, 15) == HM(23, 45), "off-grid snaps first");
    CHECK(day_step(HM(23, 50), 2, 15) == 15 && day_step(HM(23, 50), -2, 15) == HM(23, 30), "then steps");
    CHECK(day_step(HM(9, 52), 1, 5) == HM(9, 55) && day_step(HM(9, 52), -1, 5) == HM(9, 50), "5-min grid");
    CHECK(day_step(HM(10, 0), 0, 15) == HM(10, 0), "no step");
    CHECK(day_scale(DAY_ACCENT, 0.3f) == 0x4D2012 && day_scale(DAY_ACCENT, 1.0f) == DAY_ACCENT, "scale %06X",
          (unsigned)day_scale(DAY_ACCENT, 0.3f));
}

static void test_parse(void)
{
    day_block_t b = fixture("view_day_on_early.json");
    CHECK(b.state == DAY_ON && b.started == HM(10, 15) && b.stop_at == 0 && b.on_min == 293 && b.break_min == 34 &&
              b.over_min == 0 && b.offer_start == DAY_NONE && b.agents == 2 && !b.break_hint && !b.summary.has,
          "on");
    CHECK(b.n_breaks == 1 && b.breaks[0][0] == HM(13, 0) && b.breaks[0][1] == HM(13, 34), "on: one finished break");
    b = fixture("view_day_break.json");
    CHECK(b.state == DAY_BREAK && b.started == HM(12, 0) && b.on_min == 210 && b.break_min == 12 && b.n_breaks == 1 &&
              b.breaks[0][0] == HM(14, 0) && b.breaks[0][1] == HM(14, 35),
          "Ember's break golden: on a second break, the first in breaks");
    b = fixture("view_day_on.json");
    CHECK(b.state == DAY_ON && b.started == HM(12, 0) && b.on_min == 860 && b.over_min == 140 && b.break_hint &&
              b.agents == 0 && b.has_utc_offset && b.utc_offset == 0,
          "Ember's on golden: past the stop");
    b = fixture("view_day_offer.json");
    CHECK(b.state == DAY_OFF && b.offer_start == HM(9, 52) && b.started == DAY_NONE, "offer");
    b = fixture("view_day_summary.json");
    CHECK(b.summary.has && b.summary.on_min == 860 && !b.summary.in_range && b.summary.week_in_range == 0 &&
              b.summary.week_days == 1 && b.agents == 0 && b.started == DAY_NONE,
          "summary");
    b = fixture("view_day_set_replies.json");
    CHECK(b.replies_off_last_night == 2 && b.stop_at == HM(23, 45), "replies");

    knob_view_t *v = calloc(1, sizeof *v);
    CHECK(knob_view_parse("{\"v\":1}", v) && !v->has_day, "no block");
    CHECK(knob_view_parse("{\"v\":1,\"day\":null}", v) && !v->has_day, "null block");
    CHECK(knob_view_parse("{\"v\":1,\"day\":{\"state\":\"lunch\"}}", v) && !v->has_day, "unknown state");
    CHECK(knob_view_parse("{\"v\":1,\"day\":{\"stop_at\":\"00:00\"}}", v) && !v->has_day, "no state");
    free(v);

    cJSON *o = cJSON_Parse("{\"state\":\"on\",\"started\":\"9:00\",\"stop_at\":7,\"on_min\":-3,\"break_min\":\"5\","
                           "\"over_min\":1e9,\"agents\":-1,\"break_hint\":1,\"summary\":[]}");
    CHECK(day_parse(o, &b), "a known state parses");
    CHECK(b.started == DAY_NONE && b.stop_at == DAY_NONE && b.on_min == 0 && b.break_min == 0 &&
              b.over_min == DAY_COUNT_MAX && b.agents == DAY_NONE && !b.break_hint && !b.summary.has,
          "bad fields read as absent, counts clamp: %d %d %d", b.started, b.over_min, b.agents);
    cJSON_Delete(o);
    CHECK(!day_parse(NULL, &b) && b.stop_at == DAY_NONE && b.offer_start == DAY_NONE, "not an object: times absent");

    o = cJSON_Parse("{\"state\":\"off\",\"utc_offset\":-12600}");
    CHECK(day_parse(o, &b) && b.has_utc_offset && b.utc_offset == -12600, "utc_offset in seconds: %d", b.utc_offset);
    cJSON_Delete(o);
    const char *bad_off[] = {"{\"state\":\"off\",\"utc_offset\":\"3600\"}", "{\"state\":\"off\",\"utc_offset\":3600.5}",
                             "{\"state\":\"off\",\"utc_offset\":64801}", "{\"state\":\"off\"}", NULL};
    for (int i = 0; bad_off[i]; i++) {
        o = cJSON_Parse(bad_off[i]);
        CHECK(day_parse(o, &b) && !b.has_utc_offset, "no zone from %s", bad_off[i]);
        cJSON_Delete(o);
    }

    o = cJSON_Parse("{\"state\":\"on\",\"breaks\":[[\"13:00\",\"13:30\"],[\"14:00\"],[\"15:00\",\"25:00\"],\"x\","
                    "[1,2],[\"16:00\",\"16:10\",\"x\"],[\"17:00\",\"17:05\"]]}");
    CHECK(day_parse(o, &b) && b.n_breaks == 2 && b.breaks[0][0] == HM(13, 0) && b.breaks[1][1] == HM(17, 5),
          "only well-formed HH:MM pairs count: %d", b.n_breaks);
    cJSON_Delete(o);
    o = cJSON_Parse("{\"state\":\"on\",\"breaks\":{\"a\":[\"13:00\",\"13:30\"]}}");
    CHECK(day_parse(o, &b) && b.n_breaks == 0, "breaks not an array: none");
    cJSON_Delete(o);
    char big[2048];
    int n = snprintf(big, sizeof big, "{\"state\":\"on\",\"breaks\":[");
    for (int i = 0; i < DAY_BREAKS + 3; i++)
        n += snprintf(big + n, sizeof big - (size_t)n, "%s[\"%02d:00\",\"%02d:05\"]", i ? "," : "", i, i);
    snprintf(big + n, sizeof big - (size_t)n, "]}");
    o = cJSON_Parse(big);
    CHECK(day_parse(o, &b) && b.n_breaks == DAY_BREAKS && b.breaks[DAY_BREAKS - 1][0] == HM(DAY_BREAKS - 1, 0),
          "at most DAY_BREAKS kept: %d", b.n_breaks);
    cJSON_Delete(o);
}

static day_in_t at_now(const day_block_t *b, int now)
{
    return (day_in_t){.b = b, .fed = true, .now_min = now, .now_exact = now != DAY_NONE};
}

static day_face_t face(day_ui_t *ui, const day_in_t *in, double t)
{
    day_ui_sync(ui, in, t);
    return day_ui_face(ui, in, t);
}

static bool act_is(const day_act_t *a, day_act_kind_t k, int stop, int at)
{
    return a->kind == k && a->stop_at == stop && a->at == at;
}

static void test_off_set(void)
{
    day_block_t b = fixture("view_day_off.json");
    day_ui_t ui;
    day_ui_init(&ui);
    day_in_t in = at_now(&b, HM(10, 15));
    day_act_t a[DAY_ACTS];
    CHECK(face(&ui, &in, 10) == DAY_FACE_OFF, "off");
    CHECK(day_ui_turn(&ui, &in, 3, 10, a) == 0 && face(&ui, &in, 10) == DAY_FACE_OFF, "off: turn does nothing");
    CHECK(day_ui_long(&ui, &in, 10, a) == 0 && face(&ui, &in, 10) == DAY_FACE_OFF, "off: long push does nothing");
    CHECK(day_ui_push(&ui, &in, 10, a) == 0 && face(&ui, &in, 10) == DAY_FACE_SET && ui.set_stop == 0,
          "off: push opens Set at the view's stop time");
    CHECK(day_ui_turn(&ui, &in, -3, 11, a) == 0 && ui.set_stop == HM(23, 15), "set: turn -3 = 23:15: %d", ui.set_stop);
    CHECK(day_ui_turn(&ui, &in, 1, 12, a) == 0 && ui.set_stop == HM(23, 30), "set: turn +1");
    CHECK(day_ui_push(&ui, &in, 13, a) == 1 && act_is(&a[0], DAY_ACT_START, HM(23, 30), DAY_NONE),
          "set: push starts with the chosen stop");
    CHECK(face(&ui, &in, 13.2) == DAY_FACE_SET, "Set stays until the view says on");
    day_block_t on = fixture("view_day_on_early.json");
    day_in_t in_on = at_now(&on, HM(15, 42));
    CHECK(face(&ui, &in_on, 14) == DAY_FACE_ON && !ui.set, "the view's on closes Set");
    CHECK(face(&ui, &in, 15) == DAY_FACE_OFF, "back off: Off, not Set");

    day_ui_init(&ui);
    day_ui_push(&ui, &in, 100, a);
    CHECK(day_ui_long(&ui, &in, 101, a) == 0 && face(&ui, &in, 101) == DAY_FACE_OFF, "set: long push goes Off");
    day_ui_push(&ui, &in, 200, a);
    day_ui_turn(&ui, &in, 1, 230, a);
    CHECK(face(&ui, &in, 230 + DAY_SET_IDLE_S - 1) == DAY_FACE_SET, "set: open within the idle time");
    CHECK(face(&ui, &in, 230 + DAY_SET_IDLE_S + 1) == DAY_FACE_OFF, "set: closes after DAY_SET_IDLE_S without input");

    day_block_t r = fixture("view_day_set_replies.json");
    day_in_t in_r = at_now(&r, HM(9, 0));
    day_ui_init(&ui);
    day_ui_push(&ui, &in_r, 1, a);
    CHECK(ui.set_stop == HM(23, 45), "Set opens at the last stop time used");
    b.stop_at = DAY_NONE;
    day_ui_init(&ui);
    day_ui_push(&ui, &in, 1, a);
    CHECK(ui.set_stop == 0, "no stop time in the view: 00:00");
}

static void test_on_break_past(void)
{
    day_block_t b = fixture("view_day_on_early.json");
    day_ui_t ui;
    day_ui_init(&ui);
    day_in_t in = at_now(&b, HM(15, 42));
    day_act_t a[DAY_ACTS];
    CHECK(face(&ui, &in, 0) == DAY_FACE_ON, "on");
    CHECK(day_ui_turn(&ui, &in, -2, 1, a) == 0 && ui.stop == HM(23, 30) && ui.stop_dirty, "on: turn moves the stop locally");
    CHECK(day_ui_tick(&ui, &in, 1 + DAY_TURN_DEBOUNCE_S - 0.1, a) == 0, "debounced");
    CHECK(day_ui_turn(&ui, &in, 1, 2, a) == 0 && ui.stop == HM(23, 45), "another detent restarts the debounce");
    CHECK(day_ui_tick(&ui, &in, 2 + DAY_TURN_DEBOUNCE_S - 0.1, a) == 0, "still debounced");
    CHECK(day_ui_tick(&ui, &in, 2 + DAY_TURN_DEBOUNCE_S, a) == 1 && act_is(&a[0], DAY_ACT_STOP_AT, HM(23, 45), DAY_NONE),
          "one PUT with the last value");
    CHECK(day_ui_tick(&ui, &in, 10, a) == 0, "sent once");
    CHECK(day_ui_leave(&ui, 10, a) == 0, "nothing pending: leaving sends nothing");
    day_ui_turn(&ui, &in, 1, 11, a);
    CHECK(day_ui_leave(&ui, 11.2, a) == 1 && act_is(&a[0], DAY_ACT_STOP_AT, 0, DAY_NONE),
          "leaving the page inside the debounce sends the turned stop at once");
    CHECK(day_ui_tick(&ui, &in, 20, a) == 0, "and not again when the page shows");
    day_ui_turn(&ui, &in, -1, 3, a);
    day_ui_tick(&ui, &in, 3 + DAY_TURN_DEBOUNCE_S, a);
    day_model_t m;
    day_ui_sync(&ui, &in, 4);
    day_model(&ui, &in, 4, NULL, &m);
    CHECK(strcmp(m.line3, "until 23:45") == 0 && m.tick == HM(23, 45), "the turned stop shows until the view has it: %s",
          m.line3);
    day_ui_sync(&ui, &in, 4 + DAY_TURN_DEBOUNCE_S + DAY_STOP_HOLD_S);
    CHECK(ui.stop == DAY_NONE, "the view's stop again after DAY_STOP_HOLD_S");
    day_ui_turn(&ui, &in, -1, 20, a);
    b.stop_at = HM(23, 45);
    day_ui_tick(&ui, &in, 20 + DAY_TURN_DEBOUNCE_S, a);
    day_ui_sync(&ui, &in, 22);
    CHECK(ui.stop == DAY_NONE, "or once the view has it");
    b.stop_at = 0;

    day_ui_init(&ui);
    day_ui_turn(&ui, &in, 1, 30, a);
    int n = day_ui_push(&ui, &in, 30.5, a);
    CHECK(n == 2 && act_is(&a[0], DAY_ACT_STOP_AT, HM(0, 15), DAY_NONE) && a[1].kind == DAY_ACT_BREAK,
          "a push right after a turn sends the stop first: %d", n);
    CHECK(day_ui_tick(&ui, &in, 40, a) == 0, "and not again");
    day_ui_init(&ui);
    CHECK(day_ui_push(&ui, &in, 50, a) == 1 && a[0].kind == DAY_ACT_BREAK, "on: push = break");
    CHECK(day_ui_long(&ui, &in, 50, a) == 1 && a[0].kind == DAY_ACT_STOP, "on: long push = clock out");
    day_ui_turn(&ui, &in, -1, 60, a);
    n = day_ui_long(&ui, &in, 60.2, a);
    CHECK(n == 2 && a[0].kind == DAY_ACT_STOP_AT && a[1].kind == DAY_ACT_STOP, "clock out right after a turn: stop first");

    day_block_t br = fixture("view_day_break.json");
    day_in_t in_b = at_now(&br, HM(15, 57));
    day_ui_init(&ui);
    CHECK(face(&ui, &in_b, 0) == DAY_FACE_BREAK, "break");
    CHECK(day_ui_turn(&ui, &in_b, 2, 0, a) == 0 && ui.stop == DAY_NONE && !ui.stop_dirty, "break: turn does nothing");
    CHECK(day_ui_push(&ui, &in_b, 0, a) == 1 && a[0].kind == DAY_ACT_RESUME, "break: push resumes");
    CHECK(day_ui_long(&ui, &in_b, 0, a) == 1 && a[0].kind == DAY_ACT_STOP, "break: long push clocks out");

    day_block_t p = fixture("view_day_on.json");
    day_in_t in_p = at_now(&p, HM(2, 20));
    day_ui_init(&ui);
    CHECK(face(&ui, &in_p, 0) == DAY_FACE_PAST, "past");
    CHECK(day_ui_push(&ui, &in_p, 0, a) == 1 && a[0].kind == DAY_ACT_BREAK, "past: push = break");
    CHECK(day_ui_long(&ui, &in_p, 0, a) == 1 && a[0].kind == DAY_ACT_STOP, "past: long push = clock out");
    day_ui_turn(&ui, &in_p, 2, 1, a);
    CHECK(ui.stop == HM(0, 30) && face(&ui, &in_p, 1) == DAY_FACE_PAST, "a stop still before now: past");
    day_ui_turn(&ui, &in_p, 8, 2, a);
    CHECK(ui.stop == HM(2, 30) && face(&ui, &in_p, 2) == DAY_FACE_ON, "turning the stop past now is On again");
    day_ui_init(&ui);
    in_p.now_min = DAY_NONE;
    CHECK(face(&ui, &in_p, 0) == DAY_FACE_PAST, "past without a clock: from over_min");
    in_p.age_s = 125;
    day_model_t pm;
    day_model(&ui, &in_p, 0, NULL, &pm);
    CHECK(strcmp(pm.hero, "02:22") == 0, "the clock time runs on from the view: %s", pm.hero);

    day_ui_init(&ui);
    in.now_min = HM(10, 15);
    day_ui_turn(&ui, &in, -1, 0, a);
    CHECK(ui.stop == HM(23, 45) && face(&ui, &in, 0) == DAY_FACE_ON,
          "in the morning, a stop turned from 00:00 to 23:45 is still ahead, not last night");
    day_ui_turn(&ui, &in, 1, 0, a);
    day_ui_turn(&ui, &in, 1, 0, a);
    CHECK(ui.stop == HM(0, 15) && face(&ui, &in, 0) == DAY_FACE_ON, "and turned past midnight: ahead too");
    in.now_min = HM(23, 50);
    day_ui_turn(&ui, &in, -2, 0, a);
    CHECK(ui.stop == HM(23, 45) && face(&ui, &in, 0) == DAY_FACE_PAST, "late evening, turned to 23:45: past");
    day_ui_init(&ui);
    in.now_min = HM(10, 15);
    in.now_exact = false;
    day_ui_turn(&ui, &in, -40, 0, a);
    CHECK(ui.stop == HM(14, 0) && face(&ui, &in, 0) == DAY_FACE_ON,
          "a weather-zone clock does not judge a turned stop past: %d", ui.stop);
    in.now_exact = true;

    day_ui_init(&ui);
    in.now_min = HM(15, 42);
    b.stop_at = HM(18, 0);
    day_ui_turn(&ui, &in, 1, 0, a);
    day_ui_tick(&ui, &in, DAY_TURN_DEBOUNCE_S, a);
    day_ui_turn(&ui, &in, 1, 2, a);
    day_ui_tick(&ui, &in, 2 + DAY_TURN_DEBOUNCE_S, a);
    CHECK(ui.stop == HM(18, 30), "two PUTs: 18:15, then 18:30");
    b.stop_at = HM(18, 15);
    day_ui_sync(&ui, &in, 3.6);
    CHECK(ui.stop == HM(18, 30) && ui.stop_base == HM(18, 15) && ui.stop_delta == 15,
          "the first PUT's view keeps the newer target, rebased: %d base %d delta %d", ui.stop, ui.stop_base, ui.stop_delta);
    day_ui_turn(&ui, &in, 1, 4, a);
    CHECK(ui.stop == HM(18, 45), "the next detent goes on from the newer target: %d", ui.stop);
    b.stop_at = 0;

    day_ui_init(&ui);
    in.now_min = HM(0, 1);
    CHECK(face(&ui, &in, 0) == DAY_FACE_ON, "past the stop by the knob's clock but not by Ember's: on until the view says");
    b.over_min = 1;
    CHECK(face(&ui, &in, 0) == DAY_FACE_PAST, "Ember's over_min decides: past");
    b.over_min = 0;

    day_block_t early = b;
    early.started = HM(0, 10);
    early.stop_at = 0;
    early.on_min = 5;
    early.over_min = 15;
    day_in_t in_e = at_now(&early, HM(0, 15));
    day_ui_init(&ui);
    CHECK(face(&ui, &in_e, 0) == DAY_FACE_PAST, "a stop before the clock-in in Ember's workday: Ember's overtime stands");
    day_ui_turn(&ui, &in_e, -1, 1, a);
    CHECK(ui.stop == HM(23, 45) && face(&ui, &in_e, 1) == DAY_FACE_PAST, "a stop turned earlier: still past");
    day_ui_turn(&ui, &in_e, 2, 2, a);
    CHECK(ui.stop == HM(0, 15) && face(&ui, &in_e, 2) == DAY_FACE_ON, "a stop turned to now: on");
    in_e.now_min = DAY_NONE;
    day_ui_init(&ui);
    day_ui_turn(&ui, &in_e, 2, 3, a);
    CHECK(ui.stop == HM(0, 30) && face(&ui, &in_e, 3) == DAY_FACE_ON, "no clock: Ember's overtime less the turn: on");
    day_ui_init(&ui);
    day_ui_turn(&ui, &in_e, -1, 4, a);
    CHECK(face(&ui, &in_e, 4) == DAY_FACE_PAST, "no clock: an earlier stop adds to Ember's overtime");
}

static void test_offer_summary(void)
{
    day_block_t b = fixture("view_day_offer.json");
    day_ui_t ui;
    day_ui_init(&ui);
    day_in_t in = at_now(&b, HM(9, 57));
    day_act_t a[DAY_ACTS];
    CHECK(face(&ui, &in, 0) == DAY_FACE_OFFER && ui.offer_at == HM(9, 52), "offer");
    day_ui_turn(&ui, &in, -1, 1, a);
    CHECK(ui.offer_at == HM(9, 50), "offer: turn -1 = 09:50: %d", ui.offer_at);
    day_ui_turn(&ui, &in, 3, 2, a);
    CHECK(ui.offer_at == HM(9, 57), "never after now: %d", ui.offer_at);
    day_ui_turn(&ui, &in, -2, 3, a);
    CHECK(ui.offer_at == HM(9, 50), "back: %d", ui.offer_at);
    day_ui_turn(&ui, &in, -200, 4, a);
    CHECK(ui.offer_at == HM(9, 50), "not more than 12 h back: %d", ui.offer_at);
    day_in_t nc = in;
    nc.now_min = DAY_NONE;
    day_ui_turn(&ui, &nc, 3, 4.5, a);
    CHECK(ui.offer_at == HM(9, 52), "no clock: never after the first reply: %d", ui.offer_at);
    day_ui_turn(&ui, &nc, -1, 4.6, a);
    CHECK(ui.offer_at == HM(9, 50), "no clock: back still works: %d", ui.offer_at);
    day_in_t wz = in;
    wz.now_min = HM(11, 0);
    wz.now_exact = false;
    day_ui_turn(&ui, &wz, 6, 4.7, a);
    CHECK(ui.offer_at == HM(9, 52), "a weather-zone clock: still never after the first reply: %d", ui.offer_at);
    day_ui_turn(&ui, &in, -1, 4.8, a);
    CHECK(day_ui_push(&ui, &in, 5, a) == 1 && act_is(&a[0], DAY_ACT_START, 0, HM(9, 50)),
          "offer: push starts back-dated with the view's stop");
    CHECK(day_ui_long(&ui, &in, 5, a) == 1 && a[0].kind == DAY_ACT_DISMISS, "offer: long push dismisses");
    day_block_t b2 = b;
    b2.offer_start = HM(9, 40);
    day_in_t in2 = at_now(&b2, HM(9, 57));
    CHECK(face(&ui, &in2, 6) == DAY_FACE_OFFER && ui.offer_at == HM(9, 40), "a new offer resets the adjustment");
    b2.offer_start = DAY_NONE;
    CHECK(face(&ui, &in2, 7) == DAY_FACE_OFF && ui.offer_at == DAY_NONE, "no offer: Off");
    day_ui_push(&ui, &in, 8, a);
    CHECK(face(&ui, &in, 8) == DAY_FACE_OFFER, "offer: push does not open Set");

    day_block_t s = fixture("view_day_summary.json");
    day_in_t in_s = at_now(&s, HM(23, 1));
    day_ui_init(&ui);
    CHECK(face(&ui, &in_s, 100) == DAY_FACE_SUMMARY, "summary");
    CHECK(day_ui_turn(&ui, &in_s, 1, 101, a) == 0 && day_ui_long(&ui, &in_s, 101, a) == 0 &&
              face(&ui, &in_s, 101) == DAY_FACE_SUMMARY,
          "summary: turn and long push do nothing");
    CHECK(face(&ui, &in_s, 100 + DAY_SUMMARY_S + 0.1) == DAY_FACE_OFF, "summary: shows DAY_SUMMARY_S");
    day_ui_init(&ui);
    face(&ui, &in_s, 200);
    CHECK(day_ui_push(&ui, &in_s, 201, a) == 0 && face(&ui, &in_s, 201) == DAY_FACE_OFF, "summary: push goes Off");
    CHECK(day_ui_push(&ui, &in_s, 202, a) == 0 && face(&ui, &in_s, 202) == DAY_FACE_SET, "then a push opens Set");
    day_block_t off = fixture("view_day_off.json");
    day_in_t in_off = at_now(&off, HM(23, 2));
    day_ui_init(&ui);
    face(&ui, &in_s, 300);
    face(&ui, &in_off, 301);
    CHECK(face(&ui, &in_s, 400) == DAY_FACE_SUMMARY, "a later summary shows again");
}

static void test_busy_and_wait(void)
{
    day_block_t b = fixture("view_day_on_early.json");
    day_ui_t ui;
    day_ui_init(&ui);
    day_in_t in = at_now(&b, HM(15, 42));
    in.busy = true;
    day_act_t a[DAY_ACTS];
    CHECK(day_ui_push(&ui, &in, 0, a) == 0 && day_ui_long(&ui, &in, 0, a) == 0, "busy: pushes dropped");
    day_ui_turn(&ui, &in, 1, 0, a);
    CHECK(ui.stop == HM(0, 15) && day_ui_tick(&ui, &in, DAY_TURN_DEBOUNCE_S, a) == 1, "busy: turns still count");
    day_in_t none = {.fed = false, .now_min = DAY_NONE};
    day_ui_init(&ui);
    CHECK(face(&ui, &none, 0) == DAY_FACE_WAIT && day_ui_push(&ui, &none, 0, a) == 0 &&
              day_ui_long(&ui, &none, 0, a) == 0 && day_ui_turn(&ui, &none, 1, 0, a) == 0,
          "no block: nothing");
    day_model_t m;
    day_model(&ui, &none, 0, NULL, &m);
    CHECK(strcmp(m.line2, "connecting") == 0 && strcmp(m.hero, "--:--") == 0, "before a view: %s", m.line2);
    none.fed = true;
    day_model(&ui, &none, 0, NULL, &m);
    CHECK(strcmp(m.line2, "day clock off") == 0, "a view without the block: %s", m.line2);
    day_ui_init(&ui);
    day_ui_turn(&ui, &in, 1, 0, a);
    day_block_t off = fixture("view_day_off.json");
    in.b = &off;
    CHECK(day_ui_tick(&ui, &in, 5, a) == 0 && !ui.stop_dirty, "the day ended before the debounce: nothing sent");
}

static void test_models(void)
{
    day_ui_t ui;
    day_model_t m;
    day_act_t a[DAY_ACTS];

    day_block_t b = fixture("view_day_off.json");
    day_in_t in = at_now(&b, HM(10, 15));
    day_ui_init(&ui);
    day_model(&ui, &in, 0, NULL, &m);
    CHECK(m.face == DAY_FACE_OFF && strcmp(m.hero, "off") == 0 && !m.hero_digits && m.hero_rgb == DAY_OFF_TEXT &&
              strcmp(m.line2, "2 agents working") == 0 && m.moon && m.off_ring && !m.marks && m.n_segs == 0 &&
              m.tick == DAY_NONE && m.dot == DAY_NONE,
          "off face: %s / %s", m.hero, m.line2);
    b.agents = 1;
    day_model(&ui, &in, 0, NULL, &m);
    CHECK(strcmp(m.line2, "1 agent working") == 0, "%s", m.line2);
    b.agents = 0;
    day_model(&ui, &in, 0, NULL, &m);
    CHECK(strcmp(m.line2, "no agents working") == 0, "%s", m.line2);
    b.agents = DAY_NONE;
    day_model(&ui, &in, 0, NULL, &m);
    CHECK(m.line2[0] == 0, "no count: %s", m.line2);
    b.agents = 3;

    day_ui_push(&ui, &in, 0, a);
    day_model(&ui, &in, 0, NULL, &m);
    CHECK(m.face == DAY_FACE_SET && strcmp(m.hero, "00:00") == 0 && m.hero_digits && strcmp(m.line2, "stop at") == 0 &&
              strcmp(m.line3, "13 h 45") == 0 && m.marks && m.n_segs == 1 && m.seg[0].from == HM(10, 15) &&
              m.seg[0].to == 0 && m.seg[0].rgb == day_scale(DAY_ACCENT, 0.3f) && m.tick == 0 &&
              m.tick_rgb == DAY_ACCENT && m.dot == HM(10, 15),
          "set face (mockup 1): %s %s %s", m.hero, m.line2, m.line3);
    in.now_min = HM(23, 15);
    day_model(&ui, &in, 0, NULL, &m);
    CHECK(strcmp(m.line3, "45 min") == 0, "%s", m.line3);
    in.now_min = HM(10, 0);
    day_model(&ui, &in, 0, NULL, &m);
    CHECK(strcmp(m.line3, "14 h") == 0, "%s", m.line3);
    in.now_min = DAY_NONE;
    day_model(&ui, &in, 0, NULL, &m);
    CHECK(m.line3[0] == 0 && m.n_segs == 0 && m.dot == DAY_NONE && m.tick == 0, "no clock: no length, no head");

    day_block_t r = fixture("view_day_set_replies.json");
    day_in_t in_r = at_now(&r, HM(10, 15));
    day_ui_init(&ui);
    day_ui_push(&ui, &in_r, 0, a);
    day_model(&ui, &in_r, 0, NULL, &m);
    CHECK(strcmp(m.hero, "23:45") == 0 && strcmp(m.line3, "2 replies off the clock last night") == 0,
          "set face after an off-clock night (mockup 8): %s", m.line3);
    r.replies_off_last_night = 1;
    day_model(&ui, &in_r, 0, NULL, &m);
    CHECK(strcmp(m.line3, "1 reply off the clock last night") == 0, "%s", m.line3);

    day_block_t on = fixture("view_day_on_early.json");
    day_in_t in_on = at_now(&on, HM(15, 42));
    day_ui_init(&ui);
    day_model(&ui, &in_on, 0, NULL, &m);
    CHECK(m.face == DAY_FACE_ON && strcmp(m.hero, "4:53") == 0 && strcmp(m.line2, "on the clock") == 0 &&
              strcmp(m.line3, "until 00:00") == 0 && m.n_segs == 3 && m.seg[0].from == HM(10, 15) &&
              m.seg[0].to == HM(13, 0) && m.seg[0].rgb == DAY_ACCENT && m.seg[1].from == HM(13, 34) &&
              m.seg[1].to == HM(15, 42) && m.seg[1].rgb == DAY_ACCENT && m.seg[2].from == HM(15, 42) &&
              m.seg[2].to == 0 && m.tick == 0 && m.dot == HM(15, 42) && m.dot_rgb == DAY_TEXT && !m.glow,
          "on face (mockup 2), the break a gap: %s %s %s, %d segs", m.hero, m.line2, m.line3, m.n_segs);
    in_on.age_s = 125;
    day_model(&ui, &in_on, 0, NULL, &m);
    CHECK(strcmp(m.hero, "4:55") == 0, "the count runs on between views: %s", m.hero);
    in_on.age_s = 0;
    on.break_hint = true;
    day_model(&ui, &in_on, 0, NULL, &m);
    CHECK(strcmp(m.line3, "time for a break") == 0, "break hint: %s", m.line3);
    on.break_hint = false;
    in_on.now_min = DAY_NONE;
    day_model(&ui, &in_on, 0, NULL, &m);
    CHECK(m.dot == HM(15, 42) && m.seg[1].to == HM(15, 42), "no clock: the head from start + on + break time: %d", m.dot);

    day_block_t br = fixture("view_day_break.json");
    day_in_t in_b = at_now(&br, HM(16, 17));
    day_model(&ui, &in_b, 0, NULL, &m);
    CHECK(m.face == DAY_FACE_BREAK && strcmp(m.hero, "0:12") == 0 && strcmp(m.line2, "break") == 0 &&
              strcmp(m.line3, "look far away") == 0 && m.n_segs == 3 && m.seg[0].from == HM(12, 0) &&
              m.seg[0].to == HM(14, 0) && m.seg[0].rgb == day_scale(DAY_ACCENT, 0.55f) && m.seg[1].from == HM(14, 35) &&
              m.seg[1].to == HM(16, 5) && m.seg[2].from == HM(16, 5) && m.seg[2].to == HM(16, 17) &&
              m.seg[2].rgb == DAY_BREAK_RGB && m.dot == HM(16, 17) && m.dot_rgb == DAY_BREAK_RGB && m.tick == DAY_NONE,
          "break face (mockup 3), the earlier break a gap: %s %s %s, %d segs", m.hero, m.line2, m.line3, m.n_segs);
    in_b.now_min = DAY_NONE;
    day_model(&ui, &in_b, 0, NULL, &m);
    CHECK(m.dot == HM(15, 42), "no clock on a break: the guess misses the earlier break: %d", m.dot);
    in_b.now_min = HM(16, 17);
    in_b.age_s = 61;
    day_model(&ui, &in_b, 0, NULL, &m);
    CHECK(strcmp(m.hero, "0:13") == 0, "the break count runs on: %s", m.hero);

    day_block_t p = fixture("view_day_on.json");
    day_in_t in_p = at_now(&p, HM(2, 20));
    day_model(&ui, &in_p, 0, NULL, &m);
    CHECK(m.face == DAY_FACE_PAST && strcmp(m.hero, "02:20") == 0 && strcmp(m.line2, "past your stop") == 0 &&
              strcmp(m.line3, "long push to end") == 0 && m.n_segs == 2 && m.seg[0].from == HM(12, 0) &&
              m.seg[0].to == 0 && m.seg[1].from == 0 && m.seg[1].to == HM(2, 20) &&
              m.seg[1].rgb == day_scale(DAY_ACCENT, 0.4f) && m.tick == 0 && m.tick_rgb == DAY_TEXT &&
              m.dot == HM(2, 20) && m.glow,
          "past face (mockup 4): %s %s %s", m.hero, m.line2, m.line3);
    day_block_t pb = p;
    pb.n_breaks = 2;
    pb.breaks[0][0] = HM(13, 0);
    pb.breaks[0][1] = HM(14, 0);
    pb.breaks[1][0] = HM(23, 50);
    pb.breaks[1][1] = HM(0, 10);
    day_in_t in_pb = at_now(&pb, HM(2, 20));
    day_model(&ui, &in_pb, 0, NULL, &m);
    CHECK(m.n_segs == 3 && m.seg[0].from == HM(12, 0) && m.seg[0].to == HM(13, 0) && m.seg[1].from == HM(14, 0) &&
              m.seg[1].to == HM(23, 50) && m.seg[1].rgb == DAY_ACCENT && m.seg[2].from == HM(0, 10) &&
              m.seg[2].to == HM(2, 20) && m.seg[2].rgb == day_scale(DAY_ACCENT, 0.4f),
          "past with breaks, one across the stop: gaps in both arcs: %d segs", m.n_segs);
    day_block_t pe = p;
    pe.started = HM(0, 10);
    pe.on_min = 5;
    pe.over_min = 15;
    day_in_t in_pe = at_now(&pe, HM(0, 15));
    day_model(&ui, &in_pe, 0, NULL, &m);
    CHECK(m.face == DAY_FACE_PAST && m.n_segs == 1 && m.seg[0].from == HM(0, 10) && m.seg[0].to == HM(0, 15) &&
              m.seg[0].rgb == day_scale(DAY_ACCENT, 0.4f),
          "a stop before the clock-in: all of it overtime, no arc around the dial: %d segs", m.n_segs);

    day_block_t s = fixture("view_day_summary.json");
    day_in_t in_s = at_now(&s, HM(23, 1));
    day_ui_init(&ui);
    day_model(&ui, &in_s, 0, NULL, &m);
    CHECK(m.face == DAY_FACE_SUMMARY && m.check && strcmp(m.hero, "14:20") == 0 &&
              strcmp(m.line2, "past your stop") == 0 && strcmp(m.line3, "0 of 1 day this week") == 0 &&
              m.line3_rgb == DAY_TEXT && !m.marks && m.n_segs == 0 && m.tick == 0 && m.dot == DAY_NONE,
          "Ember's summary golden: %s %s %s", m.hero, m.line2, m.line3);
    s.summary = (day_summary_t){.has = true, .on_min = 756, .in_range = true, .week_in_range = 4, .week_days = 5};
    s.started = HM(10, 15);
    day_model(&ui, &in_s, 0, NULL, &m);
    CHECK(strcmp(m.hero, "12:36") == 0 && strcmp(m.line2, "in range") == 0 &&
              strcmp(m.line3, "4 of 5 days this week") == 0 && m.n_segs == 1 && m.seg[0].from == HM(10, 15) &&
              m.seg[0].to == HM(23, 1),
          "summary face (mockup 5): %s / %s / %s", m.hero, m.line2, m.line3);

    day_block_t o = fixture("view_day_offer.json");
    day_in_t in_o = at_now(&o, HM(9, 57));
    day_ui_init(&ui);
    day_ui_sync(&ui, &in_o, 0);
    day_model(&ui, &in_o, 0, NULL, &m);
    CHECK(m.face == DAY_FACE_OFFER && strcmp(m.hero, "09:52") == 0 && strcmp(m.line2, "start from here?") == 0 &&
              strcmp(m.line3, "first reply seen") == 0 && m.n_segs == 1 && m.seg[0].from == HM(9, 52) &&
              m.seg[0].to == HM(9, 57) && m.dot == HM(9, 52) && m.tick == DAY_NONE,
          "offer face (mockup 7): %s %s %s", m.hero, m.line2, m.line3);

    day_model(&ui, &in_o, 0, "offline: not sent", &m);
    CHECK(strcmp(m.line3, "offline: not sent") == 0 && m.line3_rgb == DAY_NOTE, "a note replaces the third line");
}

static void test_actions(void)
{
    char buf[64];
    day_act_t a = {.kind = DAY_ACT_START, .stop_at = 0, .at = DAY_NONE};
    CHECK(day_act_body(&a, buf, sizeof buf) > 0 && strcmp(buf, "{\"stop_at\":\"00:00\"}") == 0, "start: %s", buf);
    a.at = HM(9, 50);
    CHECK(day_act_body(&a, buf, sizeof buf) > 0 && strcmp(buf, "{\"at\":\"09:50\",\"stop_at\":\"00:00\"}") == 0,
          "back-dated start: %s", buf);
    CHECK(day_act_body(&a, buf, 20) == -1, "too small");
    a = (day_act_t){.kind = DAY_ACT_STOP_AT, .stop_at = HM(23, 45), .at = DAY_NONE};
    CHECK(day_act_body(&a, buf, sizeof buf) > 0 && strcmp(buf, "{\"stop_at\":\"23:45\"}") == 0, "stop_at: %s", buf);
    a.stop_at = DAY_NONE;
    CHECK(day_act_body(&a, buf, sizeof buf) == -1, "no stop time");
    a = (day_act_t){.kind = DAY_ACT_START, .stop_at = 0, .at = 1440};
    CHECK(day_act_body(&a, buf, sizeof buf) == -1, "bad start time");
    day_act_kind_t empty[] = {DAY_ACT_BREAK, DAY_ACT_RESUME, DAY_ACT_STOP, DAY_ACT_DISMISS};
    for (int i = 0; i < 4; i++) {
        a = (day_act_t){.kind = empty[i], .stop_at = DAY_NONE, .at = DAY_NONE};
        CHECK(day_act_body(&a, buf, sizeof buf) == 2 && strcmp(buf, "{}") == 0 && !day_act_put(empty[i]), "%s: %s",
              day_act_path(empty[i]), buf);
    }
    a.kind = DAY_ACT_NONE;
    CHECK(day_act_body(&a, buf, sizeof buf) == -1 && !day_act_path(DAY_ACT_NONE), "none");
    CHECK(strcmp(day_act_path(DAY_ACT_START), "start") == 0 && strcmp(day_act_path(DAY_ACT_BREAK), "break") == 0 &&
              strcmp(day_act_path(DAY_ACT_RESUME), "resume") == 0 && strcmp(day_act_path(DAY_ACT_STOP), "stop") == 0 &&
              strcmp(day_act_path(DAY_ACT_STOP_AT), "stop_at") == 0 &&
              strcmp(day_act_path(DAY_ACT_DISMISS), "offer/dismiss") == 0 && day_act_put(DAY_ACT_STOP_AT),
          "paths");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <Ember fixture dir> <day fixture dir>\n", argv[0]);
        return 2;
    }
    dirs[0] = argv[1];
    dirs[1] = argv[2];
    test_time();
    test_parse();
    test_off_set();
    test_on_break_past();
    test_offer_summary();
    test_busy_and_wait();
    test_models();
    test_actions();
    if (failures) {
        printf("day: %d failure(s)\n", failures);
        return 1;
    }
    printf("day: all tests passed\n");
    return 0;
}
