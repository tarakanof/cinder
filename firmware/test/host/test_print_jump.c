#include <stdio.h>
#include <string.h>

#include "print_jump.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SRV0 1782000000.0
#define ID(s) ((long long)((SRV0 + (s)) * 1000))

static print_block_t blk(print_phase_t ph, print_ev_kind_t kind, long long id, long long at)
{
    print_block_t b;
    memset(&b, 0, sizeof b);
    b.phase = ph;
    b.event = (print_event_t){id, kind, at};
    return b;
}

typedef struct {
    pj_t j;
    print_seen_t seen;
    double boot_t;
} rig_t;

static void rig_init(rig_t *r)
{
    pj_init(&r->j);
    print_seen_init(&r->seen);
}

static pj_in_t in_at(double t, const print_block_t *b)
{
    return (pj_in_t){.t = t, .b = b, .have_srv = true, .srv_now = SRV0 + t, .page_on = true, .real_ok = true};
}

static bool pj_one(rig_t *r, pj_in_t in) { return pj_tick(&r->j, &r->seen, &in); }

static bool tick(rig_t *r, pj_in_t in)
{
    bool fired = false;
    for (int i = 0; i < 4; i++) {
        pj_in_t s = in;
        s.t += 0.25 * i;
        s.srv_now += 0.25 * i;
        fired |= pj_one(r, s);
    }
    return fired;
}

static void test_once_per_id(void)
{
    rig_t r;
    rig_init(&r);
    CHECK(!tick(&r, in_at(10, NULL)), "no block: no jump");
    print_block_t idle = blk(PRINT_IDLE, PRINT_EV_NONE, 0, 0);
    CHECK(!tick(&r, in_at(11, &idle)), "idle: no jump");
    print_block_t heat = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    CHECK(tick(&r, in_at(21, &heat)), "a new started id jumps");
    CHECK(!tick(&r, in_at(22, &heat)) && !tick(&r, in_at(23, &heat)), "and only once across view polls");
    print_block_t run = blk(PRINT_PRINTING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    CHECK(!tick(&r, in_at(100, &run)), "the same id in a later phase: no second jump");
    print_block_t next = blk(PRINT_PRINTING, PRINT_EV_STARTED, ID(600), (long long)SRV0 + 600);
    CHECK(tick(&r, in_at(601, &next)), "the next started id jumps again");
    CHECK(r.seen.id == ID(600), "the shared store holds the newest id");
}

static void test_other_kinds(void)
{
    rig_t r;
    rig_init(&r);
    static const print_ev_kind_t K[] = {PRINT_EV_ATTENTION, PRINT_EV_FINISHED, PRINT_EV_STOPPED, PRINT_EV_ERROR, PRINT_EV_UNKNOWN};
    for (int i = 0; i < 5; i++) {
        print_block_t b = blk(PRINT_PRINTING, K[i], ID(20 + i), (long long)SRV0 + 20 + i);
        CHECK(!tick(&r, in_at(25 + i, &b)), "kind %d: no jump", K[i]);
        CHECK(r.seen.id == 0, "kind %d: left to its own consumer", K[i]);
    }
}

static void test_shared_store(void)
{
    rig_t r;
    rig_init(&r);
    print_block_t st = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    CHECK(tick(&r, in_at(21, &st)), "started jumps");
    print_block_t fin = blk(PRINT_FINISHED, PRINT_EV_FINISHED, ID(7200), (long long)SRV0 + 7200);
    print_event_t e = fin.event;
    CHECK(print_seen_take(&r.seen, &e, true, SRV0 + 7201), "the moments' take of a later finish still works on the shared store");
    print_block_t late = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    CHECK(!tick(&r, in_at(7300, &late)), "an id the moments' store has passed never jumps");
    print_block_t att = blk(PRINT_ATTENTION, PRINT_EV_ATTENTION, ID(7400), (long long)SRV0 + 7400);
    print_event_t a = att.event;
    print_seen_mark(&r.seen, &a);
    print_block_t old = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(7390), (long long)SRV0 + 7390);
    CHECK(!tick(&r, in_at(7401, &old)), "a started id below one taken by the moments is not new");
}

static void test_reboot(void)
{
    rig_t r;
    rig_init(&r);
    print_block_t run = blk(PRINT_PRINTING, PRINT_EV_STARTED, ID(-60), (long long)SRV0 - 60);
    CHECK(!tick(&r, in_at(3, &run)), "a reboot one minute into a print: the event is older than the boot, no jump");
    CHECK(r.seen.id == ID(-60), "and it is marked");
    CHECK(!tick(&r, in_at(4, &run)), "still none");

    rig_init(&r);
    run = blk(PRINT_PRINTING, PRINT_EV_STARTED, ID(-1500), (long long)SRV0 - 1500);
    CHECK(!tick(&r, in_at(3, &run)), "a reboot 25 min into a print: none");

    rig_init(&r);
    pj_in_t quiet = in_at(2, NULL);
    CHECK(!tick(&r, quiet), "first clock, no block");
    print_block_t born = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(5), (long long)SRV0 + 5);
    CHECK(tick(&r, in_at(6, &born)), "a print that starts after this boot jumps");

    rig_init(&r);
    print_block_t during = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(1), (long long)SRV0 + 1);
    CHECK(tick(&r, in_at(2, &during)), "a start in the first seconds, with the first view: after the boot, it jumps");

    rig_init(&r);
    pj_in_t noclock = in_at(2, &born);
    noclock.have_srv = false;
    CHECK(!tick(&r, noclock) && r.seen.id == 0, "no Ember clock: it waits unmarked");
    CHECK(tick(&r, in_at(6, &born)), "and jumps once the clock is known");
}

static void test_page_off(void)
{
    rig_t r;
    rig_init(&r);
    print_block_t st = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    pj_in_t off = in_at(21, &st);
    off.page_on = false;
    CHECK(!tick(&r, off), "page off: no jump");
    CHECK(!tick(&r, in_at(22, &st)), "turning the page on later does not bring the old event back");
    print_block_t next = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(300), (long long)SRV0 + 300);
    CHECK(tick(&r, in_at(301, &next)), "the next print jumps");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    pj_in_t blocked = in_at(21, &st);
    blocked.blocked = true;
    CHECK(!tick(&r, blocked), "held");
    pj_in_t gone = blocked;
    gone.page_on = false;
    CHECK(!tick(&r, gone), "the page switched off while held");
    CHECK(!tick(&r, in_at(23, &st)), "and on again: the held jump is gone");
}

static void test_deferred(void)
{
    rig_t r;
    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    print_block_t st = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    pj_in_t w = in_at(21, &st);
    w.blocked = true;
    CHECK(!tick(&r, w), "an agent waits: no jump");
    w.t = 30;
    w.srv_now = SRV0 + 30;
    CHECK(!tick(&r, w), "still waiting");
    CHECK(tick(&r, in_at(40, &st)), "the wait ends: the jump comes");
    CHECK(!tick(&r, in_at(41, &st)), "and not again");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    w = in_at(21, &st);
    w.blocked = true;
    tick(&r, w);
    w.t = 700;
    w.srv_now = SRV0 + 700;
    print_block_t run = blk(PRINT_PRINTING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    w.b = &run;
    CHECK(!tick(&r, w), "a wait over 5 min is still a wait");
    CHECK(tick(&r, in_at(701, &run)), "a held jump outlives the freshness window while the print runs");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    w = in_at(21, &st);
    w.blocked = true;
    tick(&r, w);
    print_block_t fin = blk(PRINT_FINISHED, PRINT_EV_FINISHED, ID(500), (long long)SRV0 + 500);
    CHECK(!tick(&r, in_at(501, &fin)) && !tick(&r, in_at(502, &st)), "the print ended during the wait: no jump, even if the view showed the event again");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    w = in_at(21, &st);
    w.blocked = true;
    tick(&r, w);
    print_block_t stopped = blk(PRINT_STOPPED, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    CHECK(!tick(&r, in_at(40, &stopped)), "the phase is no longer a running one: no jump");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    w = in_at(21, &st);
    w.blocked = true;
    tick(&r, w);
    CHECK(!tick(&r, in_at(40, NULL)), "the block went away: the held jump is dropped");
    CHECK(!tick(&r, in_at(41, &st)), "and the event is spent");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    w = in_at(21, &st);
    w.blocked = true;
    tick(&r, w);
    print_block_t att = blk(PRINT_ATTENTION, PRINT_EV_ATTENTION, ID(200), (long long)SRV0 + 200);
    CHECK(tick(&r, in_at(201, &att)), "a later attention event does not cancel it: the print still runs, the jump comes");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    w = in_at(21, &st);
    w.blocked = true;
    tick(&r, w);
    print_block_t aged = blk(PRINT_PRINTING, PRINT_EV_NONE, 0, 0);
    w = in_at(2200, &aged);
    w.blocked = true;
    CHECK(!tick(&r, w), "a 35 min wait: the event has aged out of the view, still held");
    CHECK(tick(&r, in_at(2210, &aged)), "and the jump comes when the wait ends while the print runs");
}

static void test_settle(void)
{
    rig_t r;
    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    print_block_t st = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    pj_in_t held = in_at(30, &st);
    held.blocked = true;
    CHECK(!pj_one(&r, held), "held at 30");
    CHECK(!pj_one(&r, in_at(30.1, &st)), "the block ended: not at once, the mood and the block of one poll must agree");
    CHECK(!pj_one(&r, in_at(30.4, &st)), "still settling");
    CHECK(pj_one(&r, in_at(30.6, &st)), "PJ_SETTLE_S after the block ended it fires");
    CHECK(!pj_one(&r, in_at(30.7, &st)), "once");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    CHECK(!pj_one(&r, in_at(40, &st)), "a new event is not acted on in the frame it is first seen");
    CHECK(!pj_one(&r, in_at(40.3, &st)) && pj_one(&r, in_at(40.5, &st)), "but PJ_SETTLE_S later");
}

static void test_turned_away(void)
{
    rig_t r;
    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    print_block_t st = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    pj_in_t held = in_at(21, &st);
    held.blocked = true;
    CHECK(!tick(&r, held), "held behind a wait");
    pj_in_t there = held;
    there.on_page = true;
    CHECK(!tick(&r, there), "the user turned to the print page meanwhile");
    CHECK(!tick(&r, in_at(40, &st)), "and away again: the wait ending brings nothing");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    pj_in_t at = in_at(21, &st);
    at.on_page = true;
    CHECK(!tick(&r, at) && r.seen.id == ID(20), "a start while the user is on the print page: spent, nothing to do");
    CHECK(!tick(&r, in_at(22, &st)), "and not held for later");
    print_block_t next = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(900), (long long)SRV0 + 900);
    CHECK(tick(&r, in_at(901, &next)), "the next id jumps");
}

static void test_gate(void)
{
    pj_gate_t g = {.bot_listed = true, .mood_known = true, .t = 100, .input_t = 50};
    CHECK(!pj_blocked(&g), "idle, known mood, no input: open");
    g.mood_known = false;
    CHECK(pj_blocked(&g), "an unknown mood (a failed poll, no Wi-Fi) blocks: a waiting agent may be behind it");
    g.mood_known = true;
    g.waiting = true;
    CHECK(pj_blocked(&g), "a waiting agent blocks");
    g.waiting = false;
    g.bot_listed = false;
    g.mood_known = false;
    CHECK(!pj_blocked(&g), "the bot page off: the view carries no mood, so no agent can be waited for");
    g.waiting = true;
    CHECK(!pj_blocked(&g), "and a stale waiting mood from before the bot page went off is no reason either");
    g.waiting = false;
    g.bot_listed = true;
    g.mood_known = true;
    g.usage_open = true;
    CHECK(pj_blocked(&g), "the usage screen blocks");
    g.usage_open = false;
    g.down = true;
    CHECK(pj_blocked(&g), "a button or touch down blocks");
    g.down = false;
    g.input_t = 99.5;
    CHECK(pj_blocked(&g), "input half a second ago blocks");
    g.input_t = 99.0;
    CHECK(!pj_blocked(&g), "a second after the input it is open");
}

static void test_not_real(void)
{
    rig_t r;
    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    print_block_t st = blk(PRINT_HEATING, PRINT_EV_STARTED, ID(20), (long long)SRV0 + 20);
    pj_in_t scene = in_at(21, &st);
    scene.real_ok = false;
    CHECK(!tick(&r, scene) && r.seen.id == 0, "a demo scene or offline link: not taken, not marked");
    CHECK(tick(&r, in_at(22, &st)), "back to the real view within the window: it jumps");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    CHECK(!tick(&r, scene) && tick(&r, in_at(400, &st)) && r.seen.id == ID(20), "back after 5 min, still inside Ember's 30 min and the print runs: it jumps");
    CHECK(!tick(&r, in_at(401, &st)), "once");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    print_block_t demo = blk(PRINT_HEATING, PRINT_EV_STARTED, 3502309358LL, (long long)SRV0 + 20);
    CHECK(!tick(&r, in_at(21, &demo)) && r.seen.id == 0, "a demo event never jumps and never marks");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    pj_in_t w = in_at(21, &st);
    w.blocked = true;
    tick(&r, w);
    scene = in_at(22, &st);
    scene.real_ok = false;
    CHECK(!tick(&r, scene), "a scene holds a pending jump");
    CHECK(tick(&r, in_at(23, &st)), "and it comes after");

    rig_init(&r);
    CHECK(!tick(&r, in_at(2, NULL)), "clock");
    w = in_at(21, &st);
    w.blocked = true;
    tick(&r, w);
    pj_in_t demo_view = in_at(22, NULL);
    demo_view.real_ok = false;
    CHECK(!tick(&r, demo_view), "a demo view feeds no real block: the held jump waits");
    CHECK(tick(&r, in_at(23, &st)), "and comes with the real view");
}

int main(void)
{
    test_once_per_id();
    test_other_kinds();
    test_shared_store();
    test_reboot();
    test_page_off();
    test_deferred();
    test_not_real();
    test_turned_away();
    test_settle();
    test_gate();
    if (failures) {
        printf("print_jump: %d failure(s)\n", failures);
        return 1;
    }
    printf("print_jump: ok\n");
    return 0;
}
