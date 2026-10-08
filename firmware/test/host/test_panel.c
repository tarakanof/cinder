#include <stdio.h>
#include <string.h>

#include "panel_req.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct {
    uint8_t level;
    int writes, reads;
    uint8_t written[16];
    bool stuck;
    uint8_t stuck_at;
    int fail_read_at, fail_write_at, corrupt_from;
    int lag_ms, write_ms, fail_write_until;
    uint8_t prev;
    int64_t wrote_at;
} fake_t;

static int64_t g_now;

static int fk_read(void *ctx, uint8_t *v)
{
    fake_t *f = ctx;
    if (++f->reads == f->fail_read_at) return -1;
    *v = f->stuck ? f->stuck_at : f->lag_ms && g_now - f->wrote_at < f->lag_ms ? f->prev : f->level;
    if (f->corrupt_from && f->reads >= f->corrupt_from) *v ^= 0x80;
    return 0;
}

static int fk_write(void *ctx, uint8_t v)
{
    fake_t *f = ctx;
    ++f->writes;
    g_now += f->write_ms;
    if (f->writes == f->fail_write_at || (f->writes > f->fail_write_at && f->writes <= f->fail_write_until)) return -1;
    if (f->writes <= 16) f->written[f->writes - 1] = v;
    f->prev = f->level;
    f->wrote_at = g_now;
    f->level = v;
    return 0;
}

static panel_req_t P;
static fake_t F;
static int64_t fk_now(void *ctx)
{
    (void)ctx;
    return g_now;
}

static pr_io_t IO = {fk_read, fk_write, fk_now, &F};

static void reset(uint8_t level)
{
    pr_init(&P);
    memset(&F, 0, sizeof F);
    F.level = F.prev = level;
    F.wrote_at = -1000;
}

static void frame(int64_t t)
{
    g_now = t;
    pr_frame(&P, &IO, t);
}

static void frames(int64_t from, int64_t to)
{
    for (int64_t t = from; t <= to; t += 16) frame(t);
}

static void test_brightness(void)
{
    reset(100);
    frame(0);
    CHECK(F.writes == 0, "nothing queued: no write");
    pr_brightness(&P, 40);
    pr_brightness(&P, 50);
    frame(16);
    CHECK(F.writes == 1 && F.level == 50, "newest level wins, one write (%d writes, level %d)", F.writes, F.level);
    frame(32);
    CHECK(F.writes == 1, "written once");
}

static void test_check_good(void)
{
    reset(100);
    int bad = 9;
    uint8_t raw[3];
    uint32_t t = pr_check_post(&P, 4);
    CHECK(!pr_check_result(&P, t, &bad, raw), "no result before a frame");
    frame(0);
    CHECK(F.level == (100 ^ 1), "even seed writes level ^ 1 (%d)", F.level);
    frame(PR_SETTLE_MS - 1);
    CHECK(F.reads == 1, "waits %d ms before the read back", PR_SETTLE_MS);
    CHECK(!pr_check_result(&P, t, &bad, raw), "no result mid-check");
    frame(PR_SETTLE_MS);
    CHECK(F.level == 100, "second write restores the level");
    frame(2 * PR_SETTLE_MS);
    CHECK(pr_check_result(&P, t, &bad, raw) && bad == 0, "good loopback: 0 bad (%d)", bad);
    CHECK(raw[0] == 100 && raw[1] == (100 ^ 1) && raw[2] == 100, "raw %d %d %d", raw[0], raw[1], raw[2]);
    CHECK(F.writes == 2 && F.level == 100, "two writes, level restored");
    t = pr_check_post(&P, 5);
    frames(200, 400);
    CHECK(pr_check_result(&P, t, &bad, raw) && bad == 0 && raw[1] == (100 ^ 2), "odd seed writes level ^ 2");
}

static void test_check_mismatch(void)
{
    reset(100);
    F.stuck = true;
    F.stuck_at = 100;
    int bad;
    uint32_t t = pr_check_post(&P, 0);
    frames(0, 300);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == 1, "read back stuck at the old level: 1 bad (%d)", bad);
    reset(100);
    F.corrupt_from = 2;
    t = pr_check_post(&P, 0);
    frames(0, 300);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == 2, "both read backs corrupt: 2 bad (%d)", bad);
}

static void test_brightness_waits_for_check(void)
{
    reset(100);
    int bad;
    uint32_t t = pr_check_post(&P, 0);
    frame(0);
    pr_brightness(&P, 30);
    frame(16);
    CHECK(F.level == (100 ^ 1), "no brightness write while the check runs");
    frames(32, 2 * PR_SETTLE_MS + 16);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == 0, "check unaffected (%d)", bad);
    frame(200);
    CHECK(F.level == 30, "brightness written after the check (%d)", F.level);
}

static void test_brightness_before_check(void)
{
    reset(100);
    int bad;
    uint8_t raw[3];
    pr_brightness(&P, 30);
    uint32_t t = pr_check_post(&P, 0);
    frames(0, 300);
    CHECK(pr_check_result(&P, t, &bad, raw) && bad == 0, "check passes (%d)", bad);
    CHECK(F.level == 30, "queued level kept (%d)", F.level);
}

static void test_readback_lag(void)
{
    int bad;
    uint8_t raw[3];
    reset(100);
    F.lag_ms = PR_SETTLE_MS - 1;
    pr_brightness(&P, 30);
    uint32_t t = pr_check_post(&P, 0);
    frames(0, 400);
    CHECK(pr_check_result(&P, t, &bad, raw) && bad == 0, "lagging RDDISBV, level and check in one frame: passes (%d)", bad);
    CHECK(F.level == 30, "the check never restores a level read before it settled (%d)", F.level);

    reset(100);
    F.lag_ms = PR_SETTLE_MS - 1;
    pr_brightness(&P, 30);
    frame(0);
    t = pr_check_post(&P, 0);
    frames(16, 400);
    CHECK(pr_check_result(&P, t, &bad, raw) && bad == 0 && raw[0] == 30, "check posted just after a write reads the new level (%d)", raw[0]);
    CHECK(F.level == 30, "level kept (%d)", F.level);

    reset(100);
    F.lag_ms = PR_SETTLE_MS - 1;
    t = pr_check_post(&P, 1);
    for (int64_t ms = 0; ms <= 1000; ms += 16) {
        if (ms % 96 == 0) pr_brightness(&P, (uint8_t)(100 + ms / 96));
        frame(ms);
    }
    frames(1016, 1400);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == 0, "a fade every 96 ms does not starve the check (%d)", bad);
    CHECK(F.level == 110, "fade ends at its last level (%d)", F.level);
}

static void test_slow_write(void)
{
    int bad;
    reset(100);
    F.lag_ms = PR_SETTLE_MS - 1;
    F.write_ms = 20;
    uint32_t t = pr_check_post(&P, 0);
    frames(0, 400);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == 0, "a write that blocks 20 ms: settle counts from its end (%d)", bad);
    CHECK(F.level == 100, "level restored (%d)", F.level);
}

static void test_restore_fails(void)
{
    int bad;
    reset(100);
    F.fail_write_at = 2;
    F.fail_write_until = 3;
    uint32_t t = pr_check_post(&P, 0);
    frames(0, 300);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == -1, "restore and its retry fail: -1");
    frames(316, 600);
    CHECK(F.level == 100, "the old level is queued and written later (%d)", F.level);

    reset(100);
    F.fail_write_at = 2;
    F.fail_write_until = 3;
    t = pr_check_post(&P, 0);
    frames(0, 32);
    pr_brightness(&P, 50);
    frames(48, 600);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == -1, "restore fails with a newer level pending: -1");
    CHECK(F.level == 50, "a level posted mid-check wins over the queued restore (%d)", F.level);
}

static void test_brightness_write_fails(void)
{
    reset(100);
    F.fail_write_at = 1;
    pr_brightness(&P, 30);
    frame(0);
    CHECK(F.level == 100, "write failed");
    frames(16, 300);
    CHECK(F.level == 30 && F.writes == 2, "failed level retried (%d writes, level %d)", F.writes, F.level);

    reset(100);
    F.fail_write_at = 1;
    pr_brightness(&P, 30);
    frame(0);
    pr_brightness(&P, 40);
    frames(16, 300);
    CHECK(F.level == 40 && F.writes == 2, "a newer level wins over the retry (%d writes, level %d)", F.writes, F.level);
}

static void test_static_init(void)
{
    static panel_req_t q = PR_INIT;
    pr_brightness(&q, 77);
    memset(&F, 0, sizeof F);
    F.level = 5;
    g_now = 1000;
    pr_frame(&q, &IO, 1000);
    CHECK(F.level == 77, "a level queued before the owner starts is written (%d)", F.level);
    pr_frame(&q, &IO, 1016);
    CHECK(F.writes == 1, "no check without a ticket");
}

static void test_errors(void)
{
    reset(100);
    int bad;
    F.fail_read_at = 1;
    uint32_t t = pr_check_post(&P, 0);
    frame(0);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == -1, "first read fails: -1");
    CHECK(F.writes == 0, "nothing written");

    reset(100);
    F.fail_read_at = 2;
    t = pr_check_post(&P, 0);
    frames(0, 300);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == -1, "read back fails: -1");
    CHECK(F.level == 100, "test level undone (%d)", F.level);

    reset(100);
    F.fail_write_at = 2;
    t = pr_check_post(&P, 0);
    frames(0, 300);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == -1, "restore write fails: -1");
    CHECK(F.writes == 3 && F.level == 100, "restore retried once (%d writes, level %d)", F.writes, F.level);
}

static void test_tickets(void)
{
    reset(100);
    int bad;
    uint32_t a = pr_check_post(&P, 0);
    uint32_t b = pr_check_post(&P, 1);
    frames(0, 300);
    CHECK(!pr_check_result(&P, a, &bad, NULL), "replaced ticket never answers");
    CHECK(pr_check_result(&P, b, &bad, NULL) && bad == 0, "newest ticket answers");
    CHECK(F.writes == 2, "one check ran (%d writes)", F.writes);
    frames(400, 600);
    CHECK(F.writes == 2, "a finished ticket does not run again");

    reset(100);
    a = pr_check_post(&P, 0);
    frame(0);
    b = pr_check_post(&P, 0);
    frames(16, 2 * PR_SETTLE_MS + 16);
    CHECK(pr_check_result(&P, a, &bad, NULL) && !pr_check_result(&P, b, &bad, NULL), "a running check finishes first");
    frames(200, 400);
    CHECK(pr_check_result(&P, b, &bad, NULL) && bad == 0, "then the next ticket runs");
}

int main(void)
{
    test_brightness();
    test_check_good();
    test_check_mismatch();
    test_brightness_waits_for_check();
    test_brightness_before_check();
    test_readback_lag();
    test_brightness_write_fails();
    test_slow_write();
    test_restore_fails();
    test_static_init();
    test_errors();
    test_tickets();
    if (failures) {
        printf("panel: %d failures\n", failures);
        return 1;
    }
    printf("panel: all tests passed\n");
    return 0;
}
