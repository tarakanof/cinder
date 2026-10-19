#include <stdio.h>
#include <string.h>

#include "shown.h"
#include "wake_turn.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { EV_PRESS, EV_STEAL, EV_REQUEUE, EV_WAKE, EV_SHOWN };

typedef struct {
    int at_ms;
    int kind;
    int item;
    const char *page;
} ev_t;

#define QMAX 8
#define EVMAX 16

static struct {
    int now;
    unsigned efd;
    int q[QMAX];
    int qn;
    int stolen;
    ev_t ev[EVMAX];
    int evn;
    ev_t drain_ev;
    bool drain_armed;
    bool press_after_take;
    bool wait_fails;
    int waits;
    int takes_blocking;
    shown_t shown;
} S;

static void reset(void)
{
    memset(&S, 0, sizeof S);
    S.shown = (shown_t)SHOWN_INIT;
}

static void efd_write(void) { S.efd++; }

static unsigned efd_read(void)
{
    unsigned v = S.efd;
    S.efd = 0;
    return v;
}

static void push_back(int item) { S.q[S.qn++] = item; }

static void push_front(int item)
{
    memmove(&S.q[1], &S.q[0], sizeof S.q[0] * (size_t)S.qn);
    S.q[0] = item;
    S.qn++;
}

static bool pop(int *item)
{
    if (!S.qn) return false;
    *item = S.q[0];
    memmove(&S.q[0], &S.q[1], sizeof S.q[0] * (size_t)--S.qn);
    return true;
}

static void fire(const ev_t *e)
{
    if (e->kind == EV_PRESS) {
        push_back(e->item);
        efd_write();
    } else if (e->kind == EV_STEAL) {
        pop(&S.stolen);
    } else if (e->kind == EV_REQUEUE) {
        push_front(S.stolen);
        efd_write();
    } else if (e->kind == EV_WAKE) {
        efd_write();
    } else if (shown_note(&S.shown, e->page, -1, e->at_ms)) {
        efd_write();
    }
}

static void at(int ms, int kind, int item) { S.ev[S.evn++] = (ev_t){.at_ms = ms, .kind = kind, .item = item}; }

static void shown_at(int ms, const char *page) { S.ev[S.evn++] = (ev_t){.at_ms = ms, .kind = EV_SHOWN, .page = page}; }

static int next_ev(void)
{
    int best = -1;
    for (int i = 0; i < S.evn; i++)
        if (S.ev[i].at_ms >= 0 && (best < 0 || S.ev[i].at_ms < S.ev[best].at_ms)) best = i;
    return best;
}

static bool advance(int until)
{
    int i = next_ev();
    if (i >= 0 && S.ev[i].at_ms <= until) {
        if (S.ev[i].at_ms > S.now) S.now = S.ev[i].at_ms;
        fire(&S.ev[i]);
        S.ev[i].at_ms = -1;
        return true;
    }
    S.now = until;
    return false;
}

static void fire_due(void)
{
    while (advance(S.now)) {
    }
}

static bool f_take(void *ctx, int wait_ms, void *item)
{
    (void)ctx;
    fire_due();
    if (pop(item)) return true;
    if (!wait_ms && S.press_after_take) {
        S.press_after_take = false;
        fire(&(ev_t){.kind = EV_PRESS, .item = 4});
    }
    if (!wait_ms) return false;
    S.takes_blocking++;
    int end = S.now + wait_ms;
    while (advance(end))
        if (pop(item)) return true;
    return false;
}

static void drain(void)
{
    efd_read();
    if (S.drain_armed) {
        S.drain_armed = false;
        fire(&S.drain_ev);
    }
}

static int f_wait(void *ctx, int ms)
{
    (void)ctx;
    S.waits++;
    if (S.wait_fails) return -1;
    fire_due();
    int end = S.now + ms;
    while (!S.efd && advance(end)) {
    }
    if (!S.efd) return 0;
    drain();
    return 1;
}

static const wake_ops_t OPS = {.take = f_take, .wait = f_wait};

static void test_take_first(void)
{
    reset();
    push_back(7);
    int item = 0;
    CHECK(wake_turn(&OPS, 1000, &item) && item == 7 && S.now == 0 && S.waits == 0, "a queued item is taken before waiting");
}

static void test_press_in_window(void)
{
    reset();
    S.press_after_take = true;
    int item = 0;
    bool got = wake_turn(&OPS, 1000, &item);
    CHECK(got && item == 4 && S.now == 0, "a press right after the first take ends the wait and is taken: got %d at %d ms", got,
          S.now);
}

static void test_press_mid_wait(void)
{
    reset();
    at(420, EV_PRESS, 5);
    int item = 0;
    CHECK(wake_turn(&OPS, 1000, &item) && item == 5 && S.now == 420, "a press mid-wait is taken at the press: %d ms", S.now);
}

static void test_enqueue_during_drain(void)
{
    reset();
    at(100, EV_WAKE, 0);
    S.drain_ev = (ev_t){.kind = EV_PRESS, .item = 6};
    S.drain_armed = true;
    int item = 0;
    CHECK(wake_turn(&OPS, 1000, &item) && item == 6 && S.now == 100, "a press during the drain is taken in the same turn");
    CHECK(S.efd == 1, "its wake is not lost: %u", S.efd);
    int t = S.now;
    CHECK(!wake_turn(&OPS, 1000, &item) && S.now == t, "that wake costs one turn");
    CHECK(!wake_turn(&OPS, 1000, &item) && S.now == t + 1000, "then the wait blocks: %d", S.now - t);
}

static void test_legacy_requeue(void)
{
    reset();
    at(100, EV_PRESS, 8);
    at(100, EV_STEAL, 0);
    at(300, EV_REQUEUE, 0);
    int item = 0;
    CHECK(!wake_turn(&OPS, 1000, &item) && S.now == 100, "the wake is used up while the legacy task holds the item");
    CHECK(wake_turn(&OPS, 900, &item) && item == 8 && S.now == 300, "its re-queue wakes the wait and is taken at once: %d ms",
          S.now);
}

static void test_wait_fails(void)
{
    reset();
    S.wait_fails = true;
    int item = 0;
    CHECK(!wake_turn(&OPS, 800, &item) && S.now == 800 && S.takes_blocking == 1, "no wait: blocks on the queue, no spin: %d ms",
          S.now);
    at(S.now + 250, EV_PRESS, 9);
    int t = S.now;
    CHECK(wake_turn(&OPS, 800, &item) && item == 9 && S.now == t + 250, "and a press still ends it at once");
}

static void test_stale_wake(void)
{
    reset();
    efd_write();
    efd_write();
    efd_write();
    int item = 0, turns = 0;
    while (!wake_turn(&OPS, 1000, &item) && S.now == 0 && turns < 10) turns++;
    CHECK(turns == 1 && S.now == 1000, "three coalesced stale wakes cost one turn: %d turns, %d ms", turns, S.now);
}

static int plain_wait(int ms)
{
    int end = S.now + ms;
    int turns = 0;
    for (;;) {
        turns++;
        int left = end - S.now;
        if (left <= 0) break;
        fire_due();
        int due = shown_wait_ms(&S.shown, S.now, true);
        if (due == 0) break;
        int item;
        if (wake_turn(&OPS, wake_slice_ms(left, due, -1, false), &item)) break;
        CHECK(turns < 100, "no spin");
        if (turns >= 100) break;
    }
    return turns;
}

static void poll(void)
{
    char q[SHOWN_QUERY_MAX];
    shown_val_t v;
    shown_query(&S.shown, q, sizeof q, &v);
    shown_sent(&S.shown, v);
}

static void test_latency(void)
{
    reset();
    shown_note(&S.shown, "bot", -1, 0);
    poll();
    shown_at(137, "nowplaying");
    plain_wait(5000);
    CHECK(S.now >= 137 + SHOWN_DEBOUNCE_MS && S.now <= 137 + SHOWN_DEBOUNCE_MS + 20,
          "plain poll goes out at the settle, %d ms after the change (was up to ~1300)", S.now - 137);
    poll();
    int t = S.now;
    shown_at(t + 50, "bot");
    shown_at(t + 150, "nowplaying");
    shown_at(t + 250, "bot");
    plain_wait(5000);
    CHECK(S.now == t + 250 + SHOWN_DEBOUNCE_MS, "three turns 100 ms apart: one poll at the last settle: %d", S.now - t);
    poll();
    t = S.now;
    int turns = plain_wait(5000);
    CHECK(S.now == t + 5000 && turns <= 7, "no change: the full poll_ms in 1 s slices: %d ms, %d turns", S.now - t, turns);
    t = S.now;
    at(t + 600, EV_PRESS, 1);
    plain_wait(5000);
    CHECK(S.now == t + 600, "a press ends the plain-poll wait at once: %d", S.now - t);
}

static void test_slice(void)
{
    CHECK(wake_slice_ms(5000, -1, -1, false) == WAKE_SLICE_MAX_MS, "1 s slices");
    CHECK(wake_slice_ms(400, -1, -1, false) == 400, "the time left");
    CHECK(wake_slice_ms(5000, 120, -1, false) == 120, "the debounce left");
    CHECK(wake_slice_ms(5000, 120, 30, false) == 30, "the next fade step");
    CHECK(wake_slice_ms(5000, -1, -1, true) == WAKE_SLICE_NP_MS, "now playing");
    CHECK(wake_slice_ms(5000, 0, -1, false) == WAKE_SLICE_MAX_MS, "due 0 is the caller's poll, not a cut");
}

int main(void)
{
    test_take_first();
    test_press_in_window();
    test_press_mid_wait();
    test_enqueue_during_drain();
    test_legacy_requeue();
    test_wait_fails();
    test_stale_wake();
    test_latency();
    test_slice();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("wake: all tests passed\n");
    return 0;
}
