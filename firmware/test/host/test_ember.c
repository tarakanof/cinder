#include <stdio.h>
#include <string.h>

#include "dim.h"
#include "ember_host.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define T0 "2026-10-03T01:31:24.316342963+02:00"
#define T1 "2026-10-03T01:31:30.674070207+02:00"

static void test_priority(void)
{
    CHECK(ember_host_state_priority("waiting") == 0, "waiting");
    CHECK(ember_host_state_priority("error") == 1, "error");
    CHECK(ember_host_state_priority("running") == 2, "running");
    CHECK(ember_host_state_priority("done") == 3, "done");
    CHECK(ember_host_state_priority("idle") == 4, "idle inactive");
    CHECK(ember_host_state_priority("bogus") == 4, "unknown inactive");
    CHECK(ember_host_state_priority(NULL) == 4, "null inactive");
}

static void test_time(void)
{
    int64_t s;
    int32_t ns;
    CHECK(ember_host_parse_time("1970-01-01T00:00:00Z", &s, &ns) && s == 0 && ns == 0, "epoch");
    CHECK(ember_host_parse_time("2026-10-02T23:31:30.5Z", &s, &ns) && s == 1790983890 && ns == 500000000,
          "utc fraction: %lld %d", (long long)s, ns);
    CHECK(ember_host_parse_time("2026-10-03T01:31:30.5+02:00", &s, &ns) && s == 1790983890, "offset +02:00");
    CHECK(ember_host_parse_time("2026-10-02T18:31:30.5-05:00", &s, &ns) && s == 1790983890, "offset -05:00");
    CHECK(ember_host_parse_time(T1, &s, &ns) && ns == 674070207, "nanos");
    CHECK(ember_host_parse_time("2024-02-29T12:00:00Z", &s, &ns) && s == 1709208000, "leap day");
    CHECK(!ember_host_parse_time("", &s, &ns), "empty");
    CHECK(!ember_host_parse_time(NULL, &s, &ns), "null");
    CHECK(!ember_host_parse_time("2026-10-03T01:31:30", &s, &ns), "no zone");
    CHECK(!ember_host_parse_time("2026-10-03T01:31:30.Z", &s, &ns), "empty fraction");
    CHECK(!ember_host_parse_time("2026-13-03T01:31:30Z", &s, &ns), "month 13");
    CHECK(!ember_host_parse_time("2026-10-03T01:31:30Zjunk", &s, &ns), "trailing junk");
}

static void test_pick(void)
{
    ember_host_session_t a[] = {
        {"m4", "running", T1},
        {"m5", "running", T0},
    };
    CHECK(ember_host_pick_winning(a, 2) == 0, "newest running wins");
    ember_host_session_t b[] = {
        {"m4", "running", T1},
        {"m5", "waiting", T0},
        {"m6", "error", T1},
    };
    CHECK(ember_host_pick_winning(b, 3) == 1, "waiting beats error and newer running");
    ember_host_session_t c[] = {
        {"m4", "done", T1},
        {"m6", "error", T0},
        {"m7", "idle", T1},
    };
    CHECK(ember_host_pick_winning(c, 3) == 1, "error beats done; idle never wins");
    ember_host_session_t d[] = {{"m7", "idle", T1}, {"m8", "unknown", T1}};
    CHECK(ember_host_pick_winning(d, 2) == -1, "all inactive");
    CHECK(ember_host_pick_winning(NULL, 0) == -1, "empty");
    ember_host_session_t e[] = {{"a", "waiting", T1}, {"b", "waiting", T1}};
    CHECK(ember_host_pick_winning(e, 2) == 0, "tie keeps the first (strictly After)");
    ember_host_session_t f[] = {{"a", "done", "2026-10-03T01:31:30Z"}, {"b", "done", "2026-10-03T01:31:30.5Z"}};
    CHECK(ember_host_pick_winning(f, 2) == 1, "30.5 newer than 30");
    ember_host_session_t g[] = {{"a", "done", "2026-10-02T23:30:00Z"}, {"b", "done", "2026-10-03T00:00:00+02:00"}};
    CHECK(ember_host_pick_winning(g, 2) == 0, "zone-aware compare");
    ember_host_session_t h[] = {{"a", "done", T0}, {"b", "done", "garbage"}};
    CHECK(ember_host_pick_winning(h, 2) == 0, "unparsable time never replaces");
    ember_host_session_t i[] = {{"a", "done", "garbage"}, {"b", "done", T0}};
    CHECK(ember_host_pick_winning(i, 2) == 1, "timed replaces untimed");
}

static void test_label(void)
{
    char out[24];
    ember_host_session_t s[] = {{"m4", "running", T1}, {"m5", "waiting", T0}};
    ember_host_label("m4", true, s, 2, out, sizeof out);
    CHECK(strcmp(out, "M4") == 0, "render.source wins, uppercased: %s", out);
    ember_host_label("", true, s, 2, out, sizeof out);
    CHECK(strcmp(out, "") == 0, "present but empty: no fallback: %s", out);
    ember_host_label(NULL, false, s, 2, out, sizeof out);
    CHECK(strcmp(out, "M5") == 0, "fallback to the winner: %s", out);
    ember_host_label(NULL, false, NULL, 0, out, sizeof out);
    CHECK(strcmp(out, "") == 0, "no sessions");
    ember_host_session_t two[] = {{"m4", "waiting", T0}, {"m5", "waiting", T1}, {"m6", "running", T1}};
    ember_host_label(NULL, false, two, 3, out, sizeof out);
    CHECK(strcmp(out, "") == 0, "m4+m5 both waiting: no host: %s", out);
    ember_host_session_t same[] = {{"m4", "waiting", T0}, {"m4", "waiting", T1}, {"m6", "running", T1}};
    ember_host_label(NULL, false, same, 3, out, sizeof out);
    CHECK(strcmp(out, "M4") == 0, "two m4 waiting: M4: %s", out);
    ember_host_session_t miss[] = {{"m4", "error", T1}, {NULL, "error", T0}};
    ember_host_label(NULL, false, miss, 2, out, sizeof out);
    CHECK(strcmp(out, "") == 0, "missing source in the winning state: no host: %s", out);
    ember_host_session_t nos[] = {{NULL, "waiting", T0}};
    ember_host_label(NULL, false, nos, 1, out, sizeof out);
    CHECK(strcmp(out, "") == 0, "winner without source");
    ember_host_label("macbook-pro-16-inch", true, NULL, 0, out, sizeof out);
    CHECK(strcmp(out, "MACBOOK-PR") == 0 && strlen(out) == EMBER_HOST_MAX, "trimmed: %s", out);
    char small[4];
    ember_host_label("abcdef", true, NULL, 0, small, sizeof small);
    CHECK(strcmp(small, "ABC") == 0, "respects cap: %s", small);
    ember_host_label("m\xc3\xa4\n4", true, NULL, 0, out, sizeof out);
    CHECK(strcmp(out, "M4") == 0, "non-ASCII and control dropped: %s", out);
}

static void test_dim(void)
{
    CHECK(dim_level_valid(true, 0) && dim_level_valid(true, 255), "valid ends");
    CHECK(!dim_level_valid(false, 100) && !dim_level_valid(true, -1) && !dim_level_valid(true, 256), "invalid");

    dim_fade_t f;
    uint8_t v = 0;
    dim_fade_init(&f, 153);
    CHECK(!dim_fade_tick(&f, &v), "settled: nothing to write");
    CHECK(!dim_fade_set_target(&f, 153), "same level: no fade");

    CHECK(dim_fade_set_target(&f, 0), "fade starts");
    CHECK(f.target == DIM_FLOOR, "target clamped to floor");
    int ticks = 0, prev = 153;
    while (dim_fade_tick(&f, &v)) {
        ticks++;
        CHECK(v < prev && v >= DIM_FLOOR, "monotonic down, never below floor: %d", v);
        prev = v;
        if (ticks > 1000) break;
    }
    CHECK(ticks <= DIM_FADE_STEPS && ticks >= DIM_FADE_STEPS - 2 && v == DIM_FLOOR, "down in %d ticks (want ~%d), at %d", ticks, DIM_FADE_STEPS, v);

    dim_fade_t g;
    dim_fade_init(&g, 100);
    dim_fade_set_floor(&g, 60);
    dim_fade_set_target(&g, 20);
    while (dim_fade_tick(&g, &v)) {
    }
    CHECK(g.cur == 60, "custom floor holds: %d", g.cur);
    dim_fade_set_floor(&g, 0);
    CHECK(g.floor == 1, "floor >= 1");
    dim_fade_set_target(&f, 255);
    ticks = 0;
    while (dim_fade_tick(&f, &v)) ticks++;
    CHECK(ticks <= DIM_FADE_STEPS && v == 255, "up in %d ticks, at %d", ticks, v);

    dim_fade_set_target(&f, 250);
    ticks = 0;
    while (dim_fade_tick(&f, &v)) ticks++;
    CHECK(ticks == 5 && v == 250, "small step: %d ticks at %d", ticks, v);

    dim_fade_init(&f, 200);
    dim_fade_set_target(&f, 20);
    for (int i = 0; i < 5; i++) dim_fade_tick(&f, &v);
    int mid = v;
    dim_fade_set_target(&f, 240);
    CHECK(dim_fade_tick(&f, &v) && v > mid && v - mid <= (240 - mid + DIM_FADE_STEPS - 1) / DIM_FADE_STEPS,
          "retarget from %d to %d", mid, v);
    while (dim_fade_tick(&f, &v)) {}
    CHECK(v == 240, "retarget reaches 240: %d", v);

    dim_fade_init(&f, 100);
    dim_fade_set_target(&f, 140);
    bool set;
    CHECK(dim_fade_step(&f, 5000, &v, &set) == DIM_STEP_MS && set && v == 102, "step: the first at once: %d", v);
    CHECK(dim_fade_step(&f, 5030, &v, &set) == DIM_STEP_MS - 30 && !set && f.cur == 102, "a turn 30 ms later: no step");
    CHECK(dim_fade_step(&f, 5060, &v, &set) == DIM_STEP_MS - 60 && !set, "nor at 60 ms");
    CHECK(dim_fade_step(&f, 5100, &v, &set) == DIM_STEP_MS && set && v == 104, "the next at DIM_STEP_MS: %d", v);
    int steps = 2;
    for (int t = 5100; dim_fade_step(&f, t, &v, &set) >= 0 && t < 20000; t += 10) steps += set;
    CHECK(f.cur == 140 && steps == 20, "10 ms turns still take the fade's 20 steps: %d", steps);
    CHECK(dim_fade_step(&f, 30000, &v, &set) == -1 && !set, "settled: -1");
    dim_fade_init(&f, 100);
    dim_fade_set_target(&f, 102);
    dim_fade_step(&f, 1000, &v, &set);
    dim_fade_step(&f, 1100, &v, &set);
    CHECK(f.cur == 102 && dim_fade_step(&f, 1110, &v, &set) == -1, "settled 10 ms after its last step");
    dim_fade_set_target(&f, 140);
    CHECK(dim_fade_step(&f, 1120, &v, &set) == DIM_STEP_MS && set && v == 104, "a new target steps at once: %d", f.cur);
    CHECK(dim_fade_step(&f, 1219, &v, &set) == DIM_STEP_MS && set && v == 106, "a turn 1 ms short of the step (tick rounding) steps");
    CHECK(dim_fade_step(&f, 1317, &v, &set) == 2 && !set, "2 ms short does not");
    dim_fade_init(&f, 100);
    dim_fade_set_target(&f, 140);
    CHECK(dim_fade_tick(&f, &v) && v == 102, "first step: %d", v);
    CHECK(dim_fade_tick(&f, &v) && v == 104, "then continues: %d", v);
    while (dim_fade_tick(&f, &v)) {}
    CHECK(v == 140, "fade ends at 140: %d", v);
    CHECK(!dim_fade_tick(&f, &v), "then settled");
    dim_fade_set_target(&f, 100);
    CHECK(dim_fade_tick(&f, &v) && v == 138, "retarget continues from there: %d", v);
}

static void test_quiet_dim(void)
{
    CHECK(dim_quiet_level(153, 10, false, 20) == 153, "not quiet: level");
    CHECK(dim_quiet_level(5, 10, false, 20) == 10, "not quiet: floor raises");
    CHECK(dim_quiet_level(153, 10, true, 20) == 20, "quiet caps at dim_level");
    CHECK(dim_quiet_level(15, 10, true, 20) == 15, "quiet: darker level kept");
    CHECK(dim_quiet_level(5, 10, true, 20) == 10, "quiet: floor still raises below dim_level");
    CHECK(dim_quiet_level(153, 30, true, 20) == 20, "quiet: floor does not raise above dim_level");
    CHECK(dim_quiet_level(153, 10, true, 1) == 1, "quiet: dim_level 1");
    CHECK(dim_quiet_level(20, 10, true, 20) == 20, "night level 20 at default dim_level: unchanged");
    CHECK(dim_quiet_level(20, 10, true, 5) == 5, "night level 20, dim_level 5: darker");
    CHECK(dim_quiet_level(8, 10, true, 20) == 10, "night level 8 under floor 10: floor, under dim_level");
    CHECK(dim_quiet_level(300, 10, false, 20) == 255 && dim_quiet_level(300, 10, true, 255) == 255, "clamped to 255");
    CHECK(dim_quiet_floor(10, false, 5) == 10 && dim_quiet_floor(10, true, 20) == 10 && dim_quiet_floor(30, true, 20) == 20,
          "fade floor");

    dim_fade_t f;
    uint8_t v = 0;
    int floor = 30, dim = 5;
    dim_fade_init(&f, 153);
    dim_fade_set_floor(&f, dim_quiet_floor(floor, false, dim));
    dim_fade_set_target(&f, dim_quiet_level(153, floor, false, dim));
    CHECK(!dim_fade_tick(&f, &v), "not quiet: settled at 153");
    dim_fade_set_floor(&f, dim_quiet_floor(floor, true, dim));
    dim_fade_set_target(&f, dim_quiet_level(153, floor, true, dim));
    while (dim_fade_tick(&f, &v)) {}
    CHECK(f.cur == 5, "quiet on: fades to dim_level below the floor: %d", f.cur);
    dim_fade_set_floor(&f, dim_quiet_floor(floor, false, dim));
    dim_fade_set_target(&f, dim_quiet_level(153, floor, false, dim));
    while (dim_fade_tick(&f, &v)) {}
    CHECK(f.cur == 153, "quiet off: back to 153: %d", f.cur);
    dim_fade_set_floor(&f, dim_quiet_floor(floor, false, dim));
    dim_fade_set_target(&f, dim_quiet_level(2, floor, false, dim));
    while (dim_fade_tick(&f, &v)) {}
    CHECK(f.cur == 30, "after quiet the floor holds again: %d", f.cur);
}

static void test_idle(void)
{
    const dim_idle_rules_t none = {0, 20, 0}, dim = {600, 40, 0}, both = {600, 40, 1800}, off_only = {0, 20, 60};
    dim_idle_t s;
    dim_idle_init(&s, 1000);
    CHECK(dim_idle_state(&none, &s, 1000 + 90000000LL) == DIM_IDLE_AWAKE, "defaults (0, 20, 0): never idle, however long");
    CHECK(dim_idle_level(153, DIM_IDLE_AWAKE, &none) == 153 && dim_idle_floor(10, DIM_IDLE_AWAKE, &none) == 10,
          "defaults leave the level and the floor alone");

    CHECK(dim_idle_state(&dim, &s, 1000 + 599999) == DIM_IDLE_AWAKE, "1 ms before dim_after_s: awake");
    CHECK(dim_idle_state(&dim, &s, 1000 + 600000) == DIM_IDLE_DIMMED, "at dim_after_s: dimmed");
    CHECK(dim_idle_state(&dim, &s, 1000 + 86400000LL) == DIM_IDLE_DIMMED, "no off_after_s: dimmed for good");
    CHECK(dim_idle_state(&both, &s, 1000 + 1799999) == DIM_IDLE_DIMMED, "1 ms before off_after_s: dimmed");
    CHECK(dim_idle_state(&both, &s, 1000 + 1800000) == DIM_IDLE_OFF, "at off_after_s: off");
    CHECK(dim_idle_state(&off_only, &s, 1000 + 59999) == DIM_IDLE_AWAKE, "off only: awake until off_after_s");
    CHECK(dim_idle_state(&off_only, &s, 1000 + 60000) == DIM_IDLE_OFF, "off only: straight to off, no dim step");
    const dim_idle_rules_t inverted = {600, 40, 300};
    CHECK(dim_idle_state(&inverted, &s, 1000 + 300000) == DIM_IDLE_OFF, "off_after_s below dim_after_s: off wins");
    CHECK(dim_idle_state(&both, &s, 500) == DIM_IDLE_AWAKE, "a clock that went back: awake");

    CHECK(dim_idle_level(153, DIM_IDLE_DIMMED, &dim) == 40, "dimmed caps at dim_level");
    CHECK(dim_idle_level(25, DIM_IDLE_DIMMED, &dim) == 25, "a darker level is kept");
    CHECK(dim_idle_level(153, DIM_IDLE_OFF, &both) == 0 && dim_idle_level(0, DIM_IDLE_OFF, &both) == 0, "off is 0");
    CHECK(dim_idle_level(153, DIM_IDLE_AWAKE, &both) == 153, "awake: the level");
    CHECK(dim_idle_floor(10, DIM_IDLE_DIMMED, &dim) == 10 && dim_idle_floor(60, DIM_IDLE_DIMMED, &dim) == 40,
          "the idle cap lowers the floor below dim_level, not above");
    CHECK(dim_idle_floor(60, DIM_IDLE_AWAKE, &dim) == 60, "awake: the floor stays");

    CHECK(dim_idle_level(dim_quiet_level(153, 10, true, 20), DIM_IDLE_DIMMED, &dim) == 20, "quiet cap 20 under idle cap 40: 20");
    CHECK(dim_idle_level(dim_quiet_level(153, 10, true, 80), DIM_IDLE_DIMMED, &dim) == 40, "quiet cap 80 over idle cap 40: 40");
    CHECK(dim_idle_level(dim_quiet_level(153, 10, true, 80), DIM_IDLE_AWAKE, &dim) == 80, "quiet alone while awake: 80");
    CHECK(dim_idle_level(dim_quiet_level(153, 10, true, 20), DIM_IDLE_OFF, &both) == 0, "off beats quiet");
    CHECK(dim_idle_level(dim_quiet_level(5, 10, false, 20), DIM_IDLE_DIMMED, &dim) == 10, "base under the floor: the floor, under the idle cap");
    const dim_idle_rules_t under_floor = {600, 4, 0};
    CHECK(dim_idle_level(dim_quiet_level(153, 10, false, 20), DIM_IDLE_DIMMED, &under_floor) == 4 &&
              dim_idle_floor(10, DIM_IDLE_DIMMED, &under_floor) == 4,
          "idle dim_level below the floor holds, like quiet");

    dim_idle_init(&s, 0);
    CHECK(!dim_idle_input(&s, &both, 1000000), "input while dimmed is delivered");
    CHECK(dim_idle_state(&both, &s, 1000000 + 599999) == DIM_IDLE_AWAKE && dim_idle_state(&both, &s, 1000000 + 600000) == DIM_IDLE_DIMMED,
          "an input restarts the clock");
    CHECK(dim_idle_input(&s, &both, 1000000 + 1800000), "the first input after off is swallowed");
    CHECK(dim_idle_state(&both, &s, 1000000 + 1800000) == DIM_IDLE_AWAKE, "and it wakes the panel");
    CHECK(!dim_idle_input(&s, &both, 1000000 + 1800050), "the next input is delivered");
    CHECK(!dim_idle_input(&s, &both, 1000000 + 1800100), "and the one after");
    CHECK(dim_idle_input(&s, &off_only, 1000000 + 1800100 + 60000), "swallowed again after the next off");
    CHECK(!dim_idle_input(&s, &none, 1000000 + 90000000LL), "no rules: input is never swallowed");

    dim_idle_init(&s, 0);
    dim_idle_wake(&s, 1800000);
    CHECK(dim_idle_state(&both, &s, 1800000) == DIM_IDLE_AWAKE, "a wake source wakes an off panel (mood, OTA, demo)");
    CHECK(!dim_idle_input(&s, &both, 1800010), "and swallows nothing: the next input is delivered");
    CHECK(dim_idle_state(&both, &s, 1800010 + 1799999) == DIM_IDLE_DIMMED && dim_idle_state(&both, &s, 1800010 + 1800000) == DIM_IDLE_OFF,
          "the input after the wake restarts the clock: off 1800 s after it");
    dim_idle_wake(&s, 1799000);
    CHECK(s.last_ms == 1800010, "an older wake does not move the clock back");

    dim_fade_t f;
    uint8_t v = 0;
    bool set;
    dim_fade_init(&f, 153);
    CHECK(dim_fade_set_off(&f, true), "off starts a fade");
    CHECK(dim_fade_tick(&f, &v) && v == 0 && f.cur == 0, "off reaches 0 in one step, below the floor: %d", v);
    CHECK(!dim_fade_tick(&f, &v), "then settled");
    dim_fade_set_target(&f, 100);
    CHECK(!dim_fade_tick(&f, &v) && f.cur == 0 && f.target == 100, "a new level while off is kept, not shown");
    CHECK(dim_fade_set_off(&f, false), "waking starts a fade");
    CHECK(dim_fade_tick(&f, &v) && v == 100, "wake jumps to the kept level in one step: %d", v);
    CHECK(!dim_fade_tick(&f, &v), "settled at 100");
    dim_fade_set_off(&f, true);
    CHECK(dim_fade_step(&f, 5000, &v, &set) == DIM_STEP_MS && set && v == 0, "off through dim_fade_step");
    CHECK(!dim_fade_set_off(&f, true), "off again: nothing to do");
    dim_fade_set_off(&f, false);
    CHECK(dim_fade_step(&f, 6000, &v, &set) == DIM_STEP_MS && set && v == 100, "wake through dim_fade_step");
    dim_fade_set_target(&f, 20);
    CHECK(f.target == 20 && dim_fade_tick(&f, &v) && v < 100, "afterwards the normal fade");
    CHECK(dim_idle_light(DIM_IDLE_OFF, 153, 0) == 153 && dim_idle_light(DIM_IDLE_DIMMED, 153, 40) == 153, "an input after off or dim lights the panel at the awake level");
    CHECK(dim_idle_light(DIM_IDLE_AWAKE, 153, 0) == -1, "awake: nothing to write");
    CHECK(dim_idle_light(DIM_IDLE_OFF, -1, -1) == -1 && dim_idle_light(DIM_IDLE_OFF, 0, -1) == -1, "no known level: nothing to write");
    CHECK(dim_idle_light(DIM_IDLE_OFF, 300, -1) == 255, "clamped");
    CHECK(dim_idle_light(DIM_IDLE_DIMMED, 153, 200) == -1 && dim_idle_light(DIM_IDLE_DIMMED, 153, 153) == -1, "a wake during a fade down leaves the panel to the owner's fade instead of jumping to the end level");
    CHECK(dim_idle_light(DIM_IDLE_DIMMED, 153, 152) == 153 && dim_idle_light(DIM_IDLE_DIMMED, 153, -1) == 153, "a panel below the awake level, or an unknown one, is lit");
    CHECK(dim_idle_lit_take(153, DIM_IDLE_AWAKE) == 153 && dim_idle_lit_take(-1, DIM_IDLE_AWAKE) == -1, "a fresh mailbox level is taken");
    CHECK(dim_idle_lit_take(153, DIM_IDLE_DIMMED) == -1 && dim_idle_lit_take(153, DIM_IDLE_OFF) == -1, "a level posted before the knob went idle again is dropped, not replayed");

    dim_fade_init(&f, 153);
    dim_fade_set_off(&f, true);
    dim_fade_tick(&f, &v);
    dim_fade_sync(&f, 153);
    dim_fade_set_off(&f, false);
    CHECK(f.cur == 153 && !dim_fade_tick(&f, &v), "a panel lit by another task: the fade follows and has nothing to write");
    dim_fade_init(&f, 153);
    dim_fade_set_target(&f, 40);
    dim_fade_sync(&f, 153);
    CHECK(dim_fade_tick(&f, &v) && v < 153 && v > 40, "sync keeps the goal: the fade goes on from the written level: %d", v);
    dim_fade_set_target(&f, 153);
    while (dim_fade_tick(&f, &v)) {}
    CHECK(f.cur == 153, "and reaches the new goal");
}

static void test_lead(void)
{
    char out[EMBER_HOST_MAX + 1];
    ember_host_lead_label("M4", NULL, 0, out, sizeof out);
    CHECK(strcmp(out, "M4") == 0, "no lead: source (%s)", out);
    ember_host_lead_label("m4", "", 1, out, sizeof out);
    CHECK(strcmp(out, "M4") == 0, "empty lead: source, one host (%s)", out);
    ember_host_lead_label("", "m4", 2, out, sizeof out);
    CHECK(strcmp(out, "M4 +1") == 0, "two hosts (%s)", out);
    ember_host_lead_label("", "macbook-pro", 3, out, sizeof out);
    CHECK(strcmp(out, "MACBOOK +2") == 0, "cut to fit (%s)", out);
    CHECK(strlen(out) <= EMBER_HOST_MAX, "fits (%zu)", strlen(out));
    ember_host_lead_label("", "abcdefghijkl", 12, out, sizeof out);
    CHECK(strcmp(out, "ABCDEF +11") == 0, "two-digit N (%s)", out);
    ember_host_lead_label("", "", 0, out, sizeof out);
    CHECK(out[0] == 0, "none");
    ember_host_lead_label(NULL, NULL, 2, out, sizeof out);
    CHECK(out[0] == 0, "no name, no bare +N (%s)", out);
    char small[4];
    ember_host_lead_label("", "m4", 2, small, sizeof small);
    CHECK(strlen(small) == 3, "cap respected (%s)", small);

    ember_host_info_t was, now;
    ember_host_lead_info("", "studio", 0, "", 1, 0, &was);
    now = was;
    CHECK(was.lead && was.count == 1 && !ember_host_lead_new(&was, &now), "same lead and count: not new");
    ember_host_lead_info("", "studio", 2, "", 1, 0, &now);
    CHECK(strcmp(now.text, "STUDIO +1") == 0 && now.count == 1 && now.lead == was.lead, "one more host (%s)", now.text);
    CHECK(!ember_host_lead_new(&was, &now), "one more host, same sessions: not new");
    ember_host_lead_info("", "studio", 0, "", 2, 0, &now);
    CHECK(now.count == 2 && ember_host_lead_new(&was, &now), "no project, one more session on the same host: new");
    CHECK(!ember_host_lead_new(&now, &was), "one session fewer: not new");
    now = was;
    now.tool = 2;
    now.color = 0x123456;
    CHECK(!ember_host_lead_new(&was, &now), "tool or colour alone: not new");
    ember_host_lead_info("", "m4", 0, "", 1, 0, &now);
    CHECK(ember_host_lead_new(&was, &now), "another lead: new");
    ember_host_lead_info("", "macbook-pro-1", 0, "", 1, 0, &was);
    ember_host_lead_info("", "macbook-pro-2", 0, "", 1, 0, &now);
    CHECK(strcmp(was.text, now.text) == 0 && ember_host_lead_new(&was, &now), "hosts alike in the label: another lead (%s)",
          now.text);
    ember_host_info_t none = {0};
    CHECK(ember_host_lead_new(&none, &was), "a first lead: new");
    ember_host_lead_info("", "", 0, NULL, 0, 0, &now);
    CHECK(now.lead == 0 && now.text[0] == 0 && !ember_host_lead_new(&none, &now), "no lead: nobody, not new");

    CHECK(ember_host_color("#B48CFF") == 0xB48CFF, "upper");
    CHECK(ember_host_color("#b48cff") == 0xB48CFF, "lower");
    CHECK(ember_host_color("#000000") == 0, "black is a colour");
    CHECK(ember_host_color("B48CFF") == -1, "no #");
    CHECK(ember_host_color("#B48CF") == -1, "short");
    CHECK(ember_host_color("#B48CFG") == -1, "not hex");
    CHECK(ember_host_color(NULL) == -1 && ember_host_color("") == -1, "empty");

    CHECK(ember_host_tool("claude") == EMBER_TOOL_CLAUDE, "claude");
    CHECK(ember_host_tool("codex") == EMBER_TOOL_CODEX, "codex");
    CHECK(ember_host_tool("t3") == EMBER_TOOL_T3, "t3");
    CHECK(ember_host_tool("Claude") == EMBER_TOOL_NONE && ember_host_tool(NULL) == EMBER_TOOL_NONE, "other");
}

static void test_project(void)
{
    char out[EMBER_LABEL_MAX + 1];
    ember_host_project_label("ember", 0, out, sizeof out);
    CHECK(strcmp(out, "ember") == 0, "short, case kept (%s)", out);
    ember_host_project_label("learn-langu", 0, out, sizeof out);
    CHECK(strcmp(out, "learn-lan...") == 0, "11 chars: 9 and the ellipsis (%s)", out);
    ember_host_project_label("learn-lang", 0, out, sizeof out);
    CHECK(strcmp(out, "learn-lang") == 0, "exactly 10: whole (%s)", out);
    ember_host_project_label("learn-language", 1, out, sizeof out);
    CHECK(strcmp(out, "learn-la... +1") == 0, "+N takes slots from the name (%s)", out);
    ember_host_project_label("learn-lan", 1, out, sizeof out);
    CHECK(strcmp(out, "learn-lan +1") == 0, "9 and +1 fit whole (%s)", out);
    ember_host_project_label("learn-language", 12, out, sizeof out);
    CHECK(strcmp(out, "learn-l... +12") == 0, "two-digit N (%s)", out);
    ember_host_project_label("learn-language", 250, out, sizeof out);
    CHECK(strcmp(out, "learn-l... +99") == 0, "N capped at 99 (%s)", out);
    CHECK(strlen(out) == EMBER_LABEL_MAX, "the longest text fills the buffer (%zu)", strlen(out));
    ember_host_project_label("ember", -1, out, sizeof out);
    CHECK(strcmp(out, "ember") == 0, "no others (%s)", out);
    ember_host_project_label("abcdefgh xyz", 0, out, sizeof out);
    CHECK(strcmp(out, "abcdefgh...") == 0, "no space before the ellipsis (%s)", out);
    ember_host_project_label("Add search bar", 0, out, sizeof out);
    CHECK(strcmp(out, "Add searc...") == 0, "a space at the cut keeps the ellipsis (%s)", out);
    ember_host_project_label("my-project x", 0, out, sizeof out);
    CHECK(strcmp(out, "my-projec...") == 0, "a space then more at the cut (%s)", out);
    ember_host_project_label("abcdefghi  jk", 1, out, sizeof out);
    CHECK(strcmp(out, "abcdefgh... +1") == 0, "spaces at the cut with +N (%s)", out);
    ember_host_project_label("a          b", 0, out, sizeof out);
    CHECK(strcmp(out, "a...") == 0, "a run of spaces before the rest (%s)", out);
    ember_host_project_label("abcdefghij   ", 0, out, sizeof out);
    CHECK(strcmp(out, "abcdefghij") == 0, "only spaces after 10: whole (%s)", out);
    ember_host_project_label("  ember  ", 0, out, sizeof out);
    CHECK(strcmp(out, "ember") == 0, "trimmed (%s)", out);
    ember_host_project_label("\xd0\xbf\xd1\x80\xd0\xbe-x\xe2\x80\xa6", 0, out, sizeof out);
    CHECK(strcmp(out, "-x") == 0, "UTF-8 sequences dropped whole, no stray byte (%s)", out);
    ember_host_project_label("ab\xd0", 0, out, sizeof out);
    CHECK(strcmp(out, "ab") == 0, "a cut sequence leaves nothing (%s)", out);
    ember_host_project_label("abcdefghi\xd0\xbf\xd0\xbfjk", 0, out, sizeof out);
    CHECK(strcmp(out, "abcdefghi...") == 0, "counted in shown characters, not bytes (%s)", out);
    ember_host_project_label("\xf0\x9f\x99\x82", 3, out, sizeof out);
    CHECK(out[0] == 0, "nothing printable: none, no bare +N (%s)", out);
    ember_host_project_label("? ??? -", 1, out, sizeof out);
    CHECK(out[0] == 0, "no letter or digit: none (%s)", out);
    ember_host_project_label("??????????a", 0, out, sizeof out);
    CHECK(out[0] == 0, "a letter past the 10 slots: none (%s)", out);
    ember_host_project_label("?????????a", 0, out, sizeof out);
    CHECK(strcmp(out, "?????????a") == 0, "a letter in the 10th slot (%s)", out);
    ember_host_project_label("?????????a", 1, out, sizeof out);
    CHECK(out[0] == 0, "a letter cut by the dots: none (%s)", out);
    ember_host_project_label("????????a?b", 0, out, sizeof out);
    CHECK(strcmp(out, "????????a...") == 0, "a letter before the dots (%s)", out);
    ember_host_project_label("?7", 0, out, sizeof out);
    CHECK(strcmp(out, "?7") == 0, "one digit is enough (%s)", out);
    ember_host_project_label(NULL, 1, out, sizeof out);
    CHECK(out[0] == 0, "NULL");
    char small[6];
    ember_host_project_label("learn-language", 1, small, sizeof small);
    CHECK(strcmp(small, "learn") == 0, "cap respected (%s)", small);

    ember_host_info_t a, b;
    ember_host_lead_info("", "m4", 0, "ember", 1, 0, &a);
    CHECK(strcmp(a.text, "ember") == 0 && a.sub[0] == 0 && a.count == 1, "one session, one host: no +N, no host line (%s|%s)",
          a.text, a.sub);
    ember_host_lead_info("", "m4", 0, "ember", 3, 0, &a);
    CHECK(strcmp(a.text, "ember +2") == 0 && a.sub[0] == 0 && a.count == 3, "+N counts the other sessions, not hosts (%s)",
          a.text);
    ember_host_lead_info("", "macbook-pro", 2, "learn-language", 3, 0, &a);
    CHECK(strcmp(a.text, "learn-la... +2") == 0 && strcmp(a.sub, "MACBOOK-PR") == 0, "two hosts: host line (%s|%s)", a.text,
          a.sub);
    ember_host_lead_info("m4", "", 2, "ember", 2, 0, &a);
    CHECK(strcmp(a.sub, "M4") == 0, "host line from source without a lead (%s)", a.sub);
    ember_host_lead_info("", "m4", 3, "", 4, 0, &a);
    CHECK(strcmp(a.text, "M4 +2") == 0 && a.sub[0] == 0 && a.count == 4, "no project: today's label, sessions counted (%s)",
          a.text);
    ember_host_lead_info("", "m4", 3, "??", 4, 0, &a);
    CHECK(strcmp(a.text, "M4 +2") == 0 && a.count == 4, "folded to only '?': today's label (%s)", a.text);

    ember_host_lead_info("", "m4", 0, "ember", 1, 0, &a);
    ember_host_lead_info("", "m4", 2, "ember", 2, 0, &b);
    CHECK(a.lead == b.lead && ember_host_lead_new(&a, &b), "one more waiting session: new");
    CHECK(!ember_host_lead_new(&b, &a), "one session fewer: not new");
    ember_host_lead_info("", "m5", 2, "ember", 2, 0, &a);
    CHECK(!ember_host_lead_new(&b, &b) && ember_host_lead_new(&b, &a), "same project on another host: new");
    ember_host_lead_info("", "m4", 2, "cinder", 2, 0, &a);
    CHECK(ember_host_lead_new(&b, &a), "another project: new");
    ember_host_lead_info("", "m4", 5, "ember", 2, 0, &a);
    CHECK(!ember_host_lead_new(&b, &a), "more hosts, same sessions: not new");
    ember_host_lead_info("", "m4", 2, "cinder", 1, 0, &a);
    CHECK(!ember_host_lead_new(&b, &a), "newest answered, an older one leads with fewer sessions: not new");
    ember_host_lead_info("", "m4", 2, "", 2, 0, &a);
    CHECK(ember_host_lead_new(&b, &a) && a.lead != b.lead, "project gone at the same count: another lead");
    ember_host_lead_info("", "macbook-pro-a", 2, "ember", 2, 0, &a);
    ember_host_lead_info("", "macbook-pro-b", 2, "ember", 2, 0, &b);
    CHECK(strcmp(a.sub, b.sub) == 0 && ember_host_lead_new(&a, &b), "same project, hosts alike in the label: new");
}

static void test_done_lead(void)
{
    struct {
        const char *source, *lead, *project;
        int hosts, waiting, done;
        const char *text, *sub;
    } c[] = {
        {"", "m4", "ember", 1, 0, 1, "ember", ""},
        {"", "m4", "ember", 1, 0, 3, "ember +2", ""},
        {"", "macbook-pro", "learn-language", 2, 0, 3, "learn-la... +2", "MACBOOK-PR"},
        {"m5", "", "ember", 2, 0, 1, "ember", "M5"},
        {"", "m4", "ember", 1, 2, 5, "ember +1", ""},
        {"", "m4", "ember", 1, 0, 0, "ember", ""},
        {"", "m4", "", 2, 0, 3, "M4 +1", ""},
        {"", "m4", "??", 3, 0, 4, "M4 +2", ""},
    };
    for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
        ember_host_info_t h;
        ember_host_lead_info(c[k].source, c[k].lead, c[k].hosts, c[k].project, c[k].waiting, c[k].done, &h);
        CHECK(strcmp(h.text, c[k].text) == 0 && strcmp(h.sub, c[k].sub) == 0 && h.count == c[k].waiting,
              "done %zu: '%s' over '%s', count %d (want '%s' over '%s', %d)", k, h.text, h.sub, h.count, c[k].text, c[k].sub,
              c[k].waiting);
    }

    struct {
        const char *lead_a, *proj_a;
        int wait_a, done_a;
        const char *lead_b, *proj_b;
        int wait_b, done_b;
        bool fresh;
        const char *why;
    } t[] = {
        {"m4", "ember", 0, 1, "m5", "ember", 0, 1, false, "another done lead"},
        {"m4", "ember", 0, 1, "m4", "cinder", 0, 1, false, "another done project"},
        {"m4", "ember", 0, 1, "m4", "ember", 0, 3, false, "more done sessions"},
        {"m4", "ember", 0, 3, "m5", "cinder", 0, 2, false, "fewer done sessions, another lead"},
        {"m4", "ember", 0, 3, "m4", "ember", 1, 3, true, "done to waiting"},
        {"m4", "ember", 0, 1, "m5", "cinder", 1, 0, true, "done to waiting on another host"},
        {"m4", "ember", 0, 3, "m4", "cinder", 1, 0, true, "done to waiting, fewer sessions than were done"},
        {"m4", "ember", 1, 0, "m5", "cinder", 0, 1, false, "waiting to done"},
        {"m4", "ember", 1, 2, "m5", "cinder", 1, 2, true, "another waiting lead with done sessions around"},
        {"m4", "ember", 1, 0, "m4", "ember", 1, 4, false, "done sessions added while waiting"},
        {"", NULL, 0, 0, "m4", "ember", 0, 1, false, "a first done lead"},
    };
    for (size_t k = 0; k < sizeof t / sizeof t[0]; k++) {
        ember_host_info_t a, b;
        ember_host_lead_info("", t[k].lead_a, 1, t[k].proj_a, t[k].wait_a, t[k].done_a, &a);
        ember_host_lead_info("", t[k].lead_b, 1, t[k].proj_b, t[k].wait_b, t[k].done_b, &b);
        CHECK(ember_host_lead_new(&a, &b) == t[k].fresh, "latch %zu, %s: %s", k, t[k].why, t[k].fresh ? "new" : "not new");
    }
}

int main(void)
{
    test_lead();
    test_project();
    test_done_lead();
    test_priority();
    test_time();
    test_pick();
    test_label();
    test_dim();
    test_quiet_dim();
    test_idle();
    if (failures) {
        printf("ember: %d failure(s)\n", failures);
        return 1;
    }
    printf("ember: all tests passed\n");
    return 0;
}
