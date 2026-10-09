#include <stdio.h>
#include <string.h>

#include "link_state.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_boot(void)
{
    link_state_t s;
    link_state_init(&s, 1000);
    CHECK(s.level == LINK_OFFLINE, "boot is offline until Ember answers");
    CHECK(s.offline_since_ms == 1000 && link_state_offline_ms(&s, 4000) == 3000, "offline since boot");
    CHECK(!link_state_note(&s, false, 2000) && s.level == LINK_OFFLINE && s.offline_since_ms == 1000,
          "a failure at boot keeps offline and its start");
    CHECK(link_state_note(&s, true, 5000) && s.level == LINK_ONLINE, "first answer: online");
    CHECK(link_state_offline_ms(&s, 6000) == 0, "online has no offline time");
}

static void test_transitions(void)
{
    link_state_t s;
    link_state_init(&s, 0);
    link_state_note(&s, true, 10);
    CHECK(s.level == LINK_ONLINE && s.fails == 0, "0 failures: online");
    CHECK(!link_state_note(&s, true, 20), "ok after ok: no change");

    CHECK(link_state_note(&s, false, 100) && s.level == LINK_DEGRADED && s.fails == 1, "1 failure: degraded");
    CHECK(link_state_offline_ms(&s, 150) == 0, "degraded has no offline time");
    CHECK(!link_state_note(&s, false, 200) && s.level == LINK_DEGRADED && s.fails == 2, "2 failures: degraded");
    CHECK(link_state_note(&s, false, 300) && s.level == LINK_OFFLINE && s.fails == 3, "3 failures: offline");
    CHECK(s.offline_since_ms == 100, "offline since the first failure of the streak, not the third");
    CHECK(link_state_offline_ms(&s, 1100) == 1000, "offline time from the first failure");
    CHECK(!link_state_note(&s, false, 400) && s.level == LINK_OFFLINE && s.offline_since_ms == 100,
          "more failures keep the start");

    CHECK(link_state_note(&s, true, 500) && s.level == LINK_ONLINE && s.fails == 0, "recovery: one answer is enough");

    link_state_note(&s, false, 600);
    link_state_note(&s, false, 700);
    CHECK(s.level == LINK_DEGRADED, "2 failures again");
    CHECK(link_state_note(&s, true, 800) && s.level == LINK_ONLINE, "recovery from degraded");
    link_state_note(&s, false, 900);
    CHECK(s.fails == 1 && s.level == LINK_DEGRADED, "the streak restarts after an answer");
    link_state_note(&s, false, 1000);
    link_state_note(&s, false, 1100);
    CHECK(s.level == LINK_OFFLINE && s.offline_since_ms == 900, "new streak, new start");
    CHECK(link_state_offline_ms(&s, 800) == 0, "a clock before the start gives 0");
}

static void test_names(void)
{
    CHECK(strcmp(link_level_name(LINK_ONLINE), "online") == 0 && strcmp(link_level_name(LINK_DEGRADED), "degraded") == 0 &&
              strcmp(link_level_name(LINK_OFFLINE), "offline") == 0 && strcmp(link_level_name((link_level_t)7), "offline") == 0,
          "names");
}

int main(void)
{
    test_boot();
    test_transitions();
    test_names();
    printf(failures ? "%d FAILED\n" : "all link tests passed\n", failures);
    return failures ? 1 : 0;
}
