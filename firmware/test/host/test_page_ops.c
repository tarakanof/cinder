#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "controls_client.h"
#include "controls_view.h"
#include "day_view.h"
#include "ember_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input_route.h"
#include "lvgl.h"
#include "nowplaying_view.h"
#include "overlays.h"
#include "page_frame.h"
#include "page_fx.h"
#include "page_ops.h"
#include "pomo_client.h"
#include "pomo_view.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char s_log[512];
static void note(const char *fmt, ...)
{
    size_t n = strlen(s_log);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_log + n, sizeof s_log - n - 1, fmt, ap);
    va_end(ap);
    strcat(s_log, " ");
}
#define LOGGED(want) (strcmp(s_log, want) == 0)

static lv_obj_t s_top, s_objs[64];
lv_obj_t *lv_layer_top(void) { return &s_top; }

lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    for (int i = 0; i < 64; i++) {
        lv_obj_t *o = &s_objs[i];
        if (o->used) continue;
        memset(o, 0, sizeof *o);
        o->used = true;
        o->parent = parent;
        parent->kids[parent->n++] = o;
        return o;
    }
    return NULL;
}

static void obj_free(lv_obj_t *o)
{
    for (int i = 0; i < o->n; i++) obj_free(o->kids[i]);
    o->used = false;
}

void lv_obj_delete(lv_obj_t *o)
{
    lv_obj_t *p = o->parent;
    for (int i = 0; i < p->n; i++)
        if (p->kids[i] == o) {
            memmove(&p->kids[i], &p->kids[i + 1], (size_t)(p->n - i - 1) * sizeof p->kids[0]);
            p->n--;
            break;
        }
    obj_free(o);
}

void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)fmt;
    if (level == ESP_LOG_ERROR) note("log:%s", tag);
}

static void (*s_task)(void *);
static int s_tasks;
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, void *handle, int core)
{
    CHECK(!strcmp(name, "reset") && stack == 5120 && !arg && prio == 4 && !handle && core == 1, "the reset task keeps its name, stack, priority and core");
    s_task = fn;
    s_tasks++;
    return 1;
}
void vTaskDelay(uint32_t ticks) { note("delay%u", (unsigned)ticks); }

static esp_err_t s_erase = ESP_OK;
esp_err_t config_store_erase(void)
{
    note("erase");
    return s_erase;
}
const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "err";
}
void ember_client_forget_wifi(void) { note("forget"); }
void esp_restart(void) { note("restart"); }

static int s_counting;
void input_route_hold_counting(void) { s_counting++; }

static bool s_step_ok;
static int s_steps;
bool pages_step(pages_nav_t *nav, int steps)
{
    (void)nav;
    s_steps += steps;
    return s_step_ok;
}

static const char *s_day_label;
const char *day_view_hold_label(void) { return s_day_label; }
void page_day_frame(const page_frame_t *f) { (void)f; }

static double s_now;
double pomo_client_now(void) { return s_now; }
static pomo_snapshot_t s_pomo;
static bool s_pomo_ok;
bool pomo_client_get(pomo_snapshot_t *out)
{
    *out = s_pomo;
    return s_pomo_ok;
}
void pomo_client_action(pomo_input_t in) { note(in == POMO_INPUT_PUSH ? "push" : "long"); }
const char *pomo_client_note(const pomo_snapshot_t *snap, double now)
{
    (void)snap, (void)now;
    return "note";
}
void pomo_view_show(bool show) { note("pomo_show%d", show); }
void pomo_view_set_note(const char *n) { note("set_%s", n); }
void pomo_view_update(const pomo_est_t *e, double now) { note("update%d%d@%g", e->has_state, e->offline, now); }

static ctl_state_t s_ctl;
void controls_client_send(ctl_gesture_t g, int steps) { note("send%d:%d", (int)g, steps); }
void controls_client_get(ctl_state_t *out) { *out = s_ctl; }
void controls_view_show(bool on) { note("ctl_show%d", on); }
void controls_view_update(const char *target, const char *n) { note("ctl[%s|%s]", target, n); }

void np_view_show(bool show) { note("np_show%d", show); }
void np_view_input(double now) { note("input@%g", now); }
void np_view_turn(int detents, double now) { note("turn%d@%g", detents, now); }
void np_view_push(double now) { note("push@%g", now); }
void np_view_tap(int zone, double now) { note("tap%d@%g", zone, now); }
void np_view_skip(int steps, double now) { note("skip%d@%g", steps, now); }
void np_view_update(double now, bool offline, const char *age) { note("np%d[%s]@%g", offline, age, now); }
int ember_client_age_text(char *buf, size_t n)
{
    snprintf(buf, n, "5 min ago");
    return 0;
}

static lv_obj_t *top_with(int kids, int after)
{
    for (int i = after; i < s_top.n; i++)
        if (s_top.kids[i]->n == kids) return s_top.kids[i];
    return NULL;
}

static int s_last_y1;
static int drawn(lv_obj_t *o)
{
    lv_layer_t layer = {0};
    lv_event_t e = {.layer = &layer};
    o->draw(&e);
    s_last_y1 = layer.last_y1;
    return layer.rects;
}

static void test_page_go(lv_obj_t *wipe)
{
    pages_nav_t nav = {.n = 3, .pos = 1};
    s_step_ok = false;
    page_go(&nav, 1, 10);
    CHECK(s_steps == 1 && !wipe->invalid && !top_with(3, 3), "a step that stays: no wipe, no dots");
    CHECK(!drawn(wipe), "no wipe to draw");

    s_step_ok = true;
    page_go(&nav, 1, 10);
    lv_obj_t *dots = top_with(3, 3);
    CHECK(dots && wipe->invalid == 1, "a step starts the wipe and the dots");
    CHECK(dots && dots->kids[1]->bg == 0xFFFFFF && dots->kids[0]->bg == 0x4A4A4A, "the page's dot is white");
    CHECK(drawn(wipe) > 0, "the wipe draws");
    page_fx_frame(10.1);
    CHECK(wipe->invalid == 2, "the wipe invalidates the rows it moved over");
    CHECK(drawn(wipe) > 0, "and draws on");
    int forward_y1 = s_last_y1;
    CHECK(forward_y1 < 100, "a forward step wipes from the top, y1 %d", forward_y1);
    page_fx_frame(10.1);
    CHECK(wipe->invalid == 2, "no move, no invalidate");

    nav.pos = 2;
    page_go(&nav, 1, 10.5);
    CHECK(top_with(3, 3) == dots && dots->kids[2]->bg == 0xFFFFFF && dots->kids[1]->bg == 0x4A4A4A, "a second step recolours the same dots");
    page_fx_frame(11.49);
    CHECK(top_with(3, 3), "dots stay TS_DOTS_S after the last step");
    page_fx_frame(11.5);
    CHECK(!top_with(3, 3), "then they go");
    page_fx_frame(30);
    int inv = wipe->invalid;
    CHECK(!drawn(wipe), "a wipe that ended draws nothing");
    page_fx_frame(40);
    CHECK(wipe->invalid == inv, "and stops invalidating");

    nav.pos = 0;
    page_go(&nav, -1, 50);
    CHECK(wipe->invalid == inv + 1, "back wipes too");
    page_fx_frame(50.1);
    CHECK(drawn(wipe) > 0 && s_last_y1 > 100, "back wipes from the bottom, y1 %d", s_last_y1);
    nav.n = 2;
    dots_refresh(&nav);
    CHECK(!top_with(3, 3) && top_with(2, 3), "a new page list rebuilds the dots");
    page_fx_frame(50.5);
    CHECK(top_with(2, 3), "a rebuild keeps the time left");
    page_fx_frame(51);
    CHECK(!top_with(2, 3), "no more than that");
    page_go(&nav, -1, 60);
    page_fx_frame(60.5);
    nav.n = 1;
    dots_refresh(&nav);
    CHECK(!top_with(2, 3) && !top_with(1, 3), "one page: no dots");
    dots_refresh(&nav);
    page_fx_frame(90);
}

static void test_fx_pause(lv_obj_t *wipe)
{
    pages_nav_t nav = {.n = 3};
    s_step_ok = true;
    page_go(&nav, 1, 100);
    int inv = wipe->invalid;
    page_fx_pause();
    CHECK(!top_with(3, 3), "the OTA face drops the dots");
    CHECK(!drawn(wipe), "and stops the wipe");
    page_fx_frame(100.1);
    CHECK(wipe->invalid == inv, "which no longer moves");
    page_fx_pause();
}

static void test_reset(lv_obj_t *layer)
{
    lv_obj_t *arc = layer->kids[0], *label = layer->kids[1];
    CHECK(layer->hidden, "starts hidden");
    CHECK(!overlays_reset_frame(RG_IDLE, 3) && layer->hidden && !arc->arc, "idle stays hidden and leaves the arc");
    CHECK(!overlays_reset_frame(RG_ARMED, 5) && !layer->hidden && arc->arc == 5, "armed shows the progress");
    CHECK(!overlays_reset_frame(RG_ARMED, 6) && arc->arc == 6, "it follows the turn");
    CHECK(overlays_reset_frame(RG_IDLE, 0) && layer->hidden && arc->arc == 6, "closing says so once");
    CHECK(!overlays_reset_frame(RG_IDLE, 0), "only once");

    overlays_reset_frame(RG_ARMED, 7);
    overlays_pause();
    CHECK(layer->hidden, "the OTA face hides the screen");
    CHECK(!overlays_reset_frame(RG_IDLE, 0), "and forgets it, so no redraw is due");
    overlays_reset_frame(RG_ARMED, 7);
    CHECK(!layer->hidden, "it shows again on a new gesture");

    s_log[0] = 0;
    overlays_reset_frame(RG_CONFIRMED, 24);
    overlays_reset_frame(RG_CONFIRMED, 24);
    CHECK(s_tasks == 1 && !strcmp(label->text, "Resetting") && arc->arc == 24, "confirming starts one task");
    s_task(NULL);
    CHECK(LOGGED("delay300 erase forget restart "), "the task erases, forgets Wi-Fi, reboots: %s", s_log);
    s_log[0] = 0;
    s_erase = ESP_FAIL;
    s_task(NULL);
    CHECK(LOGGED("delay300 erase log:cinder forget restart "), "a failed erase still reboots, under the log tag cinder: %s", s_log);
    s_erase = ESP_OK;
    overlays_reset_frame(RG_IDLE, 0);
}

static void test_hold(lv_obj_t *layer)
{
    lv_obj_t *arc = layer->kids[0], *label = layer->kids[1];
    const page_desc_t day = {.frame = page_day_frame}, ctl = {.frame = page_controls_frame}, pomo = {.frame = page_pomo_frame},
                      np = {.frame = page_np_frame};
    CHECK(layer->hidden, "starts hidden");
    s_day_label = "clock out";
    overlays_hold_tick(&day, -1);
    CHECK(layer->hidden && !s_counting, "not held: nothing");
    overlays_hold_tick(&day, 400);
    CHECK(!layer->hidden && !arc->arc && !strcmp(label->text, "clock out") && s_counting == 1, "the ring opens with the day's label");
    overlays_hold_tick(&day, 950);
    CHECK(arc->arc == 50 && s_counting == 2, "it fills");
    overlays_hold_tick(&day, 5000);
    CHECK(arc->arc == 100, "up to 100");
    overlays_hold_tick(&day, -1);
    CHECK(layer->hidden && s_counting == 3, "letting go hides it");
    s_day_label = NULL;
    overlays_hold_tick(&day, 950);
    CHECK(layer->hidden && s_counting == 3, "a face without a long push shows none");

    overlays_hold_tick(&ctl, 950);
    CHECK(!layer->hidden && !strcmp(label->text, "Mac action"), "controls names the Mac action");
    overlays_hold_tick(&np, 950);
    CHECK(layer->hidden && s_counting == 4, "no label on another page");

    s_pomo_ok = true;
    pomo_clock_init(&s_pomo.clock);
    pomo_clock_sync(&s_pomo.clock, &(pomo_state_t){.phase = POMO_PHASE_IDLE}, 0);
    overlays_hold_tick(&pomo, 950);
    CHECK(layer->hidden && s_counting == 4, "an idle Pomodoro has nothing to stop");
    pomo_clock_sync(&s_pomo.clock, &(pomo_state_t){.phase = POMO_PHASE_FOCUS, .running = true, .remaining_sec = 600, .planned_sec = 1500}, 0);
    s_pomo_ok = false;
    overlays_hold_tick(&pomo, 950);
    CHECK(layer->hidden && s_counting == 4, "no Pomodoro data, no ring");
    s_pomo_ok = true;
    overlays_hold_tick(&pomo, 950);
    CHECK(!layer->hidden && !strcmp(label->text, "stop") && s_counting == 5, "a running Pomodoro stops");
    overlays_hold_tick(&pomo, -1);
}

static void test_pomo_page(void)
{
    s_log[0] = 0;
    page_pomo_show(true);
    page_pomo_input(&(page_input_t){.pushes = 2, .longs = 1, .detents = 3}, 0);
    CHECK(LOGGED("pomo_show1 push push long "), "pushes, then long pushes; a turn does nothing: %s", s_log);
    s_log[0] = 0;
    page_pomo_input(&(page_input_t){.longs = 2}, 0);
    CHECK(LOGGED("long long "), "each long push goes: %s", s_log);

    s_log[0] = 0;
    s_pomo.disabled = false;
    s_pomo.online = true;
    page_pomo_frame(&(page_frame_t){.t = 7});
    CHECK(LOGGED("set_note update10@7 "), "the note first, then the estimate, online: %s", s_log);
    s_log[0] = 0;
    s_pomo.online = false;
    page_pomo_frame(&(page_frame_t){.t = 7.5});
    CHECK(LOGGED("set_note update11@7.5 "), "an offline snapshot estimates offline: %s", s_log);
    s_log[0] = 0;
    s_pomo.disabled = true;
    page_pomo_frame(&(page_frame_t){.t = 8});
    CHECK(LOGGED("set_note update01@8 "), "Ember's Pomodoro off: no state: %s", s_log);
    s_pomo.disabled = false;
    s_pomo.online = true;
}

static void test_controls_page(void)
{
    s_log[0] = 0;
    page_controls_show(true);
    page_controls_input(&(page_input_t){.detents = 3}, 100);
    CHECK(LOGGED("ctl_show1 "), "a turn waits for the frame: %s", s_log);

    s_log[0] = 0;
    page_controls_input(&(page_input_t){.detents = -1, .pushes = 2, .longs = 1}, 100);
    CHECK(LOGGED("send2:2 send0:0 send0:0 send1:0 "), "the turn goes before the pushes, then the long push: %s", s_log);

    s_log[0] = 0;
    page_controls_input(&(page_input_t){.detents = 2}, 100);
    page_controls_input(&(page_input_t){.longs = 1}, 100);
    CHECK(LOGGED("send2:2 send1:0 "), "a long push alone flushes the turn first: %s", s_log);

    s_log[0] = 0;
    page_controls_input(&(page_input_t){.longs = 2}, 100);
    CHECK(LOGGED("send1:0 send1:0 "), "each long push goes: %s", s_log);

    s_log[0] = 0;
    page_controls_input(&(page_input_t){.detents = 2}, 100.01);
    page_controls_frame(&(page_frame_t){.t = 100.02});
    memcpy(s_ctl.target, "M4", 3);
    s_ctl.res = CTL_RES_SENT;
    s_ctl.at = 99;
    s_now = 100;
    page_controls_frame(&(page_frame_t){.t = 101});
    CHECK(LOGGED("ctl[|] send2:2 ctl[M4|Sent] "), "the frame holds the turn inside the wait, sends past it, then the view: %s", s_log);

    s_log[0] = 0;
    page_controls_frame(&(page_frame_t){.t = 102, .offline = true});
    CHECK(LOGGED("ctl[M4|Offline] "), "offline goes to the note: %s", s_log);

    s_log[0] = 0;
    page_controls_input(&(page_input_t){.detents = 2}, 103);
    page_controls_show(false);
    page_controls_frame(&(page_frame_t){.t = 200});
    CHECK(strstr(s_log, "send") == NULL, "leaving drops a batch: %s", s_log);
}

static void test_np_page(void)
{
    s_log[0] = 0;
    page_np_show(false);
    page_np_input(&(page_input_t){0}, 1);
    CHECK(LOGGED("np_show0 "), "no input, no wake: %s", s_log);

    s_log[0] = 0;
    page_input_t in = {.detents = 1, .pushes = 1, .hswipes = -1};
    in.taps[2] = 1;
    page_np_input(&in, 2);
    CHECK(LOGGED("input@2 turn1@2 push@2 tap2@2 skip-1@2 "), "input first, then turn, push, taps, skip: %s", s_log);

    s_log[0] = 0;
    page_np_input(&(page_input_t){.wakes = 1}, 3);
    page_np_input(&(page_input_t){.touches = 1}, 3);
    page_np_input(&(page_input_t){.longs = 1}, 3);
    CHECK(LOGGED("input@3 input@3 input@3 "), "a wake, a touch and a long push only note the input: %s", s_log);

    s_log[0] = 0;
    page_np_input(&(page_input_t){.hswipes = 2}, 3);
    page_np_input(&(page_input_t){.detents = -2}, 3);
    page_np_input(&(page_input_t){.pushes = 1}, 3);
    CHECK(LOGGED("input@3 skip2@3 input@3 turn-2@3 input@3 push@3 "), "a swipe, a turn and a push each note the input: %s", s_log);

    s_log[0] = 0;
    page_np_frame(&(page_frame_t){.t = 4});
    page_np_frame(&(page_frame_t){.t = 5, .offline = true});
    CHECK(LOGGED("np0[]@4 np1[5 min ago]@5 "), "the age only offline: %s", s_log);
}

int main(void)
{
    page_fx_create();
    lv_obj_t *wipe = s_top.kids[0];
    overlays_create();
    CHECK(s_top.n == 3, "wipe, reset, hold on the top layer, in that order");
    lv_obj_t *reset = s_top.kids[1], *hold = s_top.kids[2];
    test_page_go(wipe);
    test_fx_pause(wipe);
    test_reset(reset);
    test_hold(hold);
    test_pomo_page();
    test_controls_page();
    test_np_page();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("page ops: ok\n");
    return 0;
}
