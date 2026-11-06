#include <math.h>
#include <stdio.h>
#include <string.h>

#include "room_moment.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static rm_trigger_t tr;
static rm_in_t in;
static double T;

static void reset(void)
{
    rm_init(&tr);
    T = 10000;
    in = (rm_in_t){.t = T, .valid = true, .aqi = 1, .mood = WXM_REST, .bot_mood = 0, .shown = true, .on = true, .last_at = -INFINITY};
}

static bool tick(rm_out_t *out)
{
    in.t = T;
    return rm_tick(&tr, &in, out);
}

static double run(double s, rm_out_t *out, bool show)
{
    for (double end = T + s; T < end; T += 0.5) {
        if (tick(out)) {
            if (show) {
                rm_shown(&tr, out, T);
                in.last_at = T;
            }
            return T;
        }
    }
    return -1;
}

static void settle(void)
{
    rm_out_t out;
    for (double end = T + WXM_SETTLE_S + 1; T < end; T += 0.5) tick(&out);
}

static void test_stale(void)
{
    rm_out_t out;
    reset();
    settle();
    CHECK(run(600, &out, false) < 0, "good air: nothing");
    in.aqi = 3;
    CHECK(run(600, &out, false) < 0, "moderate air: nothing");
    in.aqi = 4;
    double t0 = T, at = run(10, &out, false);
    CHECK(at >= 0 && at - t0 < 1 && out.kind == WXM_ROOM_STALE && out.aqi == 4 && !out.quiet, "AQI 4: stale air at once (%.1f)", at - t0);
    CHECK(tick(&out) && out.kind == WXM_ROOM_STALE, "offered again until rm_shown records it");
    rm_shown(&tr, &out, T);
    in.last_at = T;
    t0 = T;
    at = run(WXM_SAME_S + 60, &out, true);
    CHECK(at >= t0 + WXM_SAME_S && at < t0 + WXM_SAME_S + 1 && out.kind == WXM_ROOM_STALE, "it repeats every 30 min while it lasts (%.1f)", at - t0);

    reset();
    in.aqi = 5;
    settle();
    CHECK(run(10, &out, false) >= 0 && out.kind == WXM_ROOM_STALE && out.aqi == 5, "AQI 5: stale air");

    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.aqi = 3;
    t0 = T;
    CHECK(run(WXM_SAME_S + 60, &out, true) < 0, "hysteresis: at AQI 3 no repeat and no fresh air");
    in.aqi = 4;
    CHECK(run(10, &out, true) >= 0 && out.kind == WXM_ROOM_STALE, "back to 4 after the 30 min: stale again");
    t0 = T;
    in.aqi = 3;
    run(60, &out, false);
    in.aqi = 4;
    at = run(WXM_SAME_S + 60, &out, true);
    CHECK(at >= t0 + WXM_SAME_S, "a dip to 3 and back does not restart the 30 min (%.1f)", at - t0);
}

static void test_fresh(void)
{
    rm_out_t out;
    reset();
    in.aqi = 4;
    settle();
    double t0 = run(10, &out, true);
    CHECK(t0 >= 0 && out.kind == WXM_ROOM_STALE, "stale first");
    in.aqi = 2;
    double at = run(WXM_GAP_S + 60, &out, true);
    CHECK(at >= t0 + WXM_GAP_S && out.kind == WXM_ROOM_FRESH && out.aqi == 2, "AQI 2 after it: fresh air, after the 5 min gap (%.1f)", at - t0);
    CHECK(run(WXM_SAME_S * 3, &out, true) < 0, "once");

    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.aqi = 1;
    T += WXM_GAP_S;
    CHECK(run(10, &out, false) >= 0 && out.kind == WXM_ROOM_FRESH && out.aqi == 1, "AQI 1: fresh air too");

    reset();
    in.aqi = 4;
    settle();
    CHECK(run(10, &out, false) >= 0, "stale air due");
    in.aqi = 2;
    CHECK(run(WXM_SAME_S, &out, true) < 0, "a stale spell whose moment never played: no fresh air");

    reset();
    in.aqi = 1;
    settle();
    CHECK(run(WXM_SAME_S, &out, true) < 0, "good air from the start: no fresh air");

    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.aqi = 3;
    CHECK(run(WXM_GAP_S + 60, &out, true) < 0, "AQI 3 is not fresh yet");
    in.aqi = 2;
    CHECK(run(10, &out, true) >= 0 && out.kind == WXM_ROOM_FRESH, "2 is");

    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.aqi = 2;
    in.blocked = true;
    run(WXM_WAIT_S + 10, &out, false);
    in.blocked = false;
    CHECK(run(60, &out, false) < 0, "a fresh-air moment held back 30 min is dropped");

    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.aqi = 2;
    in.blocked = true;
    run(60, &out, false);
    in.aqi = 5;
    run(1, &out, false);
    in.aqi = 2;
    in.blocked = false;
    CHECK(run(WXM_GAP_S + 60, &out, false) < 0,
          "a queued fresh-air moment drops when the air turns stale again, and that spell played nothing, so none is owed");
}

static void test_hysteresis(void)
{
    rm_out_t out;
    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.aqi = 3;
    CHECK(run(40 * 60, &out, true) < 0, "40 min at AQI 3: no repeat, no fresh air");
    in.aqi = 2;
    CHECK(run(10, &out, true) >= 0 && out.kind == WXM_ROOM_FRESH, "then AQI 2 ends the spell: fresh air (3 kept it stale)");

    reset();
    in.aqi = 4;
    settle();
    double t0 = run(10, &out, true);
    in.aqi = 3;
    run(60, &out, true);
    in.aqi = 4;
    CHECK(run(WXM_SAME_S - 120, &out, true) < 0, "back to 4 inside the 30 min: nothing yet");
    in.aqi = 2;
    double at = run(10, &out, true);
    CHECK(at >= 0 && out.kind == WXM_ROOM_FRESH && at - t0 < WXM_SAME_S, "the spell went on through 3 and back: fresh air still owed (%.1f)", at - t0);

    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.aqi = 2;
    in.blocked = true;
    run(10, &out, false);
    in.blocked = false;
    in.aqi = 3;
    T += WXM_GAP_S;
    CHECK(run(60, &out, true) < 0, "fresh air queued, the AQI back at 3: not while it is not 2 or better");
    in.aqi = 2;
    CHECK(run(10, &out, true) >= 0 && out.kind == WXM_ROOM_FRESH, "at 2 again it plays (3 did not make it stale)");

    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.aqi = 2;
    in.blocked = true;
    run(10, &out, false);
    in.blocked = false;
    in.valid = false;
    in.aqi = 0;
    T += WXM_GAP_S;
    CHECK(run(60, &out, true) < 0, "fresh air queued, the reading gone (the glue's AQI 0): nothing");
    in.valid = true;
    in.aqi = 1;
    CHECK(run(10, &out, true) >= 0 && out.kind == WXM_ROOM_FRESH && out.aqi == 1, "a valid reading again: it plays");
}

static void test_warm_up(void)
{
    rm_out_t out;
    reset();
    in.aqi = 5;
    in.valid = false;
    settle();
    CHECK(run(WXM_SAME_S, &out, true) < 0, "warm-up (not valid): no moment");
    in.valid = true;
    CHECK(run(10, &out, true) >= 0 && out.kind == WXM_ROOM_STALE, "valid: stale air");
    in.valid = false;
    in.aqi = 1;
    CHECK(run(WXM_SAME_S * 2, &out, true) < 0, "an invalid reading neither plays nor ends the spell");
    in.valid = true;
    in.aqi = 2;
    CHECK(run(10, &out, true) >= 0 && out.kind == WXM_ROOM_FRESH, "a valid AQI 2 ends it");
    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.aqi = 2;
    in.valid = false;
    T += WXM_GAP_S;
    CHECK(run(60, &out, true) < 0, "an invalid good reading does not count as fresh");
}

static void test_moods(void)
{
    rm_out_t out;
    for (int m = 0; m < 3; m++) {
        reset();
        in.aqi = 4;
        in.mood = (wxm_mood_t)m;
        settle();
        bool due = run(10, &out, false) >= 0;
        CHECK(due == (m != WXM_BUSY), "mood %d: %s", m, due ? "due" : "none");
        if (due) CHECK(out.quiet == (m == WXM_WORKING), "mood %d: quiet %d", m, out.quiet);
    }
    reset();
    in.aqi = 4;
    in.shown = false;
    settle();
    CHECK(run(60, &out, false) < 0, "off the bot page or asleep: none");
    in.shown = true;
    double t0 = T, at = run(10, &out, false);
    CHECK(at >= t0 + WXM_SETTLE_S, "back on the page: after the 3 s settle (%.1f)", at - t0);
    reset();
    in.aqi = 4;
    settle();
    in.bot_mood = 3;
    t0 = T;
    at = run(10, &out, false);
    CHECK(at >= t0 + WXM_SETTLE_S, "the bot's own mood changing restarts the settle (%.1f)", at - t0);
    reset();
    in.aqi = 4;
    in.mood = WXM_BUSY;
    settle();
    run(60, &out, false);
    in.mood = WXM_REST;
    t0 = T;
    at = run(10, &out, false);
    CHECK(at >= t0 + WXM_SETTLE_S && out.kind == WXM_ROOM_STALE, "a stale moment that waited for a busy bot comes after it (%.1f)", at - t0);
}

static void test_gates(void)
{
    rm_out_t out;
    reset();
    in.aqi = 4;
    settle();
    in.blocked = true;
    CHECK(run(60, &out, false) < 0, "blocked: none");
    in.blocked = false;
    in.weather_first = true;
    CHECK(run(60, &out, false) < 0, "a weather change queued: it goes first");
    in.weather_first = false;
    CHECK(run(1, &out, false) >= 0, "then the room's");

    reset();
    in.aqi = 4;
    settle();
    in.last_at = T - 10;
    double t0 = T, at = run(WXM_GAP_S, &out, false);
    CHECK(at >= in.last_at + WXM_GAP_S, "5 min after any other moment (%.1f)", at - t0);

    reset();
    in.aqi = 4;
    settle();
    run(10, &out, true);
    in.on = false;
    run(1, &out, true);
    in.on = true;
    in.aqi = 2;
    T += WXM_GAP_S;
    CHECK(run(60, &out, true) < 0, "turning them off forgets the spell: no fresh air after");
    in.aqi = 4;
    CHECK(run(1, &out, true) >= 0 && out.kind == WXM_ROOM_STALE, "and the 30 min of the last stale moment");

    reset();
    in.aqi = 4;
    in.on = false;
    settle();
    CHECK(run(WXM_SAME_S, &out, true) < 0, "off: none");
}

int main(void)
{
    test_stale();
    test_fresh();
    test_hysteresis();
    test_warm_up();
    test_moods();
    test_gates();
    if (failures) {
        printf("room moment: %d failure(s)\n", failures);
        return 1;
    }
    printf("room moment: all tests passed\n");
    return 0;
}
