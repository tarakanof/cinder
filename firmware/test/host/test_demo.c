#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo.h"
#include "knob_view.h"
#include "view_policy.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *load(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    return b;
}

static demo_block_t block(const char *id, demo_page_t page, demo_act_kind_t act, bool view, bool hold)
{
    demo_block_t b = {.present = true, .page = page, .face = -1, .has_view = view, .hold = hold};
    b.act.kind = act;
    snprintf(b.id, sizeof b.id, "%s", id);
    return b;
}

static void test_act(void)
{
    demo_act_t a;
    CHECK(demo_act_parse("moment:storm:full", &a) && a.kind == DEMO_ACT_MOMENT && a.moment == 9 && !a.quiet, "storm");
    CHECK(demo_act_parse("moment:glance:quiet", &a) && a.kind == DEMO_ACT_MOMENT && a.moment == 1 && a.quiet, "glance");
    static const char *const K[] = {"glance", "night", "frost", "heat", "wind", "afterrain", "snow", "rain", "storm"};
    for (int i = 0; i < 9; i++) {
        char s[40];
        snprintf(s, sizeof s, "moment:%s:full", K[i]);
        CHECK(demo_act_parse(s, &a) && a.moment == i + 1, "%s", s);
    }
    CHECK(demo_act_parse("chase:full", &a) && a.kind == DEMO_ACT_CHASE && a.chase_style == 0, "chase full");
    CHECK(demo_act_parse("chase:half", &a) && a.kind == DEMO_ACT_CHASE && a.chase_style == 1, "chase half");
    CHECK(demo_act_parse("hop", &a) && a.kind == DEMO_ACT_HOP, "hop");
    CHECK(demo_act_parse("pop", &a) && a.kind == DEMO_ACT_POP, "pop");
    CHECK(demo_act_parse("blink", &a) && a.kind == DEMO_ACT_BLINK, "blink");
    CHECK(demo_act_parse("sleepy", &a) && a.kind == DEMO_ACT_SLEEPY, "sleepy");
    CHECK(demo_act_parse("asleep", &a) && a.kind == DEMO_ACT_ASLEEP, "asleep");
    static const char *const BAD[] = {"", "moment", "moment:", "moment:storm", "moment:storm:", "moment:storm:loud",
                                      "moment:hail:full", "moment::full", "chase", "chase:quarter", "Hop", "hop ",
                                      "moment:storm:full:x", "blinks", "Sleepy", "sleepy:full", "Asleep", "asleep:full", "asleep "};
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++)
        CHECK(!demo_act_parse(BAD[i], &a) && a.kind == DEMO_ACT_NONE, "bad act '%s'", BAD[i]);
    CHECK(!demo_act_parse(NULL, &a), "NULL act");
}

static void test_page(void)
{
    demo_page_t p;
    int face;
    CHECK(demo_page_parse("bot", &p, &face) && p == DEMO_PAGE_BOT && face == -1, "bot");
    CHECK(demo_page_parse("pomodoro", &p, &face) && p == DEMO_PAGE_POMODORO, "pomodoro");
    CHECK(demo_page_parse("nowplaying", &p, &face) && p == DEMO_PAGE_NOWPLAYING, "nowplaying");
    CHECK(demo_page_parse("weather", &p, &face) && p == DEMO_PAGE_WEATHER && face == -1, "weather");
    static const char *const F[] = {"temp", "rain", "cast", "wind", "days", "sun", "air"};
    for (int i = 0; i < 7; i++) {
        char s[24];
        snprintf(s, sizeof s, "weather:%s", F[i]);
        CHECK(demo_page_parse(s, &p, &face) && p == DEMO_PAGE_WEATHER && face == i, "%s", s);
    }
    static const char *const BAD[] = {"", "weather:", "weather:moon", "bot:temp", "pomo", "Weather", "weather:temp:x"};
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) CHECK(!demo_page_parse(BAD[i], &p, &face), "bad page '%s'", BAD[i]);
    for (int i = 0; i < DEMO_PAGES; i++) {
        CHECK(demo_page_parse(demo_page_id((demo_page_t)i), &p, &face) && p == (demo_page_t)i, "page id %d", i);
    }
}

static void test_block(void)
{
    demo_block_t b;
    cJSON *j = cJSON_Parse("{\"id\":\"a\",\"page\":\"weather:sun\",\"act\":\"pop\",\"offline\":true,\"hold\":true,"
                           "\"view\":{}}");
    CHECK(demo_block_parse(j, &b) && b.present && strcmp(b.id, "a") == 0 && b.page == DEMO_PAGE_WEATHER && b.face == 5 &&
              b.act.kind == DEMO_ACT_POP && b.offline && b.hold && b.has_view,
          "full block");
    cJSON_Delete(j);
    j = cJSON_Parse("{\"id\":\"b\",\"page\":\"bot\",\"act\":\"dance\",\"hold\":false}");
    CHECK(demo_block_parse(j, &b) && b.act.kind == DEMO_ACT_NONE && !b.has_view && !b.hold && !b.offline,
          "an unknown act plays the scene without it");
    cJSON_Delete(j);
    static const char *const BAD[] = {"null", "[]", "{\"page\":\"bot\"}", "{\"id\":\"\",\"page\":\"bot\"}",
                                      "{\"id\":7,\"page\":\"bot\"}", "{\"id\":\"x\"}", "{\"id\":\"x\",\"page\":\"moon\"}",
                                      "{\"id\":\"x\",\"page\":\"weather:moon\"}",
                                      ("{\"id\":\"01234567890123456789012345678901234567890123456789012345678901234\","
                                       "\"page\":\"bot\"}")};
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        j = cJSON_Parse(BAD[i]);
        CHECK(!demo_block_parse(j, &b) && !b.present, "bad block %s", BAD[i]);
        cJSON_Delete(j);
    }
    CHECK(!demo_block_parse(NULL, &b) && !b.present, "absent block");
    j = cJSON_Parse("{\"id\":\"0123456789012345678901234567890123456789012345678901234567890123\",\"page\":\"bot\"}");
    CHECK(demo_block_parse(j, &b) && strlen(b.id) == DEMO_ID_MAX, "a 64-byte id fits");
    cJSON_Delete(j);
}

static void test_view(void)
{
    knob_view_t v, dv;
    CHECK(knob_view_read_full("{\"v\":1,\"mood\":{\"running\":1},\"quiet\":true,\"demo\":{\"id\":\"x\",\"page\":\"bot\","
                              "\"hold\":false,\"view\":{\"v\":1,\"mood\":{\"done\":1,\"lead\":\"DEMO\"},"
                              "\"pomo\":{\"phase\":\"focus\",\"running\":true,\"remaining_sec\":60,\"planned_sec\":1500},"
                              "\"demo\":{\"id\":\"nested\",\"page\":\"bot\"}}}}",
                              &v, NULL, &dv) == KNOB_VIEW_OK,
          "view with a demo parses");
    CHECK(v.demo.present && strcmp(v.demo.id, "x") == 0 && v.demo.has_view, "demo block");
    CHECK(knob_view_mood(&v) == BOT_WORKING && v.quiet && !v.has_pomo, "the real view stays the real one");
    CHECK(knob_view_mood(&dv) == BOT_DONE && strcmp(dv.lead, "DEMO") == 0 && dv.has_pomo && dv.pomo.remaining_sec == 60,
          "the demo view");
    CHECK(!dv.quiet && !dv.demo.present, "no nested demo");
    CHECK(knob_view_read_full("{\"v\":1,\"demo\":{\"id\":\"x\",\"page\":\"bot\"}}", &v, NULL, &dv) == KNOB_VIEW_OK &&
              v.demo.present && !v.demo.has_view && !dv.has_mood,
          "a demo without a view leaves the demo view empty");
    CHECK(knob_view_read_full("{\"v\":1,\"demo\":{\"page\":\"bot\"}}", &v, NULL, &dv) == KNOB_VIEW_OK && !v.demo.present,
          "a bad block is absent, the view still parses");
    CHECK(knob_view_parse("{\"v\":1}", &v) && !v.demo.present && v.demo.face == -1, "no demo");

    knob_view_state_t s = {0};
    knob_step_t st = knob_view_step(&s, 200, "{\"v\":1,\"mood\":{\"running\":1},\"demo\":{\"id\":\"x\",\"page\":\"bot\","
                                             "\"view\":{\"mood\":{\"errors\":1}}}}");
    CHECK(st.act == KNOB_STEP_APPLY && st.mood == BOT_WORKING, "the step's mood is the real one");
    CHECK(s.view.demo.present && knob_view_mood(&s.demo_view) == BOT_ERROR, "the step keeps the demo view");
    st = knob_view_step(&s, 304, NULL);
    CHECK(st.act == KNOB_STEP_APPLY && s.view.demo.present && knob_view_mood(&s.demo_view) == BOT_ERROR,
          "a 304 keeps both");
    st = knob_view_step(&s, 200, "{\"v\":1,\"mood\":{\"running\":1}}");
    CHECK(!s.view.demo.present && !s.demo_view.has_mood, "a view without a demo clears it");
}

static void test_once(void)
{
    demo_t d;
    demo_init(&d);
    demo_block_t a = block("a", DEMO_PAGE_BOT, DEMO_ACT_NONE, true, false), out;
    char q[DEMO_QUERY_MAX];
    CHECK(demo_query(&d, q, sizeof q) && q[0] == 0, "nothing to report before a demo");
    CHECK(demo_offer(&d, NULL, 0) == DEMO_EV_NONE, "absent, idle");
    CHECK(demo_offer(&d, &a, 10) == DEMO_EV_START && demo_playing(&d) && demo_view_wanted(&d), "a starts");
    CHECK(!demo_take_start(&d, &out), "not before the view is applied");
    demo_ready(&d, 10);
    CHECK(demo_take_start(&d, &out) && strcmp(out.id, "a") == 0, "start taken");
    CHECK(!demo_take_start(&d, &out), "start taken once");
    CHECK(demo_query(&d, q, sizeof q) && strcmp(q, "demo_id=a&demo_state=playing") == 0, "playing: %s", q);
    CHECK(demo_offer(&d, &a, 11) == DEMO_EV_NONE && demo_playing(&d), "the same id again keeps playing");
    CHECK(!demo_tick(&d, 17.9) && demo_tick(&d, 18.0) && !demo_playing(&d), "a view scene plays 8 s");
    CHECK(demo_query(&d, q, sizeof q) && strcmp(q, "demo_id=a&demo_state=done") == 0, "done: %s", q);
    CHECK(demo_offer(&d, &a, 20) == DEMO_EV_NONE && !demo_playing(&d), "a done id does not replay");
    CHECK(demo_query(&d, q, sizeof q) && strcmp(q, "demo_id=a&demo_state=done") == 0, "done while sent");
    CHECK(demo_offer(&d, NULL, 21) == DEMO_EV_NONE, "absent after done");
    CHECK(demo_query(&d, q, sizeof q) && q[0] == 0, "the server stopped sending it: nothing reported");
    CHECK(demo_offer(&d, &a, 22) == DEMO_EV_NONE && !demo_playing(&d), "a plays at most once, even after a gap");
    CHECK(demo_query(&d, q, sizeof q) && strcmp(q, "demo_id=a&demo_state=done") == 0, "reported done again");

    demo_block_t b = block("b", DEMO_PAGE_POMODORO, DEMO_ACT_NONE, true, false);
    CHECK(demo_offer(&d, &b, 30) == DEMO_EV_START && demo_playing(&d), "b starts");
    unsigned gen = d.gen;
    demo_block_t c = block("c", DEMO_PAGE_BOT, DEMO_ACT_NONE, false, false);
    CHECK(demo_offer(&d, &c, 31) == DEMO_EV_START && d.gen != gen && strcmp(d.cur.id, "c") == 0,
          "a new id replaces a playing one");
    CHECK(!demo_view_wanted(&d), "c has no view");
    CHECK(demo_offer(&d, &b, 32) == DEMO_EV_STOP && !demo_playing(&d), "b again, played: c stops, b does not replay");
    CHECK(demo_offer(&d, &c, 33) == DEMO_EV_NONE && !demo_playing(&d), "c played too");

    for (int i = 0; i < DEMO_SEEN; i++) {
        char id[8];
        snprintf(id, sizeof id, "n%d", i);
        demo_block_t n = block(id, DEMO_PAGE_BOT, DEMO_ACT_NONE, false, false);
        CHECK(demo_offer(&d, &n, 40 + i) == DEMO_EV_START, "%s starts", id);
    }
    demo_block_t n1 = block("n1", DEMO_PAGE_BOT, DEMO_ACT_NONE, false, false);
    CHECK(strcmp(d.cur.id, "n7") == 0 && demo_offer(&d, &n1, 60) == DEMO_EV_STOP && !demo_playing(&d),
          "an earlier id in the ring is remembered: the playing one stops, n1 does not replay");
    CHECK(demo_offer(&d, &n1, 61) == DEMO_EV_NONE, "n1 again: still nothing");

    demo_init(&d);
    for (int i = 0; i <= DEMO_SEEN; i++) {
        char id[8];
        snprintf(id, sizeof id, "x%d", i);
        demo_block_t x = block(id, DEMO_PAGE_BOT, DEMO_ACT_NONE, false, false);
        CHECK(demo_offer(&d, &x, 70 + i) == DEMO_EV_START, "%s starts", id);
    }
    demo_block_t x0 = block("x0", DEMO_PAGE_BOT, DEMO_ACT_NONE, false, false);
    CHECK(demo_offer(&d, &x0, 90) == DEMO_EV_START, "the ring holds DEMO_SEEN ids: the oldest is evicted and plays again");
    demo_block_t x2 = block("x2", DEMO_PAGE_BOT, DEMO_ACT_NONE, false, false);
    CHECK(demo_offer(&d, &x2, 91) == DEMO_EV_STOP, "x2 is still in the ring");
}

static void test_timing(void)
{
    demo_t d;
    demo_init(&d);
    demo_block_t act = block("hop", DEMO_PAGE_BOT, DEMO_ACT_HOP, false, false);
    demo_offer(&d, &act, 100);
    demo_ready(&d, 100);
    CHECK(!demo_tick(&d, 100.5), "an act runs until done");
    CHECK(!demo_tick(&d, 101.0), "still waiting for the act");
    demo_act_finished(&d, d.gen);
    CHECK(demo_tick(&d, 101.0) && !demo_playing(&d), "act done: ends at once without a view, well before the 60 s cap");

    demo_block_t av = block("hop+view", DEMO_PAGE_BOT, DEMO_ACT_HOP, true, false);
    demo_offer(&d, &av, 200);
    demo_ready(&d, 200);
    demo_act_finished(&d, d.gen);
    CHECK(!demo_tick(&d, 207.9) && demo_tick(&d, 208.0), "an act with a view plays at least 8 s");

    demo_block_t stuck = block("stuck", DEMO_PAGE_BOT, DEMO_ACT_MOMENT, true, false);
    demo_offer(&d, &stuck, 300);
    demo_ready(&d, 300);
    CHECK(!demo_tick(&d, 359.9) && demo_tick(&d, 360.0), "an act that never ends stops at 60 s");

    demo_block_t hold = block("hold", DEMO_PAGE_WEATHER, DEMO_ACT_NONE, true, true);
    demo_offer(&d, &hold, 400);
    demo_ready(&d, 400);
    CHECK(!demo_tick(&d, 420) && !demo_tick(&d, 459.9) && demo_tick(&d, 460.0), "hold: 60 s");
    demo_block_t hold2 = block("hold2", DEMO_PAGE_BOT, DEMO_ACT_POP, false, true);
    demo_offer(&d, &hold2, 500);
    demo_ready(&d, 500);
    demo_act_finished(&d, d.gen);
    CHECK(!demo_tick(&d, 530), "hold outlasts its act");
    CHECK(demo_offer(&d, NULL, 531) == DEMO_EV_STOP && !demo_playing(&d), "the server clears a hold");
    char q[DEMO_QUERY_MAX];
    CHECK(demo_query(&d, q, sizeof q) && q[0] == 0, "cleared: nothing reported");
    demo_block_t hold3 = block("hold3", DEMO_PAGE_BOT, DEMO_ACT_NONE, true, true);
    demo_offer(&d, &hold3, 600);
    demo_block_t next = block("next", DEMO_PAGE_BOT, DEMO_ACT_NONE, true, false);
    CHECK(demo_offer(&d, &next, 610) == DEMO_EV_START && strcmp(d.cur.id, "next") == 0, "a new id ends a hold");
    demo_ready(&d, 610);
    demo_act_finished(&d, d.gen);
    CHECK(!demo_tick(&d, 611), "act_finished without an act changes nothing");

    demo_block_t late = block("late", DEMO_PAGE_POMODORO, DEMO_ACT_NONE, true, false);
    demo_offer(&d, &late, 700);
    CHECK(!demo_tick(&d, 704.9), "not ready yet: waiting");
    demo_ready(&d, 704.9);
    CHECK(!demo_tick(&d, 708.0) && !demo_tick(&d, 712.8) && demo_tick(&d, 712.9),
          "the 8 s start when the scene is ready, not at the offer");
    demo_ready(&d, 720);
    demo_block_t never = block("never", DEMO_PAGE_BOT, DEMO_ACT_NONE, true, false);
    demo_offer(&d, &never, 800);
    CHECK(!demo_tick(&d, 800 + DEMO_READY_MAX_S - 0.1) && demo_tick(&d, 800 + DEMO_READY_MAX_S) && !demo_playing(&d),
          "never ready: it ends after DEMO_READY_MAX_S");

    demo_block_t sl = block("sleepy", DEMO_PAGE_BOT, DEMO_ACT_SLEEPY, false, false);
    demo_offer(&d, &sl, 900);
    demo_ready(&d, 900);
    CHECK(!demo_tick(&d, 907.9) && demo_tick(&d, 908.0), "sleepy has no natural end: it plays 8 s, view or not");

    demo_block_t as = block("asleep", DEMO_PAGE_BOT, DEMO_ACT_ASLEEP, false, false);
    demo_offer(&d, &as, 1000);
    demo_ready(&d, 1000);
    CHECK(!demo_tick(&d, 1007.9) && demo_tick(&d, 1008.0), "asleep has no natural end either: 8 s");
    demo_block_t asv = block("asleep-view", DEMO_PAGE_BOT, DEMO_ACT_ASLEEP, true, false);
    demo_offer(&d, &asv, 1100);
    demo_ready(&d, 1100);
    demo_act_finished(&d, d.gen);
    CHECK(!demo_tick(&d, 1107.9) && demo_tick(&d, 1108.0), "asleep with a view: 8 s, an act report changes nothing");
}

static void test_stale_act(void)
{
    demo_t d;
    demo_init(&d);
    demo_block_t a = block("a", DEMO_PAGE_BOT, DEMO_ACT_HOP, false, false);
    demo_offer(&d, &a, 0);
    demo_ready(&d, 0);
    unsigned ga = d.gen;
    demo_block_t b = block("b", DEMO_PAGE_BOT, DEMO_ACT_POP, false, false);
    CHECK(demo_offer(&d, &b, 1) == DEMO_EV_START, "b replaces a");
    demo_ready(&d, 1);
    demo_act_finished(&d, ga);
    CHECK(!demo_tick(&d, 1.1) && demo_playing(&d), "a's late act completion does not end b");
    demo_act_finished(&d, d.gen);
    CHECK(demo_tick(&d, 1.2), "b's own completion does");
}

static void test_mood(void)
{
    enum { REAL = 2, VIEW = 5, FROZEN = 3 };
    demo_t d;
    demo_init(&d);
    CHECK(demo_mood(&d, REAL, VIEW, FROZEN) == REAL, "no demo: real");
    demo_block_t v = block("v", DEMO_PAGE_BOT, DEMO_ACT_NONE, true, false);
    demo_offer(&d, &v, 0);
    CHECK(demo_mood(&d, REAL, VIEW, FROZEN) == VIEW, "a view scene: its mood");
    CHECK(demo_mood(&d, REAL, -1, FROZEN) == FROZEN, "a view without a mood: the mood at the start");
    demo_block_t n = block("n", DEMO_PAGE_BOT, DEMO_ACT_HOP, false, false);
    demo_offer(&d, &n, 1);
    CHECK(demo_mood(&d, REAL, VIEW, FROZEN) == FROZEN && demo_mood(&d, REAL, VIEW, -1) == REAL,
          "no view: frozen, else real");
    demo_input(&d);
    CHECK(demo_mood(&d, REAL, VIEW, FROZEN) == REAL, "ended: real");
}

static void test_quiet(void)
{
    CHECK(demo_quiet_dim(false, true) && !demo_quiet_dim(true, true) && !demo_quiet_dim(false, false),
          "quiet dimming: the real quiet, off while a demo plays");
    CHECK(demo_calm(true, true, true, false, false), "the quiet calm scene is calm, real quiet and setting off");
    CHECK(!demo_calm(true, true, false, true, true), "a scene view without quiet is not calm in real quiet hours");
    CHECK(!demo_calm(true, false, false, true, true), "a scene without a view is not calm");
    CHECK(demo_calm(false, true, true, false, false), "the scene's view still applied just after the end: still its calm");
    CHECK(demo_calm(false, false, false, true, true) && !demo_calm(false, false, false, true, false) &&
              !demo_calm(false, false, true, false, true),
          "no demo: the real quiet and the setting");
}

static demo_gate_act_t gate(demo_gate_t *g, bool playing, bool input, bool held, bool view_on, double t)
{
    demo_gate_in_t in = {.playing = playing, .input = input, .held = held, .view_on = view_on};
    return demo_gate(g, &in, t);
}

static void test_gate(void)
{
    demo_gate_t g = {0};
    CHECK(gate(&g, false, true, false, false, 0) == DEMO_GATE_PASS, "no demo: inputs pass");
    CHECK(gate(&g, true, false, false, false, 1) == DEMO_GATE_DROP, "a demo offered, not yet shown: nothing to end");
    CHECK(gate(&g, true, true, true, true, 1.0) == DEMO_GATE_END, "touch down while offered: ends it, dropped");
    CHECK(gate(&g, false, false, true, true, 1.05) == DEMO_GATE_DROP, "still down");
    CHECK(gate(&g, false, true, false, true, 1.1) == DEMO_GATE_DROP, "the release's tap is dropped");
    CHECK(gate(&g, false, false, false, false, 1.3) == DEMO_GATE_DROP, "within 300 ms of the last input");
    CHECK(gate(&g, false, false, false, false, 1.45) == DEMO_GATE_PASS && !g.on, "the gesture is over");
    CHECK(gate(&g, false, true, false, false, 1.5) == DEMO_GATE_PASS, "a new input reaches the page");

    g = (demo_gate_t){0};
    CHECK(gate(&g, true, true, false, true, 10.0) == DEMO_GATE_END, "the first detent ends it");
    for (int i = 1; i <= 9; i++) {
        double t = 10 + 0.1 * i;
        CHECK(gate(&g, false, true, false, i < 5, t) == DEMO_GATE_DROP, "detent at %.1f", t);
    }
    CHECK(gate(&g, false, false, false, false, 11.1) == DEMO_GATE_DROP, "200 ms after the last detent");
    CHECK(gate(&g, false, false, false, false, 11.25) == DEMO_GATE_PASS, "300 ms quiet: inputs pass");

    g = (demo_gate_t){0};
    CHECK(gate(&g, true, true, true, true, 20.0) == DEMO_GATE_END, "a push ends it");
    CHECK(gate(&g, false, false, false, true, 21.0) == DEMO_GATE_DROP, "the scene's view is still applied");
    CHECK(gate(&g, false, false, true, false, 21.1) == DEMO_GATE_DROP, "the button still down");
    CHECK(gate(&g, false, false, false, false, 21.2) == DEMO_GATE_PASS, "all clear");

    g = (demo_gate_t){0};
    CHECK(gate(&g, false, true, false, true, 30.0) == DEMO_GATE_DROP, "ended on its own, real view not back: dropped");
    CHECK(gate(&g, false, true, false, false, 30.1) == DEMO_GATE_DROP, "and that gesture's rest too");
    CHECK(gate(&g, false, false, false, false, 30.5) == DEMO_GATE_PASS, "then clear");

    g = (demo_gate_t){0};
    CHECK(gate(&g, true, true, false, true, 40.0) == DEMO_GATE_END, "input ends a queued scene");
    CHECK(gate(&g, true, false, false, true, 40.1) == DEMO_GATE_DROP && g.on, "the next one offered while the view lags");
    demo_gate_start(&g);
    CHECK(!g.on, "a new scene starts: the old gesture no longer counts");
    CHECK(gate(&g, true, false, false, true, 40.2) == DEMO_GATE_DROP, "while it plays: dropped");
    CHECK(gate(&g, false, false, false, false, 48.2) == DEMO_GATE_PASS && !g.on, "its natural end is not an input end");
}

static void test_input(void)
{
    demo_t d;
    demo_init(&d);
    CHECK(!demo_input(&d), "input without a demo is not consumed");
    demo_block_t a = block("a", DEMO_PAGE_BOT, DEMO_ACT_CHASE, true, true);
    demo_offer(&d, &a, 0);
    unsigned gen = d.gen;
    CHECK(demo_input(&d) && !demo_playing(&d) && d.gen != gen, "input ends it and is consumed");
    CHECK(!demo_input(&d), "the next input reaches the page");
    char q[DEMO_QUERY_MAX];
    CHECK(demo_query(&d, q, sizeof q) && strcmp(q, "demo_id=a&demo_state=done") == 0, "done after input");
    CHECK(!demo_tick(&d, 100), "nothing to end");
    demo_block_t out;
    demo_ready(&d, 100);
    CHECK(!demo_take_start(&d, &out), "no start after the end");
}

static void test_query(void)
{
    demo_t d;
    demo_init(&d);
    demo_block_t a = block("scene wind/2&x=ü", DEMO_PAGE_BOT, DEMO_ACT_NONE, false, false);
    demo_offer(&d, &a, 0);
    char q[DEMO_QUERY_MAX];
    CHECK(demo_query(&d, q, sizeof q) && strcmp(q, "demo_id=scene%20wind%2F2%26x%3D%C3%BC&demo_state=playing") == 0,
          "escaped: %s", q);
    demo_block_t big = block("", DEMO_PAGE_BOT, DEMO_ACT_NONE, false, false);
    memset(big.id, '/', DEMO_ID_MAX);
    big.id[DEMO_ID_MAX] = 0;
    demo_offer(&d, &big, 1);
    CHECK(demo_query(&d, q, sizeof q) && strlen(q) == sizeof "demo_id=&demo_state=playing" - 1 + 3 * DEMO_ID_MAX,
          "the worst case fits DEMO_QUERY_MAX");
    char small[16];
    CHECK(!demo_query(&d, small, sizeof small) && small[0] == 0, "too small: empty");
}

static int parse_dir(const char *dir, bool need)
{
    DIR *dp = opendir(dir);
    if (!dp) {
        CHECK(!need, "no fixtures in %s", dir);
        return 0;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(dp))) {
        size_t l = strlen(e->d_name);
        if (l < 6 || strcmp(e->d_name + l - 5, ".json") != 0) continue;
        char *j = load(dir, e->d_name);
        CHECK(j != NULL, "%s/%s reads", dir, e->d_name);
        if (!j) continue;
        n++;
        static knob_view_t v, dv;
        CHECK(knob_view_read_full(j, &v, NULL, &dv) == KNOB_VIEW_OK, "%s parses", e->d_name);
        CHECK(v.demo.present, "%s: demo block", e->d_name);
        cJSON *root = cJSON_Parse(j);
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "demo");
        const char *act = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(d, "act"));
        demo_act_t a;
        CHECK(!act || demo_act_parse(act, &a), "%s: act %s", e->d_name, act ? act : "");
        CHECK(cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(d, "hold")), "%s: hold is a bool", e->d_name);
        const cJSON *view = cJSON_GetObjectItemCaseSensitive(d, "view");
        CHECK(!view || (cJSON_IsObject(view) && v.demo.has_view), "%s: view", e->d_name);
        CHECK(view || act, "%s: a scene has a view or an act", e->d_name);
        if (view) {
            CHECK(dv.has_mood == cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(view, "mood")) &&
                      dv.has_weather == cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(view, "weather")) &&
                      dv.has_pomo == cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(view, "pomo")),
                  "%s: demo view blocks", e->d_name);
        }
        cJSON_Delete(root);
        free(j);
    }
    closedir(dp);
    return n;
}

int main(int argc, char **argv)
{
    test_act();
    test_page();
    test_block();
    test_view();
    test_once();
    test_timing();
    test_input();
    test_query();
    test_stale_act();
    test_mood();
    test_quiet();
    test_gate();
    if (argc > 1) CHECK(parse_dir(argv[1], true) >= 4, "own demo fixtures");
    int ember = argc > 2 ? parse_dir(argv[2], false) : 0;
    if (failures) {
        printf("demo: %d failure(s)\n", failures);
        return 1;
    }
    printf("demo: all tests passed (%d Ember demo fixtures)\n", ember);
    return 0;
}
