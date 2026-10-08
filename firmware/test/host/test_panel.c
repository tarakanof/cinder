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
} fake_t;

static int fk_read(void *ctx, uint8_t *v)
{
    fake_t *f = ctx;
    if (++f->reads == f->fail_read_at) return -1;
    *v = f->stuck ? f->stuck_at : f->level;
    if (f->corrupt_from && f->reads >= f->corrupt_from) *v ^= 0x80;
    return 0;
}

static int fk_write(void *ctx, uint8_t v)
{
    fake_t *f = ctx;
    if (++f->writes == f->fail_write_at) return -1;
    if (f->writes <= 16) f->written[f->writes - 1] = v;
    f->level = v;
    return 0;
}

static panel_req_t P;
static fake_t F;
static pr_io_t IO = {fk_read, fk_write, &F};

static void reset(uint8_t level)
{
    pr_init(&P);
    memset(&F, 0, sizeof F);
    F.level = level;
}

static void frames(int64_t from, int64_t to)
{
    for (int64_t t = from; t <= to; t += 16) pr_frame(&P, &IO, t);
}

static void test_brightness(void)
{
    reset(100);
    pr_frame(&P, &IO, 0);
    CHECK(F.writes == 0, "nothing queued: no write");
    pr_brightness(&P, 40);
    pr_brightness(&P, 50);
    pr_frame(&P, &IO, 16);
    CHECK(F.writes == 1 && F.level == 50, "newest level wins, one write (%d writes, level %d)", F.writes, F.level);
    pr_frame(&P, &IO, 32);
    CHECK(F.writes == 1, "written once");
}

static void test_check_good(void)
{
    reset(100);
    int bad = 9;
    uint8_t raw[3];
    uint32_t t = pr_check_post(&P, 4);
    CHECK(!pr_check_result(&P, t, &bad, raw), "no result before a frame");
    pr_frame(&P, &IO, 0);
    CHECK(F.level == (100 ^ 1), "even seed writes level ^ 1 (%d)", F.level);
    pr_frame(&P, &IO, PR_SETTLE_MS - 1);
    CHECK(F.reads == 1, "waits %d ms before the read back", PR_SETTLE_MS);
    CHECK(!pr_check_result(&P, t, &bad, raw), "no result mid-check");
    pr_frame(&P, &IO, PR_SETTLE_MS);
    CHECK(F.level == 100, "second write restores the level");
    pr_frame(&P, &IO, 2 * PR_SETTLE_MS);
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
    pr_frame(&P, &IO, 0);
    pr_brightness(&P, 30);
    pr_frame(&P, &IO, 16);
    CHECK(F.level == (100 ^ 1), "no brightness write while the check runs");
    frames(32, 2 * PR_SETTLE_MS + 16);
    CHECK(pr_check_result(&P, t, &bad, NULL) && bad == 0, "check unaffected (%d)", bad);
    pr_frame(&P, &IO, 200);
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
    CHECK(pr_check_result(&P, t, &bad, raw) && bad == 0 && raw[0] == 30, "queued level written first, then checked");
    CHECK(F.level == 30, "level kept");
}

static void test_errors(void)
{
    reset(100);
    int bad;
    F.fail_read_at = 1;
    uint32_t t = pr_check_post(&P, 0);
    pr_frame(&P, &IO, 0);
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
    pr_frame(&P, &IO, 0);
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
    test_errors();
    test_tickets();
    if (failures) {
        printf("panel: %d failures\n", failures);
        return 1;
    }
    printf("panel: all tests passed\n");
    return 0;
}
