#include <stdio.h>
#include <string.h>

#include "knob_rotation.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define VIS_X1 (KR_FRAME_W - 1)
#define VIS_Y1 (KR_FRAME_H - 1)

static void test_supported(void)
{
    CHECK(KR_N_SUPPORTED == 2 && KR_SUPPORTED[0] == 0 && KR_SUPPORTED[1] == 180, "supported [0,180]");
    CHECK(kr_effective(0) == 0 && kr_effective(180) == 180, "supported kept");
    const int other[] = {90, 270, 45, -180, 360, 1, -1};
    for (size_t i = 0; i < sizeof other / sizeof other[0]; i++)
        CHECK(kr_effective(other[i]) == 0, "%d falls back to 0", other[i]);
    CHECK(kr_valid(0) && kr_valid(90) && kr_valid(180) && kr_valid(270), "protocol values");
    CHECK(!kr_valid(45) && !kr_valid(360) && !kr_valid(-90), "non-protocol values");
}

static void test_panel(void)
{
    kr_panel_t p = kr_panel(0);
    CHECK(!p.mirror_x && !p.mirror_y && p.gap_x == 0 && p.gap_y == 0, "0: as before");
    p = kr_panel(90);
    CHECK(!p.mirror_x && !p.mirror_y && p.gap_x == 0 && p.gap_y == 0, "90 unsupported: as 0");
    p = kr_panel(180);
    CHECK(p.mirror_x && p.mirror_y && p.gap_x == 2 && p.gap_y == 14, "180: mirror, gap 2/14: %d/%d", p.gap_x, p.gap_y);
    CHECK(p.gap_x % 2 == 0 && p.gap_y % 2 == 0, "even gaps keep the 2 px windows");
}

static int gram_x_in(int deg, int x, int w)
{
    kr_panel_t p = kr_panel_in(deg, w, KR_MIRROR_H);
    return p.mirror_x ? w - 1 - (x + p.gap_x) : x + p.gap_x;
}

static int gram_y_in(int deg, int y, int h)
{
    kr_panel_t p = kr_panel_in(deg, KR_MIRROR_W, h);
    return p.mirror_y ? h - 1 - (y + p.gap_y) : y + p.gap_y;
}

static int gram_x(int deg, int x) { return gram_x_in(deg, x, KR_MIRROR_W); }
static int gram_y(int deg, int y) { return gram_y_in(deg, y, KR_MIRROR_H); }

static void coverage(int deg, int w, int h)
{
    kr_panel_t p = kr_panel_in(deg, w, h);
    int x1 = 0, y1 = 0, x2 = KR_FRAME_W - 1, y2 = KR_FRAME_H - 1;
    kr_round(&p, &x1, &y1, &x2, &y2);
    int hit_x[KR_MIRROR_W] = {0}, hit_y[KR_MIRROR_H] = {0}, bad = 0;
    for (int x = x1; x <= x2; x++) {
        bad += x + p.gap_x < 0;
        int g = gram_x_in(deg, x, w);
        if (g >= 0 && g < w) hit_x[g]++;
        else bad++;
    }
    for (int y = y1; y <= y2; y++) {
        bad += y + p.gap_y < 0;
        int g = gram_y_in(deg, y, h);
        if (g >= 0 && g < h) hit_y[g]++;
        else bad++;
    }
    CHECK(bad == 0, "%d in %dx%d: every window address inside the mirror window, none negative: %d bad", deg, w, h, bad);
    int miss = 0;
    for (int g = KR_VIS_X0; g <= VIS_X1; g++) miss += hit_x[g] != 1;
    for (int g = 0; g <= VIS_Y1; g++) miss += hit_y[g] != 1;
    CHECK(miss == 0, "%d in %dx%d: every visible column and row written once (no green line): %d missed", deg, w, h, miss);
}

static void test_coverage(void)
{
    coverage(0, KR_MIRROR_W, KR_MIRROR_H);
    coverage(180, KR_MIRROR_W, KR_MIRROR_H);
    for (int w = KR_FRAME_W; w <= KR_MIRROR_W; w += 2)
        for (int h = KR_FRAME_H; h <= KR_MIRROR_H; h += 2) coverage(180, w, h);
    for (int x = 0; x < KR_FRAME_W; x += 2) {
        int a = gram_x(180, x), b = gram_x(180, x + 1);
        CHECK(b % 2 == 0 && a == b + 1, "180: even/odd window %d-%d stays a 2 px pair in GRAM (%d-%d)", x, x + 1, b, a);
    }
    for (int y = 0; y < KR_FRAME_H; y += 2) {
        int a = gram_y(180, y), b = gram_y(180, y + 1);
        CHECK(b % 2 == 0 && a == b + 1, "180: rows %d-%d stay a 2 px pair in GRAM (%d-%d)", y, y + 1, b, a);
    }
}

static void test_round(void)
{
    kr_panel_t p0 = kr_panel(0), p180 = kr_panel(180), narrow = kr_panel_in(180, 472, 466);
    int x1 = -3, y1 = 7, x2 = 500, y2 = 470;
    kr_round(&p0, &x1, &y1, &x2, &y2);
    CHECK(x1 == 0 && y1 == 6 && x2 == 471 && y2 == 465, "0: even start, odd end, inside the frame: %d,%d-%d,%d", x1, y1, x2, y2);
    x1 = 3, y1 = 3, x2 = 10, y2 = 10;
    kr_round(&p180, &x1, &y1, &x2, &y2);
    CHECK(x1 == 2 && y1 == 2 && x2 == 11 && y2 == 11, "180 in 480: same windows as 0");
    CHECK(narrow.gap_x == -6 && narrow.min_x == 6 && narrow.gap_y == 0 && narrow.min_y == 0, "472x466 window: gap -6, start 6");
    x1 = 0, y1 = 0, x2 = 9, y2 = 9;
    kr_round(&narrow, &x1, &y1, &x2, &y2);
    CHECK(x1 == 6 && x2 == 9, "negative gap: window starts at 6: %d-%d", x1, x2);
    x1 = 0, y1 = 0, x2 = 3, y2 = 1;
    kr_round(&narrow, &x1, &y1, &x2, &y2);
    CHECK(x1 == 6 && x2 == 7, "area left of the start becomes one valid 2 px window: %d-%d", x1, x2);
}

static void test_image_rotated(void)
{
    for (int x = KR_VIS_X0; x <= VIS_X1; x++)
        CHECK(gram_x(180, x) == KR_VIS_X0 + VIS_X1 - x, "180: column %d lands mirrored about the glass centre", x);
    for (int y = 0; y <= VIS_Y1; y++) CHECK(gram_y(180, y) == VIS_Y1 - y, "180: row %d mirrored", y);
}

static void test_touch(void)
{
    int x = 100, y = 50;
    kr_touch(0, &x, &y);
    CHECK(x == 100 && y == 50, "0: unchanged");
    x = 100, y = 50;
    kr_touch(90, &x, &y);
    CHECK(x == 100 && y == 50, "90 unsupported: unchanged");
    for (int tx = KR_VIS_X0; tx <= VIS_X1; tx += 5) {
        for (int ty = 0; ty <= VIS_Y1; ty += 5) {
            int px = tx, py = ty;
            kr_touch(180, &px, &py);
            CHECK(gram_x(180, px) == tx && gram_y(180, py) == ty,
                  "180: finger on GRAM %d,%d hits the pixel drawn there (%d,%d)", tx, ty, px, py);
        }
    }
    x = 236, y = 233;
    kr_touch(180, &x, &y);
    CHECK(x == 241 && y == 232, "180: centre %d,%d", x, y);
    x = 0, y = 0;
    kr_touch(180, &x, &y);
    CHECK(x == KR_FRAME_W - 1 && y == KR_FRAME_H - 1, "180: corner clamps into the frame: %d,%d", x, y);
    x = 600, y = -5;
    kr_touch(0, &x, &y);
    CHECK(x == KR_FRAME_W - 1 && y == 0, "0: out of range clamps: %d,%d", x, y);
}

typedef struct {
    int calls, degs[16];
    int fail[16];
} fake_t;

static int fake_orient(void *ctx, int deg)
{
    fake_t *f = ctx;
    int n = f->calls++;
    if (n < 16) f->degs[n] = deg;
    return n < 16 && f->fail[n] ? -1 : 0;
}

static void test_state_basics(void)
{
    kr_state_t s;
    kr_state_init(&s);
    fake_t f = {0};
    CHECK(s.want == 0 && s.shown == 0 && s.touch == 0, "boots at 0");
    CHECK(kr_step(&s, 0, fake_orient, &f) == KR_IDLE && f.calls == 0, "same value: no panel write");
    kr_want(&s, 90);
    CHECK(s.want == 0 && kr_step(&s, 0, fake_orient, &f) == KR_IDLE && f.calls == 0, "unsupported wish: 0, nothing");
    kr_want(&s, 180);
    CHECK(kr_step(&s, 5, fake_orient, &f) == KR_APPLIED && f.calls == 1 && f.degs[0] == 180, "applied once");
    CHECK(s.shown == 180 && s.touch == 180, "touch follows the panel");
    CHECK(kr_step(&s, 6, fake_orient, &f) == KR_IDLE && f.calls == 1, "applied: idle");
    kr_want(&s, 0);
    CHECK(kr_step(&s, 7, fake_orient, &f) == KR_APPLIED && s.touch == 0 && f.degs[1] == 0, "back to 0");
}

static void test_failed_apply_retried(void)
{
    kr_state_t s;
    kr_state_init(&s);
    fake_t f = {.fail = {1, 0, 1, 0, 1, 0}};
    kr_want(&s, 180);
    CHECK(kr_step(&s, 1000, fake_orient, &f) == KR_ROLLED_BACK, "first try fails, rollback ok");
    CHECK(f.calls == 2 && f.degs[0] == 180 && f.degs[1] == 0, "rollback writes the old orientation");
    CHECK(s.shown == 0 && s.touch == 0 && s.want == 180, "panel and touch still 0, wish kept");
    CHECK(kr_step(&s, 1099, fake_orient, &f) == KR_IDLE && f.calls == 2, "waits the backoff");
    CHECK(kr_step(&s, 1100, fake_orient, &f) == KR_ROLLED_BACK && f.calls == 4, "retried after 100 ms with no new wish");
    CHECK(kr_step(&s, 1299, fake_orient, &f) == KR_IDLE, "backoff doubled to 200 ms");
    CHECK(kr_step(&s, 1300, fake_orient, &f) == KR_ROLLED_BACK && f.calls == 6, "third try");
    CHECK(kr_step(&s, 1700, fake_orient, &f) == KR_APPLIED && s.shown == 180 && s.touch == 180, "retry succeeds");
    CHECK(s.backoff_ms == KR_RETRY_MIN_MS && kr_step(&s, 1701, fake_orient, &f) == KR_IDLE, "backoff reset, idle");

    kr_state_init(&s);
    fake_t g = {0};
    for (int i = 0; i < 16; i++) g.fail[i] = 1;
    kr_want(&s, 180);
    int64_t now = 0;
    for (int i = 0; i < 7; i++) {
        kr_step(&s, now, fake_orient, &g);
        now = s.retry_ms;
    }
    CHECK(s.backoff_ms == KR_RETRY_MAX_MS, "backoff capped at %d: %d", KR_RETRY_MAX_MS, s.backoff_ms);
    CHECK(s.shown == KR_UNKNOWN, "every write failing: unknown");
    kr_want(&s, 0);
    int calls = g.calls;
    kr_step(&s, 0, fake_orient, &g);
    CHECK(g.calls == calls + 1 && g.degs[calls] == 0, "a new wish skips the old backoff");
}

static void test_failed_rollback(void)
{
    kr_state_t s;
    kr_state_init(&s);
    fake_t f = {.fail = {1, 1, 1, 0}};
    kr_want(&s, 180);
    CHECK(kr_step(&s, 0, fake_orient, &f) == KR_LOST, "apply and rollback fail: lost");
    CHECK(s.shown == KR_UNKNOWN && s.touch == 0, "orientation unknown, touch keeps the last written (0)");
    CHECK(kr_step(&s, 50, fake_orient, &f) == KR_IDLE && f.calls == 2, "waits the backoff");
    CHECK(kr_step(&s, 100, fake_orient, &f) == KR_LOST && f.calls == 3 && f.degs[2] == 180,
          "retries the wish, no rollback to an unknown state");
    CHECK(kr_step(&s, 300, fake_orient, &f) == KR_APPLIED && s.shown == 180 && s.touch == 180, "recovers");

    kr_state_init(&s);
    fake_t g = {.fail = {1, 1}};
    kr_want(&s, 180);
    kr_step(&s, 0, fake_orient, &g);
    kr_want(&s, 0);
    CHECK(kr_step(&s, 0, fake_orient, &g) == KR_APPLIED && g.degs[2] == 0 && s.shown == 0,
          "unknown and the wish goes back to 0: 0 is written, not assumed");
}

int main(void)
{
    test_supported();
    test_panel();
    test_coverage();
    test_round();
    test_image_rotated();
    test_touch();
    test_state_basics();
    test_failed_apply_retried();
    test_failed_rollback();
    if (failures) {
        printf("rotation: %d failure(s)\n", failures);
        return 1;
    }
    printf("rotation: all tests passed\n");
    return 0;
}
