#include <stdio.h>
#include <stdlib.h>

#include "touch_swipe.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define POLL_S 0.016

static ts_result_t gesture(int x0, int y0, int x1, int y1, double dur, bool swipe, int *results)
{
    ts_touch_t s;
    int n = 0;
    ts_result_t last = ts_press(&s, x0, y0, 0, swipe);
    if (last != TS_NONE) n++;
    int steps = (int)(dur / POLL_S);
    for (int i = 1; i <= steps; i++)
        ts_move(&s, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, i * POLL_S, i * POLL_S);
    ts_result_t r = ts_release(&s, x1, y1, dur, dur);
    if (r != TS_NONE) {
        n++;
        last = r;
    }
    if (results) *results = n;
    return last;
}

static void test_tap(void)
{
    ts_touch_t s;
    CHECK(ts_press(&s, 236, 233, 0, true) == TS_NONE, "press does not decide");
    ts_move(&s, 242, 229, 0.016, 0.016);
    ts_move(&s, 231, 238, 0.032, 0.032);
    CHECK(ts_release(&s, 238, 235, 0.1, 0.1) == TS_TAP, "jitter within the slop is a tap");
    CHECK(ts_release(&s, 238, 235, 0.2, 0.2) == TS_NONE, "second release does nothing");
    ts_press(&s, 236, 233, 0, true);
    for (double t = POLL_S; t < 2.0; t += POLL_S) ts_move(&s, 236, 233, t, t);
    CHECK(ts_release(&s, 236, 233, 2.0, 2.0) == TS_TAP, "a long still press with fresh reports is a tap, as before");
    CHECK(ts_press(&s, 5, 5, 0, true) == TS_NONE && ts_release(&s, 5, 5, 0.1, 0.1) == TS_TAP,
          "a tap outside the circle still counts");
}

static void test_swipe(void)
{
    int n;
    CHECK(gesture(236, 300, 236, 180, 0.15, true, &n) == TS_NEXT && n == 1, "swipe up = next");
    CHECK(gesture(236, 180, 236, 300, 0.15, true, &n) == TS_PREV && n == 1, "swipe down = previous");
    CHECK(gesture(236, 250, 236, 250 - TS_SWIPE_MIN_PX, 0.1, true, NULL) == TS_NEXT, "minimum distance");
    CHECK(gesture(236, 250, 236, 250 - TS_SWIPE_MIN_PX + 1, 0.1, true, &n) == TS_NONE && n == 0,
          "short drag: neither tap nor swipe");
    CHECK(gesture(200, 300, 260, 200, 0.15, true, NULL) == TS_NEXT, "30 degrees off vertical");
    CHECK(gesture(180, 300, 280, 200, 0.15, true, NULL) == TS_NONE, "45 degrees: ignored");
    CHECK(gesture(130, 233, 330, 233, 0.15, true, NULL) == TS_NONE, "horizontal: ignored");
    CHECK(gesture(236, 300, 236, 180, TS_SWIPE_MAX_S + 0.05, true, NULL) == TS_NONE, "slow drag: ignored");
    CHECK(gesture(20, 20, 20, 200, 0.15, true, NULL) == TS_NONE, "start outside the circle: ignored");
    CHECK(gesture(236, 300, 236, 150, 0.15, false, &n) == TS_TAP && n == 1, "swipe off: press is a tap, no swipe");

    ts_touch_t s;
    ts_press(&s, 236, 300, 0, true);
    ts_move(&s, 236, 250, 0.05, 0.05);
    ts_move(&s, 236, 200, 0.1, 0.1);
    ts_move(&s, 236, 250, 0.15, 0.15);
    CHECK(ts_release(&s, 236, 296, 0.2, 0.2) == TS_NONE, "out and back: neither");
    ts_press(&s, 236, 300, 0, true);
    ts_move(&s, 236, 250, 0.05, 0.05);
    ts_cancel(&s);
    CHECK(ts_release(&s, 236, 200, 0.1, 0.1) == TS_NONE, "press lost: nothing");

    int both = 0;
    for (int dy = -200; dy <= 200; dy += 5)
        for (int dx = -100; dx <= 100; dx += 10) {
            gesture(236, 233, 236 + dx, 233 + dy, 0.2, true, &n);
            both += n > 1;
        }
    CHECK(both == 0, "never a tap and a swipe for one touch: %d", both);
}

static void test_guards(void)
{
    ts_touch_t s;
    ts_press(&s, 236, 150, 0, true);
    for (double t = POLL_S; t < 0.5; t += POLL_S) ts_move(&s, 236, 150, 0, t);
    ts_move(&s, 236, 300, 0.5, 0.5);
    CHECK(ts_release(&s, 236, 300, 0.55, 0.55) == TS_NONE, "missed release, then a tap at y=300: no page, no tap");

    ts_press(&s, 236, 150, 0, true);
    ts_move(&s, 236, 150, 0.016, 0.016);
    ts_move(&s, 236, 300, 0.032, 0.032);
    CHECK(ts_release(&s, 236, 300, 0.048, 0.048) == TS_NONE, "jump of 150 px between reads: cancelled");

    ts_press(&s, 236, 233, 0, true);
    ts_move(&s, 236, 233, 0, 0.1);
    CHECK(ts_release(&s, 236, 233, 0, 0.1) == TS_TAP, "no new report for 100 ms: still a tap");
    ts_press(&s, 236, 233, 0, true);
    ts_move(&s, 236, 233, 0, 0.1);
    ts_move(&s, 236, 233, 0, TS_STALE_S + 0.02);
    CHECK(ts_release(&s, 236, 233, 0, TS_STALE_S + 0.04) == TS_NONE, "stuck press: cancelled");
    ts_press(&s, 236, 233, 0, true);
    CHECK(ts_release(&s, 236, 233, 0, TS_STALE_S + 0.04) == TS_NONE, "stuck press found at release: cancelled");
    ts_press(&s, 236, 233, 0, true);
    ts_move(&s, 236, 233, 0, TS_STALE_S + 0.02);
    ts_move(&s, 236, 233, TS_STALE_S + 0.03, TS_STALE_S + 0.03);
    CHECK(ts_release(&s, 236, 233, TS_STALE_S + 0.04, TS_STALE_S + 0.04) == TS_NONE, "a cancel sticks until the next press");
    CHECK(ts_press(&s, 236, 233, 1, true) == TS_NONE && ts_release(&s, 236, 233, 1.05, 1.05) == TS_TAP,
          "the next press works again");

    int n;
    CHECK(gesture(236, 400, 236, 70, 0.07, true, &n) == TS_NEXT && n == 1, "fast swipe, 82 px per read: next");

    static const struct { double px_per_ms, gap_ms; } LATE[] = {{4.7, 32}, {4.7, 40}, {3.75, 32}, {3.75, 40}, {2.5, 40}};
    for (unsigned k = 0; k < sizeof LATE / sizeof LATE[0]; k++) {
        double gap = LATE[k].gap_ms / 1000, dur = 300 / LATE[k].px_per_ms / 1000;
        ts_press(&s, 236, 383, 0, true);
        ts_move(&s, 236, 383, 0, 0.0005);
        double t = gap;
        for (; t < dur; t += gap) ts_move(&s, 236, 383 - (int)(300 * t / dur), t, t);
        CHECK(ts_release(&s, 236, 83, t, t) == TS_NEXT, "300 px swipe at %.2f px/ms, reads %.0f ms apart: next",
              LATE[k].px_per_ms, LATE[k].gap_ms);
    }

    ts_press(&s, 236, 150, 0, true);
    for (double t = POLL_S; t < 0.07; t += POLL_S) ts_move(&s, 236, 150, t, t);
    for (double t = 0.08; t < 0.18; t += POLL_S) ts_move(&s, 236, 150, 0.064, t);
    ts_move(&s, 236, 300, 0.19, 0.19);
    CHECK(ts_release(&s, 236, 300, 0.25, 0.25) == TS_NONE, "two taps chained over a 100 ms lift gap: cancelled");
    ts_press(&s, 236, 150, 0, true);
    ts_move(&s, 236, 150, 0.016, 0.016);
    ts_move(&s, 236, 300, 0.2, 0.2);
    CHECK(ts_release(&s, 236, 300, 0.25, 0.25) == TS_NONE, "two taps across a 184 ms gap with no reads: cancelled");
}

static void test_page_step(void)
{
    CHECK(ts_page_step(2, 3, 1) == 0, "wraps forward");
    CHECK(ts_page_step(0, 3, -1) == 2, "wraps back");
    CHECK(ts_page_step(1, 4, 9) == 2, "many steps");
    CHECK(ts_page_step(0, 4, -9) == 3, "many steps back");
    CHECK(ts_page_step(0, 1, 1) == 0, "one page");
    CHECK(ts_page_step(0, 0, 1) == 0, "no pages");
    for (int n = 1; n <= 4; n++)
        for (int p = 0; p < n; p++)
            CHECK(ts_page_step(ts_page_step(p, n, 1), n, -1) == p, "next then previous returns (n %d pos %d)", n, p);
}

static void test_wipe(void)
{
    CHECK(ts_wipe_edge(-1) == TS_H && ts_wipe_edge(0) == TS_H, "starts fully black");
    CHECK(ts_wipe_edge(TS_WIPE_S) == -TS_WIPE_BAND_PX, "ends clear");
    CHECK(ts_wipe_edge(5) == -TS_WIPE_BAND_PX, "stays clear");
    int prev = TS_H, frames = 0, max_rows = 0;
    static unsigned char covered[2][TS_H];
    for (double t = POLL_S; prev > -TS_WIPE_BAND_PX; t += POLL_S) {
        int e = ts_wipe_edge(t);
        CHECK(e <= prev, "edge only moves up");
        for (int dir = -1; dir <= 1; dir += 2) {
            int y1, y2;
            bool any = ts_wipe_rows(prev, e, dir, &y1, &y2);
            CHECK(!any || (y1 >= 0 && y2 <= TS_H - 1 && y1 <= y2), "rows clipped: dir %d %d..%d", dir, y1, y2);
            for (int y = 0; any && y < TS_H; y++) {
                int ny = dir > 0 ? y : TS_H - 1 - y;
                bool changed = (ny >= e && ny < prev + TS_WIPE_BAND_PX);
                CHECK((y >= y1 && y <= y2) == changed, "dir %d row %d: changed %d, in %d..%d", dir, y, changed, y1, y2);
                if (y >= y1 && y <= y2) covered[dir > 0][y] = 1;
            }
            if (dir > 0 && any && y2 - y1 + 1 > max_rows) max_rows = y2 - y1 + 1;
        }
        prev = e;
        frames++;
    }
    for (int y = 0; y < TS_H; y++) CHECK(covered[0][y] && covered[1][y], "row %d redrawn in both directions", y);
    int y1, y2;
    CHECK(ts_wipe_span(-10, 20, 1, &y1, &y2) && y1 == 0 && y2 == 19, "span clipped at the top");
    CHECK(ts_wipe_span(-10, 20, -1, &y1, &y2) && y1 == TS_H - 20 && y2 == TS_H - 1, "mirrored span clipped at the bottom");
    CHECK(ts_wipe_span(450, 500, 1, &y1, &y2) && y1 == 450 && y2 == TS_H - 1, "span clipped at the bottom");
    CHECK(ts_wipe_span(450, 500, -1, &y1, &y2) && y1 == 0 && y2 == TS_H - 451, "mirrored span clipped at the top");
    CHECK(!ts_wipe_span(-40, -8, 1, &y1, &y2) && !ts_wipe_span(TS_H, TS_H + 9, -1, &y1, &y2), "off-screen span: empty");
    CHECK(!ts_wipe_rows(-TS_WIPE_BAND_PX, -TS_WIPE_BAND_PX, 1, &y1, &y2), "no move: nothing");
    CHECK(frames <= 12, "frames %d", frames);
    printf("wipe: %d frames of %.0f ms, largest strip %d rows (%d px of 472x466)\n", frames, POLL_S * 1000, max_rows,
           max_rows * 472);
    CHECK(ts_wipe_band_opa(0) == 255 && ts_wipe_band_opa(TS_WIPE_BAND_PX) == 0 && ts_wipe_band_opa(-3) == 255,
          "band ends");
    for (int d = 1; d < TS_WIPE_BAND_PX; d++) CHECK(ts_wipe_band_opa(d) <= ts_wipe_band_opa(d - 1), "band fades");
}

int main(void)
{
    test_tap();
    test_swipe();
    test_guards();
    test_page_step();
    test_wipe();
    if (failures) {
        printf("swipe: %d failures\n", failures);
        return 1;
    }
    printf("swipe: all tests passed\n");
    return 0;
}
