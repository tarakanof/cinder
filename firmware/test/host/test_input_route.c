#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_knob_15_md50et.h"
#include "dim_ctl.h"
#include "ember_client.h"
#include "esp_timer.h"
#include "input_route.h"
#include "iot_button.h"
#include "iot_knob.h"
#include "ota_client.h"
#include "press_route.h"
#include "touch_swipe.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define RESET_HOLD_MS ((int)(RG_HOLD_S * 1000) + 1)

int host_crit_depth;

static int64_t clock_us = 1000000;
static int64_t report_us;
static bool idle_answer, panel_off, classify = true;
static int idle_calls, note_calls;
static dim_wake_t idle_src;
static lv_point_t point;

int64_t esp_timer_get_time(void) { return clock_us; }
int64_t bsp_knob_15_md50et_touch_report_us(void) { return report_us; }
bool dim_ctl_off(void) { return panel_off; }
bool input_route_classify_touch(void) { return classify; }

bool ember_client_idle_input(dim_wake_t src)
{
    CHECK(host_crit_depth == 0, "the idle clock runs outside the gesture lock");
    idle_calls++;
    idle_src = src;
    return idle_answer;
}

void ota_client_note_input(void)
{
    CHECK(host_crit_depth == 0, "note_input runs outside the gesture lock");
    note_calls++;
}

lv_event_code_t lv_event_get_code(lv_event_t *e) { return e->code; }
lv_indev_t *lv_indev_active(void) { return (lv_indev_t *)&point; }

void lv_indev_get_point(const lv_indev_t *indev, lv_point_t *p)
{
    (void)indev;
    *p = point;
}

static void advance_ms(int ms) { clock_us += ms * 1000LL; }
static double clock_s(void) { return clock_us / 1e6; }

static void turn(int dir) { input_route_knob_cb((void *)(intptr_t)(dir > 0 ? KNOB_LEFT : KNOB_RIGHT)); }
static void button(button_event_t ev) { input_route_button_cb((void *)(intptr_t)ev); }

static void touch(lv_event_code_t code, int x, int y)
{
    report_us = clock_us;
    point = (lv_point_t){x, y};
    lv_event_t e = {code};
    input_route_touch_cb(&e);
}

static void tap(void)
{
    touch(LV_EVENT_PRESSED, TS_CX, TS_CY);
    advance_ms(20);
    touch(LV_EVENT_RELEASED, TS_CX, TS_CY);
}

static void swipe(int dx, int dy)
{
    touch(LV_EVENT_PRESSED, TS_CX, TS_CY);
    for (int i = 1; i <= 2; i++) {
        advance_ms(20);
        touch(LV_EVENT_PRESSING, TS_CX + dx * i / 2, TS_CY + dy * i / 2);
    }
    advance_ms(20);
    touch(LV_EVENT_RELEASED, TS_CX + dx, TS_CY + dy);
}

static page_input_t take(long long *held)
{
    page_input_t in;
    memset(&in, 0x55, sizeof in);
    long long h = input_route_take(&in);
    if (held) *held = h;
    return in;
}

static void expect(int detents, int pushes, int longs, int touches, int wakes, int hswipes, const char *what)
{
    page_input_t in = take(NULL);
    CHECK(in.detents == detents, "%s: detents %d, want %d", what, in.detents, detents);
    CHECK(in.pushes == pushes, "%s: pushes %d, want %d", what, in.pushes, pushes);
    CHECK(in.longs == longs, "%s: longs %d, want %d", what, in.longs, longs);
    CHECK(in.touches == touches, "%s: touches %d, want %d", what, in.touches, touches);
    CHECK(in.wakes == wakes, "%s: wakes %d, want %d", what, in.wakes, wakes);
    CHECK(in.hswipes == hswipes, "%s: hswipes %d, want %d", what, in.hswipes, hswipes);
}

static void expect_steps(int steps, int swipes, const char *what)
{
    int s, w;
    input_route_take_steps(&s, &w);
    CHECK(s == steps, "%s: page steps %d, want %d", what, s, steps);
    CHECK(w == swipes, "%s: swipes %d, want %d", what, w, swipes);
}

static void expect_nothing(const char *what)
{
    CHECK(!input_route_pending(), "%s: nothing pending", what);
    expect(0, 0, 0, 0, 0, 0, what);
    expect_steps(0, 0, what);
}

static void reset(void)
{
    idle_answer = panel_off = false;
    classify = true;
    button(BUTTON_PRESS_UP);
    touch(LV_EVENT_PRESS_LOST, 0, 0);
    input_route_init();
    input_route_drop();
    idle_calls = note_calls = 0;
    idle_src = DIM_WAKE_NONE;
    CHECK(!input_route_held(), "reset leaves nothing held");
}

static void expect_untouched(const char *what)
{
    CHECK(!input_route_pending(), "%s: nothing pending", what);
    expect_steps(0, 0, what);
}

static void arm_reset(void)
{
    int progress;
    button(BUTTON_PRESS_DOWN);
    advance_ms(RESET_HOLD_MS);
    CHECK(input_route_reset_tick(clock_s(), &progress) == RG_ARMED, "held for RG_HOLD_S arms the reset");
    CHECK(progress == 0, "an armed reset starts at 0");
}

static void test_turn(void)
{
    reset();
    turn(1);
    turn(1);
    turn(-1);
    CHECK(input_route_pending(), "a turn is pending");
    expect(1, 0, 0, 0, 0, 0, "two clockwise and one back");
    expect_steps(0, 0, "a plain turn is no page step");
    CHECK(note_calls == 3, "every turn notes the input: %d", note_calls);
    CHECK(idle_calls == 3 && idle_src == DIM_WAKE_ENCODER, "every turn asks the idle clock as the encoder");
}

static void test_turn_while_down(void)
{
    reset();
    button(BUTTON_PRESS_DOWN);
    CHECK(input_route_held(), "the button is held");
    turn(1);
    turn(1);
    turn(-1);
    expect(0, 0, 0, 0, 0, 0, "a turn while down is no detent");
    expect_steps(1, 0, "turns while down page, every one of them");
    advance_ms(PR_HOLD_MS + 100);
    long long held;
    expect(0, 0, 0, 0, 0, 0, "a turned press never fires the long push");
    take(&held);
    CHECK(held == -1, "no ring on a turned press");
    button(BUTTON_PRESS_UP);
    CHECK(!input_route_held(), "released");
    expect_nothing("the release of a turned press is no push");
}

static void test_push(void)
{
    reset();
    button(BUTTON_PRESS_DOWN);
    advance_ms(100);
    long long held;
    take(&held);
    CHECK(held == -1, "no ring before PR_COUNTDOWN_MS");
    button(BUTTON_PRESS_UP);
    expect(0, 1, 0, 0, 0, 0, "a short press");
    CHECK(note_calls == 2, "down and up both note the input: %d", note_calls);
    CHECK(idle_calls == 2 && idle_src == DIM_WAKE_ENCODER, "down and up both ask the idle clock as the encoder");
    expect_nothing("a short press is one push");
}

static void test_long_push(void)
{
    reset();
    button(BUTTON_PRESS_DOWN);
    advance_ms(PR_HOLD_MS - 1);
    long long held;
    page_input_t in = take(&held);
    CHECK(in.longs == 0, "not yet at PR_HOLD_MS - 1");
    CHECK(held == PR_HOLD_MS - 1, "the ring gets the time held: %lld", held);
    advance_ms(1);
    in = take(&held);
    CHECK(in.longs == 1 && in.pushes == 0, "fires at PR_HOLD_MS, in the same take");
    CHECK(held == -1, "no ring once fired");
    advance_ms(100);
    expect(0, 0, 0, 0, 0, 0, "fires once");
    turn(1);
    expect_steps(0, 0, "a turn after the long push is dropped");
    expect(0, 0, 0, 0, 0, 0, "and is no detent");
    button(BUTTON_PRESS_UP);
    expect_nothing("the release of a fired press is no push");
}

static void test_hold_race(void)
{
    reset();
    button(BUTTON_PRESS_DOWN);
    advance_ms(PR_HOLD_MS + 50);
    turn(1);
    expect_steps(1, 0, "a turn taking the claim at the hold mark pages");
    expect(0, 0, 0, 0, 0, 0, "and the long push does not fire");
    button(BUTTON_PRESS_UP);
    expect_nothing("and the release is nothing");

    reset();
    button(BUTTON_PRESS_DOWN);
    advance_ms(PR_HOLD_MS + 50);
    button(BUTTON_PRESS_UP);
    expect(0, 1, 0, 0, 0, 0, "a release before the frame takes the claim: the frame fires nothing");
}

static void test_late_frame(void)
{
    reset();
    button(BUTTON_PRESS_DOWN);
    advance_ms(PR_HOLD_MS + 50);
    button(BUTTON_LONG_PRESS_HOLD);
    button(BUTTON_PRESS_UP);
    expect(0, 0, 1, 0, 0, 0, "the iot_button hold stands in for a frame that came late");
    expect_nothing("one long push");
}

static void test_hold_ring(void)
{
    reset();
    button(BUTTON_PRESS_DOWN);
    advance_ms(PR_COUNTDOWN_MS - 1);
    long long held;
    take(&held);
    CHECK(held == -1, "no ring before PR_COUNTDOWN_MS");
    advance_ms(1);
    take(&held);
    CHECK(held == PR_COUNTDOWN_MS, "the ring from PR_COUNTDOWN_MS: %lld", held);
    input_route_hold_counting();
    button(BUTTON_PRESS_UP);
    expect_nothing("letting go while the ring counts cancels");
    take(&held);
    CHECK(held == -1, "no ring after the release");

    reset();
    button(BUTTON_PRESS_DOWN);
    advance_ms(1000);
    take(&held);
    CHECK(held == 1000, "the page shows no ring");
    button(BUTTON_PRESS_UP);
    expect(0, 1, 0, 0, 0, 0, "a slow press on a page with no long action is a push");
}

static void test_wake_turn(void)
{
    reset();
    idle_answer = true;
    turn(1);
    CHECK(note_calls == 1 && idle_calls == 1 && idle_src == DIM_WAKE_ENCODER, "a waking turn is noted and asks the clock");
    expect_nothing("a waking turn is dropped");
}

static void test_wake_press(void)
{
    reset();
    idle_answer = true;
    button(BUTTON_PRESS_DOWN);
    idle_answer = false;
    CHECK(!input_route_held(), "a waking press is not held");
    turn(1);
    expect_nothing("a turn during a waking press is dropped");
    advance_ms(PR_HOLD_MS + 100);
    long long held;
    expect(0, 0, 0, 0, 0, 0, "a waking press never fires the long push");
    take(&held);
    CHECK(held == -1, "no ring on a waking press");
    button(BUTTON_LONG_PRESS_HOLD);
    button(BUTTON_PRESS_UP);
    expect_nothing("the release of a waking press is nothing");
    int progress;
    advance_ms(RESET_HOLD_MS);
    CHECK(input_route_reset_tick(clock_s(), &progress) == RG_IDLE, "a waking press does not start the reset");

    button(BUTTON_PRESS_DOWN);
    advance_ms(50);
    button(BUTTON_PRESS_UP);
    expect(0, 1, 0, 0, 0, 0, "the next press is a push");
    turn(1);
    expect(1, 0, 0, 0, 0, 0, "and the next turn a detent");
}

static void test_wake_touch(void)
{
    reset();
    idle_answer = true;
    touch(LV_EVENT_PRESSED, TS_CX, TS_CY);
    idle_answer = false;
    CHECK(note_calls == 1 && idle_calls == 1 && idle_src == DIM_WAKE_TOUCH, "a waking touch is noted and asks the clock");
    CHECK(!input_route_held(), "a waking touch is not held");
    advance_ms(20);
    touch(LV_EVENT_PRESSING, TS_CX, TS_CY);
    advance_ms(20);
    touch(LV_EVENT_RELEASED, TS_CX, TS_CY);
    expect_nothing("the whole waking touch is dropped");
    CHECK(note_calls == 1 && idle_calls == 1, "only the press counts");

    tap();
    expect(0, 0, 0, 1, 1, 0, "the next touch is delivered");
}

static void test_touch(void)
{
    reset();
    touch(LV_EVENT_PRESSED, TS_CX, TS_CY);
    CHECK(input_route_held(), "a touch is held");
    CHECK(note_calls == 1 && idle_calls == 1 && idle_src == DIM_WAKE_TOUCH, "a press notes and asks once");
    advance_ms(20);
    touch(LV_EVENT_PRESSING, TS_CX, TS_CY);
    CHECK(input_route_held(), "still held");
    advance_ms(20);
    touch(LV_EVENT_RELEASED, TS_CX, TS_CY);
    CHECK(!input_route_held(), "released");
    CHECK(note_calls == 1 && idle_calls == 1, "pressing and releasing do not count as input");
    page_input_t in = take(NULL);
    CHECK(in.touches == 1 && in.wakes == 1, "a tap and its wake: %d, %d", in.touches, in.wakes);
    CHECK(in.taps[TS_ZONE_PLAY] == 1 && in.taps[TS_ZONE_NONE] == 0, "the tap keeps its zone");

    reset();
    classify = false;
    touch(LV_EVENT_PRESSED, TS_CX, TS_CY);
    expect(0, 0, 0, 1, 1, 0, "not classifying, the press is the tap");
    advance_ms(20);
    touch(LV_EVENT_RELEASED, TS_CX, TS_CY);
    expect_nothing("and the release is nothing");

    reset();
    touch(LV_EVENT_PRESSED, TS_CX, TS_CY);
    panel_off = true;
    advance_ms(20);
    touch(LV_EVENT_RELEASED, TS_CX, TS_CY);
    panel_off = false;
    CHECK(!input_route_held(), "a release after the panel went off ends the touch");
    expect(0, 0, 0, 0, 1, 0, "and cancels the tap");

    reset();
    touch(LV_EVENT_PRESSED, TS_CX, TS_CY);
    advance_ms(20);
    touch(LV_EVENT_PRESS_LOST, TS_CX, TS_CY);
    CHECK(!input_route_held(), "a lost press ends the touch");
    expect(0, 0, 0, 0, 1, 0, "and is no tap");
}

static void test_swipes(void)
{
    reset();
    swipe(0, -100);
    expect_steps(0, 1, "swipe up");
    expect(0, 0, 0, 0, 1, 0, "swipe up is no tap");
    swipe(0, 100);
    expect_steps(0, -1, "swipe down");
    swipe(-100, 0);
    expect_steps(0, 0, "swipe left pages nothing");
    swipe(100, 0);
    expect(0, 0, 0, 0, 3, 0, "left and right cancel out in one take");
    swipe(-100, 0);
    expect(0, 0, 0, 0, 1, 1, "swipe left");
    swipe(100, 0);
    expect(0, 0, 0, 0, 1, -1, "swipe right");
}

static void test_inject(void)
{
    static const struct {
        int kind;
        dim_wake_t src;
        int detents, pushes, longs, touches, hswipes, steps, swipes, zone;
    } T[] = {
        {0, DIM_WAKE_ENCODER, 2, 0, 0, 0, 0, 0, 0, -1},  {1, DIM_WAKE_ENCODER, 0, 2, 0, 0, 0, 0, 0, -1},
        {2, DIM_WAKE_ENCODER, 0, 0, 2, 0, 0, 0, 0, -1},  {3, DIM_WAKE_TOUCH, 0, 0, 0, 2, 0, 0, 0, TS_ZONE_NONE},
        {4, DIM_WAKE_ENCODER, 0, 0, 0, 0, 0, 2, 0, -1},  {5, DIM_WAKE_TOUCH, 0, 0, 0, 0, 0, 0, 2, -1},
        {6, DIM_WAKE_TOUCH, 0, 0, 0, 0, 0, 0, -2, -1},   {7, DIM_WAKE_TOUCH, 0, 0, 0, 0, 2, 0, 0, -1},
        {8, DIM_WAKE_TOUCH, 0, 0, 0, 0, -2, 0, 0, -1},   {9, DIM_WAKE_TOUCH, 0, 0, 0, 2, 0, 0, 0, TS_ZONE_PREV},
        {10, DIM_WAKE_TOUCH, 0, 0, 0, 2, 0, 0, 0, TS_ZONE_PLAY}, {11, DIM_WAKE_TOUCH, 0, 0, 0, 2, 0, 0, 0, TS_ZONE_NEXT},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        reset();
        char what[32];
        snprintf(what, sizeof what, "inject kind %d", T[i].kind);
        input_route_inject(T[i].kind, 2);
        CHECK(idle_calls == 1 && idle_src == T[i].src, "%s asks the idle clock first as its source", what);
        CHECK(note_calls == 0, "%s is not a hardware input", what);
        CHECK(input_route_pending(), "%s is pending", what);
        page_input_t in = take(NULL);
        CHECK(in.detents == T[i].detents && in.pushes == T[i].pushes && in.longs == T[i].longs, "%s: turn and press counts", what);
        CHECK(in.touches == T[i].touches && in.hswipes == T[i].hswipes && in.wakes == 0, "%s: touch counts", what);
        if (T[i].zone >= 0) CHECK(in.taps[T[i].zone] == 2, "%s: the tap's zone", what);
        expect_steps(T[i].steps, T[i].swipes, what);
        expect_nothing(what);

        reset();
        idle_answer = true;
        input_route_inject(T[i].kind, 2);
        expect_nothing("a waking inject is dropped");
    }
    reset();
    input_route_inject(12, 2);
    expect_nothing("an unknown kind");
}

static void load_all(void)
{
    static const int KINDS[] = {0, 1, 2, 3, 4, 5, 7, 11};
    for (size_t i = 0; i < sizeof KINDS / sizeof KINDS[0]; i++) input_route_inject(KINDS[i], 1);
    touch(LV_EVENT_PRESSED, TS_CX, TS_CY);
    touch(LV_EVENT_PRESS_LOST, TS_CX, TS_CY);
}

static void test_take_and_drop(void)
{
    reset();
    load_all();
    page_input_t in = take(NULL);
    CHECK(in.detents == 1 && in.pushes == 1 && in.longs == 1 && in.touches == 2 && in.wakes == 1 && in.hswipes == 1, "the take carries every count");
    CHECK(input_route_pending(), "page steps and swipes wait for their own take");
    expect_steps(1, 1, "page steps and swipes");
    expect_nothing("the two takes zero every counter");

    for (int kind = 0; kind <= 8; kind++) {
        reset();
        input_route_inject(kind, 1);
        CHECK(input_route_pending(), "inject %d is pending", kind);
        input_route_drop();
        expect_nothing("a drop clears one counter");
    }
    reset();
    load_all();
    input_route_drop();
    expect_nothing("a drop clears every counter");

    reset();
    touch(LV_EVENT_PRESSED, TS_CX, TS_CY);
    CHECK(input_route_pending(), "a touch press is pending");
    input_route_drop();
    CHECK(!input_route_pending(), "a drop clears the wake");
    touch(LV_EVENT_PRESS_LOST, TS_CX, TS_CY);
}

static void test_reset_gesture(void)
{
    reset();
    arm_reset();
    turn(-1);
    expect_untouched("a turn the armed reset takes reaches no counter");
    int progress;
    CHECK(input_route_reset_tick(clock_s(), &progress) == RG_IDLE, "turning left cancels");
    turn(1);
    expect_untouched("a press the reset consumed stays out of the counters");
    button(BUTTON_PRESS_UP);
    expect_nothing("and its release is no push");

    reset();
    button(BUTTON_PRESS_DOWN);
    advance_ms(PR_HOLD_MS);
    expect(0, 0, 1, 0, 0, 0, "the gesture passes the long push on the way");
    advance_ms(RESET_HOLD_MS - PR_HOLD_MS);
    CHECK(input_route_reset_tick(clock_s(), &progress) == RG_ARMED, "and still arms");

    reset();
    arm_reset();
    for (int i = 1; i <= RG_DETENTS; i++) {
        advance_ms(10);
        turn(1);
        rg_state_t st = input_route_reset_tick(clock_s(), &progress);
        CHECK(progress == i, "progress %d after %d turns", progress, i);
        CHECK(st == (i < RG_DETENTS ? RG_ARMED : RG_CONFIRMED), "armed until the last detent");
    }
    expect_untouched("a confirmed reset took every turn");

    reset();
    arm_reset();
    idle_answer = true;
    turn(1);
    idle_answer = false;
    input_route_reset_tick(clock_s(), &progress);
    CHECK(progress == 0, "the waking turn is dropped before the gesture sees it");

    reset();
    button(BUTTON_PRESS_DOWN);
    turn(1);
    advance_ms(RESET_HOLD_MS);
    CHECK(input_route_reset_tick(clock_s(), &progress) == RG_IDLE, "a turned press does not arm");
    expect_steps(1, 0, "it paged");

    reset();
    arm_reset();
    input_route_reset_clear();
    CHECK(input_route_reset_tick(clock_s(), &progress) == RG_IDLE, "the OTA face forgets the gesture");
    CHECK(progress == 0, "and its progress");
    CHECK(host_crit_depth == 0, "every lock section closed");
}

int main(void)
{
    test_turn();
    test_turn_while_down();
    test_push();
    test_long_push();
    test_hold_race();
    test_late_frame();
    test_hold_ring();
    test_wake_turn();
    test_wake_press();
    test_wake_touch();
    test_touch();
    test_swipes();
    test_inject();
    test_take_and_drop();
    test_reset_gesture();
    if (failures) {
        printf("input_route: %d failures\n", failures);
        return 1;
    }
    printf("input_route: all tests passed\n");
    return 0;
}
