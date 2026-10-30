#include "input_route.h"

#include <stdatomic.h>
#include <stdint.h>

#include "bsp_knob_15_md50et.h"
#include "dim_ctl.h"
#include "ember_client.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "iot_knob.h"
#include "ota_client.h"
#include "press_route.h"
#include "touch_swipe.h"

/* The iot_knob "left" event fires on a clockwise turn (docs/llm.md). */
#define KNOB_EVENT_CLOCKWISE KNOB_LEFT

static atomic_int s_detents;
static atomic_int s_pushes;
static atomic_uint s_touches;
static atomic_int s_long_pushes;
static atomic_int s_page_steps;
static atomic_int s_swipes;
static atomic_int s_hswipes;
static atomic_int s_np_wakes;
enum { PRESS_OPEN, PRESS_FIRED, PRESS_RELEASED, PRESS_TURNED };

static atomic_bool s_hold_seen;
static _Atomic long long s_press_at_us;
static atomic_int s_press_claim;
static atomic_bool s_press_counting;
static atomic_bool s_button_down;
static atomic_bool s_touch_down;
static atomic_bool s_turned_while_down;

static reset_gesture_t s_rg;
static portMUX_TYPE s_rg_mux = portMUX_INITIALIZER_UNLOCKED;

static EXT_RAM_BSS_ATTR ts_touch_t s_touch;
static pr_wake_t s_wake_press, s_wake_touch;

static double now_s(void) { return esp_timer_get_time() / 1e6; }

void input_route_init(void) { rg_init(&s_rg); }

void input_route_inject(int kind, int n)
{
    if (ember_client_idle_input(kind == 3 || kind >= 5 ? DIM_WAKE_TOUCH : DIM_WAKE_ENCODER)) return;
    switch (kind) {
    case 0: atomic_fetch_add(&s_detents, n); break;
    case 1: atomic_fetch_add(&s_pushes, n); break;
    case 2: atomic_fetch_add(&s_long_pushes, n); break;
    case 3: atomic_fetch_add(&s_touches, ts_tap_word(TS_ZONE_NONE, n)); break;
    case 4: atomic_fetch_add(&s_page_steps, n); break;
    case 5: atomic_fetch_add(&s_swipes, n); break;
    case 6: atomic_fetch_add(&s_swipes, -n); break;
    case 7: atomic_fetch_add(&s_hswipes, n); break;
    case 8: atomic_fetch_add(&s_hswipes, -n); break;
    case 9:
    case 10:
    case 11:
        atomic_fetch_add(&s_touches, ts_tap_word((ts_zone_t)(TS_ZONE_PREV + kind - 9), n));
        break;
    default: break;
    }
}

void input_route_knob_cb(void *event)
{
    knob_event_t ev = (knob_event_t)(intptr_t)event;
    int dir = ev == KNOB_EVENT_CLOCKWISE ? 1 : -1;
    ota_client_note_input();
    if (ember_client_idle_input(DIM_WAKE_ENCODER) || pr_wake_held(&s_wake_press)) return;
    taskENTER_CRITICAL(&s_rg_mux);
    bool reset_turn = rg_turn(&s_rg, dir, now_s());
    taskEXIT_CRITICAL(&s_rg_mux);
    switch (pr_turn(reset_turn, atomic_load(&s_button_down))) {
    case PR_TURN_PAGE: {
        int open = PRESS_OPEN;
        if (!atomic_compare_exchange_strong(&s_press_claim, &open, PRESS_TURNED) && open != PRESS_TURNED) break;
        atomic_store(&s_turned_while_down, true);
        atomic_fetch_add(&s_page_steps, dir);
        break;
    }
    case PR_TURN_DETENT:
        atomic_fetch_add(&s_detents, dir);
        break;
    default:
        break;
    }
}

void input_route_button_cb(void *event)
{
    ota_client_note_input();
    button_event_t ev = (button_event_t)(intptr_t)event;
    bool woke = ember_client_idle_input(DIM_WAKE_ENCODER);
    if (pr_wake_swallow(&s_wake_press, ev == BUTTON_PRESS_DOWN, ev == BUTTON_PRESS_UP, woke)) return;
    switch (ev) {
    case BUTTON_PRESS_DOWN:
        taskENTER_CRITICAL(&s_rg_mux);
        rg_press(&s_rg, now_s());
        taskEXIT_CRITICAL(&s_rg_mux);
        atomic_store(&s_hold_seen, false);
        atomic_store(&s_turned_while_down, false);
        atomic_store(&s_press_at_us, esp_timer_get_time());
        atomic_store(&s_press_claim, PRESS_OPEN);
        atomic_store(&s_press_counting, false);
        atomic_store(&s_button_down, true);
        break;
    case BUTTON_LONG_PRESS_HOLD:
        atomic_store(&s_hold_seen, true);
        break;
    case BUTTON_PRESS_UP: {
        taskENTER_CRITICAL(&s_rg_mux);
        bool reset_press = rg_release(&s_rg, now_s());
        taskEXIT_CRITICAL(&s_rg_mux);
        atomic_store(&s_button_down, false);
        int open = PRESS_OPEN;
        bool fired = !atomic_compare_exchange_strong(&s_press_claim, &open, PRESS_RELEASED);
        pr_press_t press = pr_release(reset_press, atomic_load(&s_turned_while_down), atomic_load(&s_hold_seen), fired,
                                      atomic_load(&s_press_counting));
        if (press != PR_PRESS_NONE) atomic_fetch_add(press == PR_PRESS_LONG ? &s_long_pushes : &s_pushes, 1);
        break;
    }
    default:
        break;
    }
}

void input_route_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED && dim_ctl_off()) code = LV_EVENT_PRESS_LOST;
    bool woke = code == LV_EVENT_PRESSED && ember_client_idle_input(DIM_WAKE_TOUCH);
    if (code == LV_EVENT_PRESSED) ota_client_note_input();
    if (pr_wake_swallow(&s_wake_touch, code == LV_EVENT_PRESSED, code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST, woke)) return;
    lv_point_t p = {0};
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &p);
    double now = now_s(), report = bsp_knob_15_md50et_touch_report_us() / 1e6;
    ts_result_t r = TS_NONE;
    atomic_store(&s_touch_down, code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING);
    if (code == LV_EVENT_PRESSED) {
        atomic_fetch_add(&s_np_wakes, 1);
        bool classify = input_route_classify_touch();
        r = ts_press(&s_touch, p.x, p.y, report, classify);
    } else if (code == LV_EVENT_PRESSING) {
        ts_move(&s_touch, p.x, p.y, report, now);
    } else if (code == LV_EVENT_RELEASED) {
        r = ts_release(&s_touch, p.x, p.y, report, now);
    } else {
        ts_cancel(&s_touch);
    }
    if (r == TS_TAP) {
        atomic_fetch_add(&s_touches, ts_tap_word(ts_tap_zone(s_touch.x0, s_touch.y0), 1));
    } else if (r == TS_LEFT || r == TS_RIGHT) {
        atomic_fetch_add(&s_hswipes, r == TS_LEFT ? 1 : -1);
    } else if (r != TS_NONE) {
        atomic_fetch_add(&s_swipes, r == TS_NEXT ? 1 : -1);
    }
}

void input_route_drop(void)
{
    atomic_store(&s_page_steps, 0);
    atomic_store(&s_swipes, 0);
    atomic_store(&s_hswipes, 0);
    atomic_store(&s_np_wakes, 0);
    atomic_store(&s_detents, 0);
    atomic_store(&s_pushes, 0);
    atomic_store(&s_touches, 0);
    atomic_store(&s_long_pushes, 0);
}

bool input_route_pending(void)
{
    return atomic_load(&s_detents) || atomic_load(&s_pushes) || atomic_load(&s_touches) || atomic_load(&s_long_pushes) ||
           atomic_load(&s_page_steps) || atomic_load(&s_swipes) || atomic_load(&s_hswipes) || atomic_load(&s_np_wakes);
}

bool input_route_held(void) { return atomic_load(&s_button_down) || atomic_load(&s_touch_down); }

static long long hold_step(void)
{
    bool down = atomic_load(&s_button_down), turned = atomic_load(&s_turned_while_down);
    bool wake = pr_wake_held(&s_wake_press);
    long long held = (esp_timer_get_time() - atomic_load(&s_press_at_us)) / 1000;
    bool fired = atomic_load(&s_press_claim) != PRESS_OPEN;
    if (!wake && pr_hold_fires(down, turned, fired, held)) {
        int open = PRESS_OPEN;
        if (atomic_compare_exchange_strong(&s_press_claim, &open, PRESS_FIRED)) atomic_fetch_add(&s_long_pushes, 1);
        fired = true;
    }
    return down && !turned && !fired && !wake && held >= PR_COUNTDOWN_MS ? held : -1;
}

void input_route_hold_counting(void) { atomic_store(&s_press_counting, true); }

long long input_route_take(page_input_t *out)
{
    out->detents = atomic_exchange(&s_detents, 0);
    out->pushes = atomic_exchange(&s_pushes, 0);
    out->touches = ts_tap_counts(atomic_exchange(&s_touches, 0), out->taps);
    long long held = hold_step();
    out->longs = atomic_exchange(&s_long_pushes, 0);
    out->wakes = atomic_exchange(&s_np_wakes, 0);
    out->hswipes = atomic_exchange(&s_hswipes, 0);
    return held;
}

void input_route_take_steps(int *steps, int *swipes)
{
    *steps = atomic_exchange(&s_page_steps, 0);
    *swipes = atomic_exchange(&s_swipes, 0);
}

rg_state_t input_route_reset_tick(double t, int *progress)
{
    taskENTER_CRITICAL(&s_rg_mux);
    rg_state_t st = rg_tick(&s_rg, t);
    *progress = s_rg.progress;
    taskEXIT_CRITICAL(&s_rg_mux);
    return st;
}

void input_route_reset_clear(void)
{
    taskENTER_CRITICAL(&s_rg_mux);
    rg_init(&s_rg);
    taskEXIT_CRITICAL(&s_rg_mux);
}
