#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bot_raster.h"
#include "pomo.h"
#include "pomo_ring.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static pomo_state_t st(pomo_phase_t ph, int running, int paused, int rem, int planned)
{
    return (pomo_state_t){.phase = ph, .running = running, .paused = paused, .remaining_sec = rem, .planned_sec = planned};
}

static void test_phase_parse(void)
{
    CHECK(pomo_phase_from_wire("idle") == POMO_PHASE_IDLE, "idle");
    CHECK(pomo_phase_from_wire("") == POMO_PHASE_IDLE, "empty = idle (Swift PomoPhase)");
    CHECK(pomo_phase_from_wire(NULL) == POMO_PHASE_IDLE, "null = idle");
    CHECK(pomo_phase_from_wire("focus") == POMO_PHASE_FOCUS, "focus");
    CHECK(pomo_phase_from_wire("short_break") == POMO_PHASE_SHORT_BREAK, "short_break");
    CHECK(pomo_phase_from_wire("long_break") == POMO_PHASE_LONG_BREAK, "long_break");
    CHECK(pomo_phase_from_wire("siesta") == POMO_PHASE_UNKNOWN, "unknown");
    CHECK(pomo_phase_is_break(POMO_PHASE_SHORT_BREAK) && pomo_phase_is_break(POMO_PHASE_LONG_BREAK) &&
              !pomo_phase_is_break(POMO_PHASE_FOCUS),
          "is_break");
}

static void test_actions(void)
{
    pomo_state_t idle = st(POMO_PHASE_IDLE, 0, 0, 0, 0);
    pomo_state_t run = st(POMO_PHASE_FOCUS, 1, 0, 600, 1500);
    pomo_state_t paused = st(POMO_PHASE_FOCUS, 1, 1, 600, 1500);
    pomo_state_t parked = st(POMO_PHASE_SHORT_BREAK, 0, 0, 300, 300);
    CHECK(pomo_mode(&idle) == POMO_MODE_IDLE && pomo_mode(&run) == POMO_MODE_RUNNING &&
              pomo_mode(&paused) == POMO_MODE_PAUSED && pomo_mode(&parked) == POMO_MODE_PARKED,
          "modes");
    CHECK(pomo_action_for(&idle, POMO_INPUT_PUSH) == POMO_ACT_START, "idle push = start");
    CHECK(pomo_action_for(&run, POMO_INPUT_PUSH) == POMO_ACT_PAUSE, "running push = pause");
    CHECK(pomo_action_for(&paused, POMO_INPUT_PUSH) == POMO_ACT_RESUME, "paused push = resume");
    CHECK(pomo_action_for(&parked, POMO_INPUT_PUSH) == POMO_ACT_RESUME, "parked push = resume (starts next)");
    CHECK(pomo_action_for(&idle, POMO_INPUT_LONG_PUSH) == POMO_ACT_NONE, "idle long push = nothing");
    CHECK(pomo_action_for(&run, POMO_INPUT_LONG_PUSH) == POMO_ACT_STOP, "running long push = stop");
    CHECK(pomo_action_for(&paused, POMO_INPUT_LONG_PUSH) == POMO_ACT_STOP, "paused long push = stop");
    CHECK(pomo_action_for(&parked, POMO_INPUT_LONG_PUSH) == POMO_ACT_STOP, "parked long push = stop");
    CHECK(strcmp(pomo_action_path(POMO_ACT_START), "start") == 0 && strcmp(pomo_action_path(POMO_ACT_PAUSE), "pause") == 0 &&
              strcmp(pomo_action_path(POMO_ACT_RESUME), "resume") == 0 && strcmp(pomo_action_path(POMO_ACT_STOP), "stop") == 0 &&
              strcmp(pomo_action_path(POMO_ACT_SKIP), "skip") == 0 && pomo_action_path(POMO_ACT_NONE) == NULL,
          "action paths");
}

static void test_fraction_and_format(void)
{
    pomo_state_t s = st(POMO_PHASE_IDLE, 0, 0, 0, 0);
    CHECK(pomo_fraction(&s) == 0, "idle fraction 0");
    s = st(POMO_PHASE_FOCUS, 1, 0, 750, 1500);
    CHECK(fabsf(pomo_fraction(&s) - 0.5f) < 1e-6f, "half");
    s.remaining_sec = 1500;
    CHECK(pomo_fraction(&s) == 1, "full");
    s.remaining_sec = 2000;
    CHECK(pomo_fraction(&s) == 1, "clamped high");
    s.remaining_sec = -5;
    CHECK(pomo_fraction(&s) == 0, "clamped low");

    char b[16];
    struct { int s; const char *want; } cases[] = {
        {0, "00:00"}, {9, "00:09"}, {59, "00:59"}, {60, "01:00"}, {1500, "25:00"},
        {5999, "99:59"}, {6000, "100:00"}, {-3, "00:00"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        pomo_format_mmss(cases[i].s, b, sizeof b);
        CHECK(strcmp(b, cases[i].want) == 0, "mmss(%d) = %s, want %s", cases[i].s, b, cases[i].want);
    }
}

static void test_clock(void)
{
    pomo_clock_t c;
    pomo_clock_init(&c);
    CHECK(!c.valid, "starts invalid");

    pomo_state_t run = st(POMO_PHASE_FOCUS, 1, 0, 100, 1500);
    pomo_clock_sync(&c, &run, 10.0);
    CHECK(pomo_clock_at(&c, 10.0).remaining_sec == 100, "at poll");
    CHECK(pomo_clock_at(&c, 10.99).remaining_sec == 100, "within the first second");
    CHECK(pomo_clock_at(&c, 11.0).remaining_sec == 99, "one second later");
    CHECK(pomo_clock_at(&c, 15.5).remaining_sec == 95, "5.5 s later");
    CHECK(pomo_clock_at(&c, 500).remaining_sec == 0, "floors at 0");
    CHECK(pomo_clock_at(&c, 9).remaining_sec == 100, "clock before base does not count up");

    pomo_clock_sync(&c, &(pomo_state_t){.phase = POMO_PHASE_FOCUS, .running = 1, .remaining_sec = 99, .planned_sec = 1500}, 12.9);
    CHECK(pomo_clock_at(&c, 12.9).remaining_sec == 98, "no step back on jitter");
    CHECK(pomo_clock_at(&c, 13.0).remaining_sec == 97, "keeps ticking on the old base");
    pomo_clock_sync(&c, &(pomo_state_t){.phase = POMO_PHASE_FOCUS, .running = 1, .remaining_sec = 50, .planned_sec = 1500}, 14.0);
    CHECK(pomo_clock_at(&c, 14.0).remaining_sec == 50 && pomo_clock_at(&c, 16.2).remaining_sec == 48, "rebase on jump");
    pomo_clock_sync(&c, &(pomo_state_t){.phase = POMO_PHASE_FOCUS, .running = 1, .remaining_sec = 50, .planned_sec = 1500}, 16.5);
    CHECK(pomo_clock_at(&c, 16.5).remaining_sec == 50, "2 s off is not jitter: rebase");

    pomo_state_t paused = st(POMO_PHASE_FOCUS, 1, 1, 48, 1500);
    pomo_clock_sync(&c, &paused, 17.0);
    CHECK(pomo_clock_at(&c, 100).remaining_sec == 48 && pomo_clock_at(&c, 100).paused, "paused does not tick");
    pomo_state_t resumed = st(POMO_PHASE_FOCUS, 1, 0, 48, 1500);
    pomo_clock_sync(&c, &resumed, 200.0);
    CHECK(pomo_clock_at(&c, 203.0).remaining_sec == 45, "resumed ticks from the resume poll");
    pomo_state_t brk = st(POMO_PHASE_SHORT_BREAK, 1, 0, 44, 300);
    pomo_clock_sync(&c, &brk, 204.0);
    CHECK(pomo_clock_at(&c, 204.0).remaining_sec == 44 && pomo_clock_at(&c, 204.0).phase == POMO_PHASE_SHORT_BREAK,
          "phase change rebases");
    pomo_state_t parked = st(POMO_PHASE_FOCUS, 0, 0, 1500, 1500);
    pomo_clock_sync(&c, &parked, 300.0);
    CHECK(pomo_clock_at(&c, 400.0).remaining_sec == 1500, "parked does not tick");
    pomo_state_t idle = st(POMO_PHASE_IDLE, 0, 0, 0, 0);
    pomo_clock_sync(&c, &idle, 500.0);
    CHECK(pomo_clock_at(&c, 600.0).remaining_sec == 0 && pomo_clock_at(&c, 600.0).phase == POMO_PHASE_IDLE, "idle");
}

#define BW 472
#define BH 466
static const pomo_ring_t RING = {.cx = 236, .cy = 233, .r = 224, .hw = 6};
#define ARC 0xFF6A3D
#define TRACK 0x2E1309

static uint16_t *new_buf(void) { return calloc(BW * BH, 2); }

static void full(uint16_t *b, pomo_ring_work_t *wk, float f)
{
    memset(b, 0, BW * BH * 2);
    pomo_ring_render(&RING, wk, b, BW, BH, (pomo_rect_t){0, 0, BW, BH}, f, ARC, TRACK);
}

static uint16_t at(const uint16_t *b, double angle_deg, double radius)
{
    double a = angle_deg * M_PI / 180;
    int x = (int)floor(RING.cx + radius * sin(a)), y = (int)floor(RING.cy - radius * cos(a));
    return b[y * BW + x];
}

static uint16_t rgb565(uint32_t rgb) { return pomo_ring_pixel(rgb, 0, 255, 0); }

static void reference(uint16_t *b, pomo_ring_work_t *wk, float f)
{
    static uint16_t tmp[BW * BH];
    static uint8_t at_[BW * BH], aa[BW * BH];
    static int segs[POMO_RING_PTS + 1];
    int narc = pomo_ring_points(&RING, wk, f);
    for (int i = 0; i < POMO_RING_PTS; i++) segs[i] = i;
    bot_raster_stroke_ref(tmp, at_, BW, BH, 0, 0, wk->tx, wk->ty, POMO_RING_PTS, segs, RING.hw, 0, 1);
    if (narc) bot_raster_stroke_ref(tmp, aa, BW, BH, 0, 0, wk->ax, wk->ay, narc, segs, RING.hw, 0, 1);
    else memset(aa, 0, sizeof aa);
    for (int i = 0; i < BW * BH; i++) b[i] = pomo_ring_pixel(ARC, TRACK, aa[i], at_[i]);
}

static int channel_diff(uint16_t p, uint16_t q)
{
    int dr = abs((p >> 11) - (q >> 11)), dg = abs(((p >> 5) & 63) - ((q >> 5) & 63)), db = abs((p & 31) - (q & 31));
    return dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
}

static void test_ring_geometry(void)
{
    static pomo_ring_work_t wk;
    uint16_t *b = new_buf();
    full(b, &wk, 0.5f);
    CHECK(at(b, 90, RING.r) == rgb565(ARC), "f=0.5: 3 o'clock is arc colour");
    CHECK(at(b, 170, RING.r) == rgb565(ARC), "f=0.5: just before 6 o'clock is arc colour");
    CHECK(at(b, 270, RING.r) == rgb565(TRACK), "f=0.5: 9 o'clock is track colour");
    CHECK(at(b, 45, 150) == 0 && at(b, 0, 0) == 0, "inside the ring is black");
    full(b, &wk, 0.0f);
    CHECK(at(b, 90, RING.r) == rgb565(TRACK), "f=0: only the track");
    full(b, &wk, 1.0f);
    CHECK(at(b, 270, RING.r) == rgb565(ARC) && at(b, 359, RING.r) == rgb565(ARC), "f=1: full arc");

    uint16_t *r = new_buf();
    float fs[] = {0, 0.001f, 0.25f, 0.4999f, 0.73f, 0.999f, 1};
    for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++) {
        full(b, &wk, fs[i]);
        reference(r, &wk, fs[i]);
        int bad = 0;
        for (int p = 0; p < BW * BH; p++) bad += b[p] != r[p];
        CHECK(bad == 0, "tiled render differs from reference at f=%.4f in %d px", fs[i], bad);
    }
    free(b);
    free(r);
}

static void test_ring_dirty(void)
{
    static pomo_ring_work_t wk;
    uint16_t *a = new_buf(), *b = new_buf();
    srand(7);
    int cases = 0, max_area = 0, worst_tick_w = 0, worst_tick_h = 0;
    for (int i = 0; i < 160; i++) {
        float f0, f1;
        if (i < 120) {
            int planned = i % 2 ? 1500 : 300;
            int rem = 1 + rand() % planned;
            f0 = (float)rem / planned;
            f1 = (float)(rem - 1) / planned;
        } else if (i < 130) {
            float edge[] = {0, 1, 0.0005f, 0.9995f, 0.5f};
            f0 = edge[i % 5];
            f1 = edge[(i / 5) % 5 == i % 5 ? (i + 1) % 5 : (i / 5) % 5];
        } else {
            f0 = (float)((double)rand() / RAND_MAX);
            f1 = (float)((double)rand() / RAND_MAX);
        }
        full(a, &wk, f0);
        pomo_rect_t d;
        if (pomo_ring_dirty(&RING, f0, f1, BW, BH, &d)) {
            CHECK(d.x % 2 == 0 && d.y % 2 == 0 && d.w % 2 == 0 && d.h % 2 == 0, "dirty rect not even-aligned");
            pomo_ring_render(&RING, &wk, a, BW, BH, d, f1, ARC, TRACK);
            if (i < 120) {
                if (d.w > worst_tick_w) worst_tick_w = d.w;
                if (d.h > worst_tick_h) worst_tick_h = d.h;
                if (d.w * d.h > max_area) max_area = d.w * d.h;
            }
        }
        full(b, &wk, f1);
        int bad = 0, worst = 0;
        for (int p = 0; p < BW * BH; p++) {
            if (a[p] != b[p]) {
                bad++;
                int cd = channel_diff(a[p], b[p]);
                if (cd > worst) worst = cd;
            }
        }
        CHECK(bad == 0, "f %.5f -> %.5f: %d px differ outside the dirty rect (max channel diff %d)", f0, f1, bad, worst);
        cases++;
    }
    CHECK(worst_tick_w <= 24 && worst_tick_h <= 24, "one-second tick dirty rect too big: %d x %d", worst_tick_w,
          worst_tick_h);
    printf("ring dirty: %d cases, largest one-second tick %d x %d px\n", cases, worst_tick_w, worst_tick_h);
    free(a);
    free(b);
}

int main(void)
{
    test_phase_parse();
    test_actions();
    test_fraction_and_format();
    test_clock();
    test_ring_geometry();
    test_ring_dirty();
    printf(failures ? "%d FAILED\n" : "all pomo tests passed\n", failures);
    return failures ? 1 : 0;
}
