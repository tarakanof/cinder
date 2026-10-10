#include <stdio.h>
#include <string.h>

#include "link_state.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool note(link_state_t *s, link_outcome_t o, int64_t t) { return link_state_note(s, o, t); }

static void test_counts(void)
{
    static const struct {
        link_level_t level;
        int wifi, aborted, status, parsed;
        link_outcome_t want;
        const char *what;
    } C[] = {
        {LINK_ONLINE, 1, 0, 200, 1, LINK_OK, "200 parsed"},
        {LINK_ONLINE, 1, 0, 304, 1, LINK_OK, "304"},
        {LINK_ONLINE, 1, 0, 200, 0, LINK_FAIL, "200 the knob cannot parse: wrong host or a portal page"},
        {LINK_ONLINE, 1, 0, 301, 0, LINK_FAIL, "301: proxy or wrong host"},
        {LINK_ONLINE, 1, 0, 302, 0, LINK_FAIL, "302: captive portal"},
        {LINK_ONLINE, 1, 0, 307, 0, LINK_FAIL, "307"},
        {LINK_ONLINE, 1, 0, 399, 0, LINK_FAIL, "399"},
        {LINK_BOOT, 1, 0, 302, 0, LINK_FAIL, "captive portal at boot"},
        {LINK_BOOT, 1, 0, 200, 0, LINK_FAIL, "unparsed 200 at boot"},
        {LINK_BOOT, 1, 0, 401, 0, LINK_SKIP, "401 at boot: not paired, not offline"},
        {LINK_ONLINE, 1, 0, -1, 0, LINK_FAIL, "transport error (connect, DNS, timeout)"},
        {LINK_ONLINE, 1, 0, 500, 0, LINK_FAIL, "500"},
        {LINK_ONLINE, 1, 0, 502, 0, LINK_FAIL, "502"},
        {LINK_ONLINE, 1, 0, 599, 0, LINK_FAIL, "599"},
        {LINK_ONLINE, 1, 0, 401, 0, LINK_SKIP, "401: not paired, not offline"},
        {LINK_ONLINE, 1, 0, 403, 0, LINK_SKIP, "403: not paired, not offline"},
        {LINK_ONLINE, 1, 0, 404, 0, LINK_SKIP, "404: view fallback"},
        {LINK_ONLINE, 1, 0, 429, 0, LINK_SKIP, "429: back off"},
        {LINK_ONLINE, 1, 0, 400, 0, LINK_SKIP, "400"},
        {LINK_ONLINE, 1, 0, 600, 0, LINK_SKIP, "600"},
        {LINK_ONLINE, 1, 1, -1, 0, LINK_SKIP, "aborted long-poll"},
        {LINK_ONLINE, 1, 1, 200, 1, LINK_SKIP, "aborted wins"},
        {LINK_ONLINE, 0, 0, 0, 0, LINK_FAIL, "no Wi-Fi after the first answer"},
        {LINK_DEGRADED, 0, 0, 0, 0, LINK_FAIL, "no Wi-Fi while degraded"},
        {LINK_OFFLINE, 0, 0, 0, 0, LINK_FAIL, "no Wi-Fi while offline"},
        {LINK_BOOT, 0, 0, 0, 0, LINK_SKIP, "no Wi-Fi yet at boot: association takes seconds"},
        {LINK_BOOT, 1, 0, -1, 0, LINK_FAIL, "transport error at boot"},
        {LINK_BOOT, 1, 0, 200, 1, LINK_OK, "first answer at boot"},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++)
        CHECK(link_counts(C[i].level, C[i].wifi, C[i].aborted, C[i].status, C[i].parsed) == C[i].want, "%s", C[i].what);
}

static void test_boot(void)
{
    link_state_t s;
    link_state_init(&s, 1000);
    CHECK(s.level == LINK_BOOT && !link_word_offline(link_state_word(&s)), "boot is not offline");
    CHECK(link_state_word(&s) == 0, "boot word is zero (zero-initialised atomics agree)");
    CHECK(link_state_offline_ms(&s, 4000) == 0, "no offline time at boot");
    for (int i = 0; i < 10; i++) note(&s, link_counts(s.level, false, false, 0, false), 1100 + i * 2000);
    CHECK(s.level == LINK_BOOT, "passes without Wi-Fi at boot do not count");
    uint32_t press = link_state_word(&s);
    CHECK(!note(&s, LINK_FAIL, 30000) && s.level == LINK_BOOT, "1 failure at boot: still boot");
    CHECK(!note(&s, LINK_FAIL, 35000) && s.level == LINK_BOOT, "2 failures at boot: still boot");
    CHECK(link_press_ok(press, link_state_word(&s)), "a press at boot is sent while not offline");
    CHECK(note(&s, LINK_OK, 36000) && s.level == LINK_ONLINE, "first answer: online, no offline flash");

    link_state_init(&s, 0);
    note(&s, LINK_FAIL, 100);
    note(&s, LINK_FAIL, 200);
    CHECK(note(&s, LINK_FAIL, 300) && s.level == LINK_OFFLINE, "3 failures at boot: offline");
    CHECK(s.offline_since_ms == 100 && s.gen == 1, "since the first failure, generation bumped");
}

static void test_transitions(void)
{
    link_state_t s;
    link_state_init(&s, 0);
    note(&s, LINK_OK, 10);
    CHECK(s.level == LINK_ONLINE && s.fails == 0, "0 failures: online");
    CHECK(!note(&s, LINK_OK, 20), "ok after ok: no change");
    CHECK(!note(&s, LINK_SKIP, 25) && s.fails == 0, "skip changes nothing");

    CHECK(note(&s, LINK_FAIL, 100) && s.level == LINK_DEGRADED && s.fails == 1, "1 failure: degraded");
    CHECK(!note(&s, LINK_SKIP, 150) && s.fails == 1, "skip keeps the streak");
    CHECK(link_state_offline_ms(&s, 150) == 0, "degraded has no offline time");
    CHECK(!note(&s, LINK_FAIL, 200) && s.level == LINK_DEGRADED && s.fails == 2, "2 failures: degraded");
    CHECK(note(&s, LINK_FAIL, 300) && s.level == LINK_OFFLINE && s.fails == 3, "3 failures: offline");
    CHECK(s.offline_since_ms == 100, "offline since the first failure of the streak, not the third");
    CHECK(link_state_offline_ms(&s, 1100) == 1000, "offline time from the first failure");
    uint32_t gen = s.gen;
    CHECK(!note(&s, LINK_FAIL, 400) && s.level == LINK_OFFLINE && s.offline_since_ms == 100 && s.gen == gen,
          "more failures keep the start and the generation");

    CHECK(note(&s, LINK_OK, 500) && s.level == LINK_ONLINE && s.fails == 0, "recovery: one answer is enough");
    CHECK(s.gen == gen, "recovery does not bump the generation");

    note(&s, LINK_FAIL, 600);
    note(&s, LINK_FAIL, 700);
    CHECK(s.level == LINK_DEGRADED, "2 failures again");
    CHECK(note(&s, LINK_OK, 800) && s.level == LINK_ONLINE, "recovery from degraded");
    note(&s, LINK_FAIL, 900);
    CHECK(s.fails == 1 && s.level == LINK_DEGRADED, "the streak restarts after an answer");
    note(&s, LINK_FAIL, 1000);
    note(&s, LINK_FAIL, 1100);
    CHECK(s.level == LINK_OFFLINE && s.offline_since_ms == 900 && s.gen == gen + 1, "new streak, new start, new gen");
    CHECK(link_state_offline_ms(&s, 800) == 0, "a clock before the start gives 0");
}

typedef struct {
    uint32_t q[8];
    int head, n, sent, dropped;
} press_sim_t;

static void press(press_sim_t *p, const link_state_t *s)
{
    uint32_t w = link_state_word(s);
    if (link_word_offline(w)) {
        p->dropped++;
        return;
    }
    p->q[(p->head + p->n++) % 8] = w;
}

static void send_one(press_sim_t *p, const link_state_t *s)
{
    if (!p->n) return;
    uint32_t at = p->q[p->head];
    p->head = (p->head + 1) % 8;
    p->n--;
    if (link_press_ok(at, link_state_word(s))) p->sent++;
    else p->dropped++;
}

static void test_no_replay(void)
{
    link_state_t s;
    press_sim_t p = {0};
    link_state_init(&s, 0);
    note(&s, LINK_OK, 1);
    note(&s, LINK_FAIL, 10);
    CHECK(s.level == LINK_DEGRADED, "degraded");
    press(&p, &s);
    press(&p, &s);
    press(&p, &s);
    CHECK(p.n == 3 && p.dropped == 0, "presses while degraded are queued");
    note(&s, LINK_FAIL, 20);
    note(&s, LINK_FAIL, 30);
    CHECK(s.level == LINK_OFFLINE, "the in-flight request fails: offline");
    send_one(&p, &s);
    send_one(&p, &s);
    CHECK(p.sent == 0 && p.dropped == 2, "two presses dropped while offline (wait, then the next poll)");
    note(&s, LINK_OK, 40);
    CHECK(s.level == LINK_ONLINE, "the next GET succeeds: online");
    CHECK(!link_word_offline(link_state_word(&s)),
          "a rule that drops only while offline would send the last press here: the replay was real");
    send_one(&p, &s);
    CHECK(p.sent == 0 && p.dropped == 3, "the last press from before the outage is not sent after the reconnect");

    press(&p, &s);
    send_one(&p, &s);
    CHECK(p.sent == 1, "a press after the reconnect is sent");

    press_sim_t q = {0};
    note(&s, LINK_FAIL, 50);
    note(&s, LINK_FAIL, 60);
    note(&s, LINK_FAIL, 70);
    press(&q, &s);
    CHECK(q.dropped == 1 && q.n == 0, "a press while offline is dropped at once");
    note(&s, LINK_OK, 80);
    CHECK(q.sent == 0, "and never sent later");

    press_sim_t r = {0};
    press(&r, &s);
    note(&s, LINK_FAIL, 90);
    note(&s, LINK_FAIL, 91);
    note(&s, LINK_FAIL, 92);
    note(&s, LINK_OK, 93);
    send_one(&r, &s);
    CHECK(r.sent == 0, "an outage between a press and its send drops it, even when the link is back");
}

static void test_wrong_host(void)
{
    static const struct {
        int status, parsed;
        bool offline;
        const char *what;
    } C[] = {
        {302, 0, true, "captive portal"},
        {301, 0, true, "proxy redirect"},
        {200, 0, true, "HTML 200 from the wrong host"},
        {401, 0, false, "401: not paired"},
        {403, 0, false, "403: not paired"},
        {404, 0, false, "404: documented, not counted"},
        {429, 0, false, "429"},
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        link_state_t s;
        link_state_init(&s, 0);
        for (int n = 0; n < 5; n++) note(&s, link_counts(s.level, true, false, C[i].status, C[i].parsed), 100 + n);
        CHECK((s.level == LINK_OFFLINE) == C[i].offline, "boot, only %s: %s", C[i].what, link_level_name(s.level));
        link_state_init(&s, 0);
        note(&s, LINK_OK, 50);
        for (int n = 0; n < 5; n++) note(&s, link_counts(s.level, true, false, C[i].status, C[i].parsed), 100 + n);
        CHECK((s.level == LINK_OFFLINE) == C[i].offline, "online, then only %s: %s", C[i].what,
              link_level_name(s.level));
    }
}

static void test_names(void)
{
    CHECK(strcmp(link_level_name(LINK_BOOT), "boot") == 0 && strcmp(link_level_name(LINK_ONLINE), "online") == 0 &&
              strcmp(link_level_name(LINK_DEGRADED), "degraded") == 0 &&
              strcmp(link_level_name(LINK_OFFLINE), "offline") == 0 &&
              strcmp(link_level_name((link_level_t)7), "offline") == 0,
          "names");
}

int main(void)
{
    test_counts();
    test_boot();
    test_transitions();
    test_no_replay();
    test_wrong_host();
    test_names();
    printf(failures ? "%d FAILED\n" : "all link tests passed\n", failures);
    return failures ? 1 : 0;
}
