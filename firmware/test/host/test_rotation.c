#include <stdio.h>
#include <string.h>

#include "knob_rotation.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define VIS_X1 (KR_FRAME_W - 1)
#define VIS_Y1 (KR_FRAME_H - 1)
#define GRAM 480
#define PANEL_ROWS 466
#define STRIP_ROWS 60

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
    CHECK(p.mirror_x && p.mirror_y && p.gap_x == 8 && p.gap_y == 0, "180: mirror, gap 8/0: %d/%d", p.gap_x, p.gap_y);
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
    int hit_x[GRAM] = {0}, hit_y[GRAM] = {0}, bad = 0;
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
    int moved = 0;
    for (int x = 0; deg == 180 && x < KR_FRAME_W; x++) moved += gram_x_in(deg, x, w) != KR_FRAME_W - 1 - x;
    for (int y = 0; deg == 180 && y < KR_FRAME_H; y++) moved += gram_y_in(deg, y, h) != KR_FRAME_H - 1 - y;
    CHECK(moved == 0, "%d in %dx%d: the frame mirrors in place whatever the window: %d moved", deg, w, h, moved);
}

static void test_coverage(void)
{
    coverage(0, KR_MIRROR_W, KR_MIRROR_H);
    coverage(180, KR_MIRROR_W, KR_MIRROR_H);
    for (int w = KR_FRAME_W; w <= GRAM; w += 2)
        for (int h = KR_FRAME_H; h <= GRAM; h += 2) coverage(180, w, h);
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
    kr_panel_t p0 = kr_panel(0), p180 = kr_panel(180), frame = kr_panel_in(180, 472, 466), narrow = kr_panel_in(180, 466, 466);
    int x1 = -3, y1 = 7, x2 = 500, y2 = 470;
    kr_round(&p0, &x1, &y1, &x2, &y2);
    CHECK(x1 == 0 && y1 == 6 && x2 == 471 && y2 == 465, "0: even start, odd end, inside the frame: %d,%d-%d,%d", x1, y1, x2, y2);
    x1 = 3, y1 = 3, x2 = 10, y2 = 10;
    kr_round(&p180, &x1, &y1, &x2, &y2);
    CHECK(x1 == 2 && y1 == 2 && x2 == 11 && y2 == 11, "180 in 480: same windows as 0");
    CHECK(frame.gap_x == 0 && frame.min_x == 0 && frame.gap_y == 0 && frame.min_y == 0, "472x466 window: gap 0, start 0");
    CHECK(narrow.gap_x == -6 && narrow.min_x == 6 && narrow.gap_y == 0 && narrow.min_y == 0, "466x466 window: gap -6, start 6");
    x1 = 0, y1 = 0, x2 = 9, y2 = 9;
    kr_round(&narrow, &x1, &y1, &x2, &y2);
    CHECK(x1 == 6 && x2 == 9, "negative gap: window starts at 6: %d-%d", x1, x2);
    x1 = 0, y1 = 0, x2 = 3, y2 = 1;
    kr_round(&narrow, &x1, &y1, &x2, &y2);
    CHECK(x1 == 6 && x2 == 7, "area left of the start becomes one valid 2 px window: %d-%d", x1, x2);
}

static void test_image_in_place(void)
{
    for (int x = 0; x < KR_FRAME_W; x++)
        CHECK(gram_x(0, x) + gram_x(180, x) == KR_FRAME_W - 1,
              "180: column %d mirrored about the frame centre, not the glass centre (%d at 0, %d at 180)", x, gram_x(0, x),
              gram_x(180, x));
    for (int y = 0; y < KR_FRAME_H; y++)
        CHECK(gram_y(0, y) + gram_y(180, y) == KR_FRAME_H - 1, "180: row %d mirrored about the frame centre", y);
    int cx = KR_FRAME_W / 2 - 1, cy = KR_FRAME_H / 2 - 1;
    CHECK(gram_x(180, cx) == gram_x(0, cx + 1) && gram_x(180, cx + 1) == gram_x(0, cx),
          "180: the frame's centre columns %d-%d stay on GRAM %d-%d (no horizontal jump): %d-%d", cx, cx + 1,
          gram_x(0, cx), gram_x(0, cx + 1), gram_x(180, cx + 1), gram_x(180, cx));
    CHECK(gram_y(180, cy) == gram_y(0, cy + 1) && gram_y(180, cy + 1) == gram_y(0, cy),
          "180: the frame's centre rows stay on their GRAM rows");
    int r = 150, lo0 = GRAM, hi0 = -1, lo180 = GRAM, hi180 = -1;
    for (int x = cx - r; x <= cx + 1 + r; x++) {
        int a = gram_x(0, x), b = gram_x(180, x);
        lo0 = a < lo0 ? a : lo0, hi0 = a > hi0 ? a : hi0;
        lo180 = b < lo180 ? b : lo180, hi180 = b > hi180 ? b : hi180;
    }
    CHECK(lo0 == lo180 && hi0 == hi180, "a centred ring covers GRAM %d..%d at 0 and %d..%d at 180", lo0, hi0, lo180, hi180);
}


static int s_gram[GRAM][GRAM];
static int s_off_panel;

static int frame_id(int x, int y) { return 1 + y * KR_FRAME_W + x; }

static void panel_write(int deg, int x1, int y1, int x2, int y2)
{
    kr_panel_t p = kr_panel(deg);
    for (int y = y1; y <= y2; y++) {
        int r = y + p.gap_y;
        int gy = p.mirror_y ? PANEL_ROWS - 1 - r : r;
        for (int x = x1; x <= x2; x++) {
            int c = x + p.gap_x;
            int gx = p.mirror_x ? GRAM - 1 - c : c;
            if (r < 0 || r >= PANEL_ROWS || c < 0 || c >= GRAM || gy < 0 || gx < 0) {
                s_off_panel++;
                continue;
            }
            s_gram[gy][gx] = frame_id(x, y);
        }
    }
}

static void flush_area(int deg, int x1, int y1, int x2, int y2)
{
    kr_panel_t p = kr_panel(deg);
    kr_round(&p, &x1, &y1, &x2, &y2);
    for (int y = y1; y <= y2; y += STRIP_ROWS)
        panel_write(deg, x1, y, x2, y + STRIP_ROWS - 1 > y2 ? y2 : y + STRIP_ROWS - 1);
}

static int glass_errors(int deg, int x1, int y1, int x2, int y2, int *unwritten)
{
    int wrong = 0;
    *unwritten = 0;
    for (int gy = 0; gy <= VIS_Y1; gy++)
        for (int gx = KR_VIS_X0; gx <= VIS_X1; gx++) {
            int u = deg == 180 ? VIS_X1 - gx : gx, v = deg == 180 ? VIS_Y1 - gy : gy;
            if (u < x1 || u > x2 || v < y1 || v > y2) continue;
            if (!s_gram[gy][gx]) (*unwritten)++;
            else if (s_gram[gy][gx] != frame_id(u, v)) wrong++;
        }
    return wrong;
}

static void test_panel_frame(int deg)
{
    memset(s_gram, 0, sizeof s_gram);
    s_off_panel = 0;
    flush_area(deg, 0, 0, KR_FRAME_W - 1, KR_FRAME_H - 1);
    int unwritten, wrong = glass_errors(deg, 0, 0, VIS_X1, VIS_Y1, &unwritten);
    CHECK(s_off_panel == 0, "%d: a full refresh in %d-row strips stays inside the panel's %d rows: %d pixels past it",
          deg, STRIP_ROWS, PANEL_ROWS, s_off_panel);
    CHECK(unwritten == 0, "%d: a full refresh writes every visible pixel: %d left stale", deg, unwritten);
    CHECK(wrong == 0, "%d: every visible pixel shows its own frame pixel (centred, upright): %d wrong", deg, wrong);
    CHECK(s_gram[233][236] == frame_id(deg == 180 ? 235 : 236, deg == 180 ? 232 : 233),
          "%d: the frame centre stays on the same GRAM pixels", deg);

    memset(s_gram, 0, sizeof s_gram);
    s_off_panel = 0;
    flush_area(deg, 201, 419, 270, 446);
    flush_area(deg, 0, 420, KR_FRAME_W - 1, KR_FRAME_H - 1);
    wrong = glass_errors(deg, 0, 420, VIS_X1, VIS_Y1, &unwritten);
    CHECK(s_off_panel == 0 && unwritten == 0 && wrong == 0,
          "%d: the bottom strip and a small window near it repaint their own rows: %d past, %d stale, %d wrong",
          deg, s_off_panel, unwritten, wrong);
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
    int miss = 0;
    for (int fx = 0; fx < KR_FRAME_W; fx++)
        for (int fy = 0; fy < KR_FRAME_H; fy++) {
            int gx = gram_x(180, fx), gy = gram_y(180, fy);
            if (gx < KR_VIS_X0 || gx > VIS_X1 || gy > VIS_Y1) continue;
            kr_touch(180, &gx, &gy);
            miss += gx != fx || gy != fy;
        }
    CHECK(miss == 0, "180: a touch on every visible frame pixel maps back to it: %d missed", miss);
    x = 236, y = 233;
    kr_touch(180, &x, &y);
    CHECK(x == 235 && y == 232, "180: centre %d,%d", x, y);
    x = 0, y = 0;
    kr_touch(180, &x, &y);
    CHECK(x == KR_FRAME_W - 1 && y == KR_FRAME_H - 1, "180: corner %d,%d", x, y);
    x = -5, y = 600;
    kr_touch(180, &x, &y);
    CHECK(x == KR_FRAME_W - 1 && y == 0, "180: out of range clamps into the frame: %d,%d", x, y);
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
    test_image_in_place();
    test_panel_frame(0);
    test_panel_frame(180);
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
