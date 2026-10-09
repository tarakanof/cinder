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

int main(void)
{
    test_lead();
    test_priority();
    test_time();
    test_pick();
    test_label();
    test_dim();
    test_quiet_dim();
    if (failures) {
        printf("ember: %d failure(s)\n", failures);
        return 1;
    }
    printf("ember: all tests passed\n");
    return 0;
}
