#include <math.h>
#include <stdio.h>
#include <string.h>

#include "print_moment.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SRV0 1782000000.0
#define REAL_ID (SRV0 * 1000)

static print_block_t blk(print_phase_t ph, print_ev_kind_t kind, long long id, long long at)
{
    print_block_t b;
    memset(&b, 0, sizeof b);
    b.phase = ph;
    b.event = (print_event_t){id, kind, at};
    b.started_at = (long long)SRV0 - 8040;
    return b;
}

static pm_in_t in_at(double t, const print_block_t *b)
{
    return (pm_in_t){.t = t, .b = b, .have_srv = true, .srv_now = SRV0 + t, .mood = 0, .bot_mood = 0, .shown = true, .on = true, .real_ok = true};
}

static bool tick(pm_trigger_t *tr, pm_in_t in, pm_out_t *o) { return pm_tick(tr, &in, o); }

static void settled(pm_trigger_t *tr, double t)
{
    pm_out_t o;
    pm_in_t in = in_at(t, NULL);
    pm_tick(tr, &in, &o);
}

static void test_seen(void)
{
    print_seen_t s;
    print_seen_init(&s);
    print_event_t real = {(long long)REAL_ID, PRINT_EV_FINISHED, (long long)SRV0}, later = {(long long)REAL_ID + 5, PRINT_EV_ATTENTION, 0};
    print_event_t demo = {3502309358LL, PRINT_EV_FINISHED, 1782043200}, none = {0, PRINT_EV_NONE, 0};
    CHECK(print_seen_new(&s, &real), "a real id is new");
    print_seen_mark(&s, &real);
    CHECK(!print_seen_new(&s, &real), "the same id is not new");
    CHECK(print_seen_new(&s, &later), "a later id is new");
    print_seen_mark(&s, &demo);
    print_seen_mark(&s, &none);
    CHECK(s.id == real.id, "demo and empty events leave the memory alone");
    CHECK(!print_seen_new(&s, &demo) && print_event_is_demo(&demo) && !print_event_is_demo(&real) && !print_event_is_demo(&none),
          "a demo id is never new and never real");
    print_event_t edge = {PM_DEMO_ID_LIMIT - 1, PRINT_EV_FINISHED, 0}, first = {PM_DEMO_ID_LIMIT, PRINT_EV_FINISHED, 0};
    CHECK(print_event_is_demo(&edge) && !print_event_is_demo(&first), "demo ids end below 2^32");
}

static void test_done(void)
{
    pm_trigger_t tr;
    pm_out_t o;
    pm_init(&tr);
    settled(&tr, 0);
    print_block_t b = blk(PRINT_FINISHED, PRINT_EV_FINISHED, (long long)REAL_ID, (long long)SRV0 + 1);
    CHECK(!tick(&tr, in_at(1, &b), &o), "not before the bot has settled");
    CHECK(tick(&tr, in_at(3.5, &b), &o) && o.kind == PM_DONE && !o.demo && !o.quiet, "B3 once settled");
    CHECK(o.took_s == b.event.at - b.started_at, "B3 carries the job length: %lld", o.took_s);
    pm_shown(&tr, &o, 3.5);
    CHECK(!tick(&tr, in_at(4, &b), &o), "the same event id plays once");
    CHECK(!tick(&tr, in_at(60, &b), &o), "and still not on later polls");
    b.event.id += 1;
    CHECK(tick(&tr, in_at(70, &b), &o) && o.kind == PM_DONE, "a new id plays again");
}

static void test_reboot(void)
{
    pm_trigger_t tr;
    pm_out_t o;
    pm_init(&tr);
    settled(&tr, 0);
    print_block_t old = blk(PRINT_FINISHED, PRINT_EV_FINISHED, (long long)REAL_ID, (long long)SRV0 - 301 + 10);
    pm_in_t in = in_at(10, &old);
    in.srv_now = SRV0 + 10;
    CHECK(!pm_tick(&tr, &in, &o), "an event over 5 min old at boot plays nothing");
    in.t = 20;
    in.srv_now = SRV0 + 20;
    CHECK(!pm_tick(&tr, &in, &o) && tr.pend == PM_NONE, "and is not kept for later");
    pm_init(&tr);
    settled(&tr, 0);
    print_block_t fresh = blk(PRINT_FINISHED, PRINT_EV_FINISHED, (long long)REAL_ID, (long long)SRV0 - 200);
    in = in_at(5, &fresh);
    in.srv_now = SRV0;
    CHECK(pm_tick(&tr, &in, &o) && o.kind == PM_DONE, "an event under 5 min old at boot plays");
    pm_init(&tr);
    settled(&tr, 0);
    in = in_at(5, &fresh);
    in.have_srv = false;
    CHECK(!pm_tick(&tr, &in, &o) && tr.seen.id == 0, "without the server clock a real event waits and is not marked");
    in.have_srv = true;
    CHECK(pm_tick(&tr, &in, &o), "and plays once the clock is known");
}

static void test_priority(void)
{
    pm_trigger_t tr;
    pm_out_t o;
    print_block_t b = blk(PRINT_FINISHED, PRINT_EV_FINISHED, (long long)REAL_ID, (long long)SRV0);
    pm_init(&tr);
    settled(&tr, 0);
    pm_in_t in = in_at(5, &b);
    in.mood = 2;
    in.bot_mood = 2;
    CHECK(!pm_tick(&tr, &in, &o), "no moment while an agent waits");
    in.t = 100;
    in.srv_now = SRV0 + 100;
    CHECK(!pm_tick(&tr, &in, &o), "still waiting");
    in.mood = 0;
    in.bot_mood = 0;
    in.t = 101;
    in.srv_now = SRV0 + 101;
    CHECK(!pm_tick(&tr, &in, &o), "a mood change restarts the settle");
    in.t = 105;
    in.srv_now = SRV0 + 105;
    CHECK(pm_tick(&tr, &in, &o) && o.kind == PM_DONE, "the pending B3 plays when the agent is done");

    pm_init(&tr);
    settled(&tr, 0);
    in = in_at(5, &b);
    in.mood = 1;
    in.bot_mood = 1;
    pm_tick(&tr, &in, &o);
    in.t = 10;
    in.srv_now = SRV0 + 10;
    CHECK(pm_tick(&tr, &in, &o) && o.quiet, "while agents work: the quiet variant");

    pm_init(&tr);
    settled(&tr, 0);
    in = in_at(5, &b);
    in.shown = false;
    CHECK(!pm_tick(&tr, &in, &o), "nothing off the bot page or while asleep");
    in = in_at(5, &b);
    in.busy = true;
    CHECK(!pm_tick(&tr, &in, &o), "nothing while another moment plays");
    in = in_at(5, &b);
    in.calm = true;
    CHECK(!pm_tick(&tr, &in, &o), "nothing in quiet hours");
    in = in_at(5, &b);
    in.on = false;
    CHECK(!pm_tick(&tr, &in, &o), "nothing with bot.print_moments off");
    in = in_at(400, &b);
    CHECK(!pm_tick(&tr, &in, &o), "a B3 held back over 5 min is dropped");

    pm_init(&tr);
    settled(&tr, 0);
    in = in_at(2, &b);
    in.shown = true;
    in.on = false;
    pm_tick(&tr, &in, &o);
    in = in_at(8, &b);
    CHECK(!pm_tick(&tr, &in, &o), "an event seen with the setting off is not replayed when it turns on");

    pm_init(&tr);
    settled(&tr, 0);
    in = in_at(2, &b);
    in.busy = true;
    pm_tick(&tr, &in, &o);
    print_block_t next = blk(PRINT_HEATING, PRINT_EV_FINISHED, (long long)REAL_ID, (long long)SRV0);
    in = in_at(8, &next);
    CHECK(!pm_tick(&tr, &in, &o), "a B3 is dropped when the next job has started");
}

static void test_attention(void)
{
    pm_trigger_t tr;
    pm_out_t o;
    pm_init(&tr);
    settled(&tr, 0);
    print_block_t b = blk(PRINT_ATTENTION, PRINT_EV_ATTENTION, (long long)REAL_ID, (long long)SRV0);
    CHECK(tick(&tr, in_at(5, &b), &o) && o.kind == PM_ATTENTION && !o.demo, "B4 for an attention event");
    pm_shown(&tr, &o, 5);
    CHECK(!tick(&tr, in_at(60, &b), &o), "not again within 2 min");
    CHECK(!tick(&tr, in_at(124.9, &b), &o), "nor just before");
    CHECK(tick(&tr, in_at(125, &b), &o) && o.kind == PM_ATTENTION, "repeats 2 min after the last one");
    pm_shown(&tr, &o, 125);
    CHECK(tick(&tr, in_at(245, &b), &o), "and again 2 min later");
    pm_shown(&tr, &o, 245);
    CHECK(!tick(&tr, in_at(300, &b), &o), "between repeats nothing");
    print_block_t gone = b;
    gone.phase = PRINT_PRINTING;
    CHECK(!tick(&tr, in_at(400, &gone), &o), "no repeat once the phase left attention");
    CHECK(!tick(&tr, in_at(500, &b), &o), "and the same attention event does not come back");
    b.event.id += 2;
    b.event.at = (long long)SRV0 + 505;
    CHECK(tick(&tr, in_at(510, &b), &o), "a new attention event plays at once");
    pm_shown(&tr, &o, 510);

    pm_init(&tr);
    settled(&tr, 0);
    print_block_t held = b;
    pm_in_t in = in_at(5, &held);
    in.busy = true;
    pm_tick(&tr, &in, &o);
    in = in_at(30, &held);
    CHECK(pm_tick(&tr, &in, &o), "a B4 held back by another moment plays after it");

    pm_init(&tr);
    settled(&tr, 0);
    print_block_t old = blk(PRINT_ATTENTION, PRINT_EV_ATTENTION, (long long)REAL_ID, (long long)SRV0 - 600);
    in = in_at(5, &old);
    in.srv_now = SRV0;
    CHECK(!pm_tick(&tr, &in, &o), "an attention event over 5 min old at boot plays nothing");
    in = in_at(500, &old);
    CHECK(!pm_tick(&tr, &in, &o), "and never starts the repeat");

    pm_init(&tr);
    settled(&tr, 0);
    print_block_t waiting = b;
    waiting.event.id += 10;
    in = in_at(5, &waiting);
    in.mood = 2;
    in.bot_mood = 2;
    pm_tick(&tr, &in, &o);
    in = in_at(700, &waiting);
    in.mood = 0;
    in.bot_mood = 0;
    pm_tick(&tr, &in, &o);
    in = in_at(710, &waiting);
    CHECK(pm_tick(&tr, &in, &o), "B4 stays due while the printer still needs the user, however long the agent waited");
}

static void test_demo(void)
{
    pm_trigger_t tr;
    pm_out_t o;
    pm_init(&tr);
    print_block_t d = blk(PRINT_FINISHED, PRINT_EV_FINISHED, 3502309358LL, 1782043200);
    pm_in_t in = in_at(0.1, &d);
    in.on = false;
    in.calm = true;
    in.mood = 2;
    in.have_srv = false;
    CHECK(pm_tick(&tr, &in, &o) && o.kind == PM_DONE && o.demo, "a demo event plays at once, whatever the setting, quiet hours, mood, settle");
    CHECK(o.took_s == d.event.at - d.started_at, "with its own duration");
    pm_shown(&tr, &o, 0.1);
    CHECK(tr.seen.id == 0 && !tr.armed, "the real memory and B4's repeat stay untouched");
    CHECK(!pm_tick(&tr, &in, &o), "once per demo id");
    d.event.id = 3502309359LL;
    CHECK(pm_tick(&tr, &in, &o) && o.demo, "a new demo id plays again");
    pm_shown(&tr, &o, 0.2);
    in.busy = true;
    d.event.id = 3502309360LL;
    CHECK(!pm_tick(&tr, &in, &o), "a demo waits while a moment plays");
    in.busy = false;
    in.shown = false;
    CHECK(!pm_tick(&tr, &in, &o), "and for the bot page");
    in.b = NULL;
    in.shown = true;
    CHECK(!pm_tick(&tr, &in, &o), "a demo that ended before it could play is dropped");

    pm_init(&tr);
    settled(&tr, 0);
    print_block_t realatt = blk(PRINT_ATTENTION, PRINT_EV_ATTENTION, (long long)REAL_ID, (long long)SRV0);
    CHECK(tick(&tr, in_at(5, &realatt), &o) && !o.demo, "a real B4 armed");
    pm_shown(&tr, &o, 5);
    print_block_t dd = blk(PRINT_FINISHED, PRINT_EV_FINISHED, 99, 1782043200);
    pm_in_t di = in_at(6, &dd);
    di.real_ok = false;
    CHECK(pm_tick(&tr, &di, &o) && o.demo && o.kind == PM_DONE, "a demo B3 plays over a real B4 without the real gate");
    pm_shown(&tr, &o, 6);
    di.t = 7;
    di.b = NULL;
    pm_tick(&tr, &di, &o);
    di.t = 8;
    di.b = &dd;
    dd.event.id = 100;
    dd.phase = PRINT_ATTENTION;
    pm_tick(&tr, &di, &o);
    CHECK(tr.armed && tr.next_at == 125, "demo blocks, and scenes without a print block, leave the real B4 armed and on its schedule");
    CHECK(tick(&tr, in_at(125, &realatt), &o) && !o.demo, "and the real repeat comes");

    pm_init(&tr);
    settled(&tr, 0);
    print_block_t heldb = blk(PRINT_FINISHED, PRINT_EV_FINISHED, (long long)REAL_ID, (long long)SRV0);
    pm_in_t hi = in_at(2, &heldb);
    hi.busy = true;
    pm_tick(&tr, &hi, &o);
    print_block_t da2 = blk(PRINT_ATTENTION, PRINT_EV_ATTENTION, 55, 1782043200);
    di = in_at(3, &da2);
    di.real_ok = false;
    di.busy = true;
    pm_tick(&tr, &di, &o);
    CHECK(tr.pend == PM_DONE, "a demo with a running-job phase does not drop a held real B3");
    hi = in_at(8, &heldb);
    CHECK(pm_tick(&tr, &hi, &o) && !o.demo && o.kind == PM_DONE, "which plays when the demo is over");

    pm_init(&tr);
    settled(&tr, 0);
    pm_in_t gap = in_at(5, &heldb);
    gap.real_ok = false;
    CHECK(!pm_tick(&tr, &gap, &o) && tr.seen.id == 0, "between scenes a real event is neither played nor marked");
    CHECK(pm_tick(&tr, &(pm_in_t){.t = 10, .b = &heldb, .have_srv = true, .srv_now = SRV0 + 10, .shown = true, .on = true, .real_ok = true}, &o) ||
              tr.pend == PM_DONE,
          "and is taken once the real view is back");

    pm_init(&tr);
    settled(&tr, 0);
    print_block_t real = blk(PRINT_ATTENTION, PRINT_EV_ATTENTION, (long long)REAL_ID, (long long)SRV0);
    print_block_t da = blk(PRINT_ATTENTION, PRINT_EV_ATTENTION, 77, 1782043200);
    CHECK(tick(&tr, in_at(5, &real), &o) && !o.demo, "a real B4");
    pm_shown(&tr, &o, 5);
    CHECK(tick(&tr, in_at(6, &da), &o) && o.demo && o.kind == PM_ATTENTION, "a demo B4 on top");
    pm_shown(&tr, &o, 6);
    CHECK(!tick(&tr, in_at(100, &real), &o), "the real event is not replayed when the demo ends");
    CHECK(tick(&tr, in_at(125, &real), &o) && !o.demo, "and the real repeat keeps its schedule (5 + 120 s)");
}

int main(void)
{
    test_seen();
    test_done();
    test_reboot();
    test_priority();
    test_attention();
    test_demo();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("print moment: ok\n");
    return 0;
}
