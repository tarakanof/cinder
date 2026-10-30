#include <math.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "bot_view.h"
#include "chase_ctl.h"
#include "ember_client.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "input_route.h"
#include "moment_view.h"
#include "moments.h"
#include "music_view.h"
#include "nowplaying_client.h"
#include "page_stubs.h"
#include "scene.h"
#include "temp_face.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NOW 1782043200LL
#define DT (1.0 / 60)
#define SEED 0x454D4252ULL
#define SCENE_START_S 3.0
#define SCENE_NEXT_S 1.5

void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...) { (void)level, (void)tag, (void)fmt; }

static double T = 100;
static bot_t bot, ref;
static pages_nav_t nav;
static knob_settings_t ks;
static char calls[256];

static void cue_apply(bot_t *b, const wxm_cue_t *c, double t);

static void note(const char *what)
{
    size_t n = strlen(calls);
    snprintf(calls + n, sizeof calls - n, "%s ", what);
}

static struct {
    ember_demo_status_t st;
    bool timed, have, late_offer, offer_on_input;
    demo_block_t block;
    int inputs, acts;
    unsigned act_gen;
} D;

void ember_client_demo_status(ember_demo_status_t *out) { *out = D.st; }

bool ember_client_demo_take(demo_block_t *out, unsigned *gen)
{
    if (D.late_offer) {
        D.late_offer = false;
        D.have = true;
        D.st.gen++;
    }
    if (!D.have) return false;
    D.have = false;
    *out = D.block;
    *gen = D.st.gen;
    return true;
}

bool ember_client_demo_tick(double t)
{
    (void)t;
    note("tick");
    bool timed = D.timed;
    if (timed) {
        D.st.playing = false;
        D.st.gen++;
    }
    D.timed = false;
    return timed;
}

bool ember_client_demo_input(void)
{
    D.inputs++;
    bool was = D.st.playing;
    if (was) D.st.gen++;
    D.st.playing = D.offer_on_input;
    return was;
}

void ember_client_demo_act_done(unsigned gen)
{
    D.acts++;
    D.act_gen = gen;
}

static struct {
    bool busy, ok, held;
    double tau;
    int starts, stops, aborts, releases;
    wxm_moment_t last;
} MV;

bool moment_view_start(const wxm_moment_t *m, double t)
{
    (void)t;
    MV.starts++;
    if (!MV.ok) return false;
    MV.last = *m;
    MV.busy = true;
    return true;
}
double moment_view_poll(double t)
{
    (void)t;
    return MV.tau;
}
void moment_view_abort(double t)
{
    (void)t;
    MV.aborts++;
    MV.busy = false;
    MV.tau = -1;
}
void moment_view_stop(void)
{
    MV.stops++;
    MV.busy = false;
    MV.tau = -1;
}
bool moment_view_busy(void) { return MV.busy; }
void moment_view_release(void) { MV.releases++; }

static struct {
    bool busy, ok;
    double tau;
    int starts, stops, aborts;
} MU;

bool music_view_start(double t)
{
    (void)t;
    MU.starts++;
    if (!MU.ok) return false;
    MU.busy = true;
    return true;
}
double music_view_poll(double t)
{
    (void)t;
    return MU.tau;
}
void music_view_abort(double t)
{
    (void)t;
    MU.aborts++;
    MU.busy = false;
    MU.tau = -1;
}
void music_view_stop(void)
{
    MU.stops++;
    MU.busy = false;
    MU.tau = -1;
}
bool music_view_busy(void) { return MU.busy; }

static np_snapshot_t np;
static bool np_new;

bool np_client_get(np_snapshot_t *out, uint32_t *gen)
{
    (*gen)++;
    bool fresh = np_new;
    np_new = false;
    if (fresh) *out = np;
    return fresh;
}

static void np_set(const char *title, np_play_t state, bool demo)
{
    np = (np_snapshot_t){.have = title != NULL, .demo = demo};
    np.info.state = state;
    snprintf(np.info.title, sizeof np.info.title, "%s", title ? title : "");
    np_new = true;
}

static int face_pick, face_picks;
int temp_face_picked(void) { return face_pick; }
void temp_face_pick(int face)
{
    face_pick = face;
    face_picks++;
}

static bool glint_forced;
void bot_view_glint_force(bool on) { glint_forced = on; }

static int wipes, wipe_dir;
void wipe_start(int dir, double t)
{
    (void)t;
    wipes++;
    wipe_dir = dir;
}

static bool in_pending, in_held;
static int drops;
bool input_route_pending(void) { return in_pending; }
void input_route_drop(void)
{
    note("drop");
    drops++;
    in_pending = false;
}
bool input_route_held(void) { return in_held; }

static struct {
    TaskFunction_t fn;
    const char *name;
    uint32_t stack;
    UBaseType_t prio;
    BaseType_t core;
    int deletes;
    bool fail;
} TK;
static jmp_buf task_end;
static int task_handle;

BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                                           TaskHandle_t *handle, BaseType_t core, unsigned caps)
{
    (void)arg, (void)caps;
    TK.fn = fn, TK.name = name, TK.stack = stack, TK.prio = prio, TK.core = core;
    if (TK.fail) return pdFAIL;
    *handle = &task_handle;
    return pdPASS;
}
void vTaskDelay(uint32_t ticks) { (void)ticks; }
void vTaskSuspend(TaskHandle_t task)
{
    (void)task;
    longjmp(task_end, 1);
}
void vTaskDeleteWithCaps(TaskHandle_t task)
{
    CHECK(task == &task_handle, "the orbit task is the one deleted");
    TK.deletes++;
}

static wx_obs_t obs(const char *code, float temp, int rain)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = temp, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .moon_up = -1, .observed_at = NOW - 300, .has_obs_age = true, .obs_age_s = 300,
                  .has_utc_offset = true, .utc_offset = 3600, .has_now_rain = true, .has_rain = true, .has_night = true,
                  .has_wind = true, .wind_kmh = 10, .now_rain = rain};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.condition, sizeof o.condition, "clouds");
    snprintf(o.code, sizeof o.code, "%s", code);
    return o;
}

static void fresh(void)
{
    if (scene_on()) scene_end(T);
    scene_resume_cancel();
    moment_unact(T);
    music_unact(T);
    T += 10000;
    memset(&D, 0, sizeof D);
    memset(&MV, 0, sizeof MV);
    memset(&MU, 0, sizeof MU);
    MV.ok = MU.ok = true;
    MV.tau = MU.tau = -1;
    memset(&TK, 0, sizeof TK);
    calls[0] = 0;
    face_pick = -1;
    face_picks = 0;
    glint_forced = false;
    wipes = wipe_dir = 0;
    in_pending = in_held = false;
    drops = 0;
    np_set(NULL, NP_NONE, false);
    page_log_reset();
    nav = (pages_nav_t){.order = {0, 1, 2}, .n = 3};
    ks = (knob_settings_t){.weather_moments = true, .weather_every_min = 1};
    bot_init(&bot, SEED, T);
    bot_init(&ref, SEED, T);
    chase_ctl_init(0x1234);
    moments_init(&(moments_ctx_t){.bot = &bot, .ks = &ks});
    scene_init(&(scene_ctx_t){.bot = &bot, .nav = &nav});
    while (scene_gate(T, false)) T += 1;
}

static gc_out_t chase_frame(bool glint, bool calm)
{
    chase_ctl_in_t in = {.t = T, .glint = glint, .calm = calm, .scene = scene_on(), .scene_req = scene_chase_take(), .eyes = BOT_EYES_DASH};
    return chase_ctl_frame(&bot, &in);
}

static void offer(demo_page_t page, int face, demo_act_t act)
{
    D.block = (demo_block_t){.present = true, .page = page, .face = face, .act = act};
    D.have = true;
    D.st.playing = true;
    D.st.gen++;
}

static void step(double s) { T += s; }

static void poses_same(const char *what)
{
    bot_pose_t a = bot_pose(&bot, T), b = bot_pose(&ref, T);
    CHECK(a.gaze_x == b.gaze_x && a.gaze_y == b.gaze_y && a.lid_l == b.lid_l && a.lid_r == b.lid_r && a.eyes == b.eyes &&
              a.hop_dy == b.hop_dy && a.blink_l == b.blink_l,
          "%s: pose (%.4f, %.4f) lid %.3f eyes %d against (%.4f, %.4f) lid %.3f eyes %d", what, a.gaze_x, a.gaze_y, a.lid_l,
          (int)a.eyes, b.gaze_x, b.gaze_y, b.lid_l, (int)b.eyes);
    CHECK(bot.acting == ref.acting, "%s: acting %d against %d", what, bot.acting, ref.acting);
}

static void poses_same_for(const char *what, double s)
{
    for (double end = T + s; T < end; T += DT) poses_same(what);
}

static void music_tick(bool calm, bool offline, bool page, bool on) { moments_music_tick(T, calm, offline, page, on); }

static double music_run(bool calm, bool offline, bool page, bool on, double s)
{
    for (double end = T + s; T < end; T += 0.25) {
        music_tick(calm, offline, page, on);
        if (MU.starts) return T;
    }
    return -1;
}

static void music_owed(void)
{
    np_set("A", NP_PLAYING, false);
    music_tick(false, false, true, true);
    step(0.1);
    np_set("B", NP_PLAYING, false);
}

static void music_started(void)
{
    music_owed();
    double t0 = T, at = music_run(false, false, true, true, 8);
    CHECK(at >= t0 + MM_SETTLE_S, "the track's moment starts after the settle, at %.2f", at - t0);
    CHECK(MU.busy && bot.acting, "with the acting");
}

static void test_page_and_face(void)
{
    fresh();
    CHECK(!scene_on() && scene_n() == 0, "no scene yet");
    temp_face_pick(3);
    face_picks = 0;
    scene_frame(T);
    CHECK(!scene_on() && wipes == 0, "nothing offered, nothing starts");

    offer(DEMO_PAGE_WEATHER, 5, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    CHECK(scene_on() && scene_n() == 1, "the scene starts on the take");
    CHECK(strcmp(PAGES[nav.page].id, "weather") == 0, "the scene's page shows, not %s", PAGES[nav.page].id);
    CHECK(nav.pos == 0 && strstr(page_log, "weather+"), "pages_show keeps the user's position: %s", page_log);
    CHECK(wipes == 1 && wipe_dir == 1, "the page change wipes forward");
    CHECK(face_pick == 5 && face_picks == 1, "the scene's face is picked");
    CHECK(scene_resuming() == false, "a new scene clears the hold");

    scene_frame(T);
    CHECK(scene_on() && face_picks == 1 && wipes == 1, "a second frame changes nothing");

    step(1);
    D.timed = true;
    scene_frame(T);
    CHECK(!scene_on(), "the scene ends when the demo stops playing");
    CHECK(face_pick == 3 && face_picks == 2, "the user's face comes back, got %d", face_pick);
    CHECK(scene_resuming() && strcmp(PAGES[nav.page].id, "weather") == 0, "a natural end holds the page");

    step(SCENE_NEXT_S - 0.1);
    scene_frame(T);
    CHECK(scene_resuming() && wipes == 1, "the page is held until SCENE_NEXT_S");
    step(0.2);
    scene_frame(T);
    CHECK(!scene_resuming() && nav.page == 0 && wipes == 2 && wipe_dir == -1, "then the user's page comes back with a wipe back");
    scene_frame(T);
    CHECK(wipes == 2, "once");

    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    CHECK(scene_on() && scene_n() == 2 && wipes == 2 && face_picks == 2, "a scene on the shown page wipes nothing, -1 leaves the face");
    unsigned gen = D.st.gen;
    D.st.playing = false;
    D.st.gen++;
    step(1);
    scene_frame(T);
    CHECK(!scene_on() && !scene_resuming() && D.st.gen == gen + 1, "a server stop on the user's page returns at once");
    CHECK(wipes == 2, "and wipes nothing: the page never changed");

    offer(DEMO_PAGE_POMODORO, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    offer(DEMO_PAGE_WEATHER, 2, (demo_act_t){.kind = DEMO_ACT_NONE});
    int before = wipes;
    scene_frame(T);
    CHECK(scene_on() && scene_n() == 4, "a new id replaces a playing scene");
    CHECK(strcmp(PAGES[nav.page].id, "weather") == 0 && wipes == before + 1, "and moves on without the user's page between");
    CHECK(!scene_resuming(), "the old scene's hold is dropped");
    int page = nav.page;
    step(SCENE_NEXT_S + 1);
    scene_frame(T);
    CHECK(nav.page == page, "so the user's page does not cut into the new scene");
    D.st.gen++;
    step(1);
    scene_frame(T);
    CHECK(!scene_on(), "a gen that moved ends the scene, playing or not");

    fresh();
    temp_face_pick(3);
    offer(DEMO_PAGE_WEATHER, 5, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    D.block = (demo_block_t){.present = true, .page = DEMO_PAGE_WEATHER, .face = -1};
    D.late_offer = true;
    step(1);
    scene_frame(T);
    CHECK(scene_on() && scene_n() >= 2 && face_pick == 3, "an offer that lands after the status read still ends the old scene first, face %d", face_pick);
}

static void test_stops_moments(void)
{
    fresh();
    wx_obs_t o = obs("61", 9, 2);
    static wx_temp_t scratch;
    static wxm_moment_t m;
    CHECK(wxm_moment_from(WXM_RAIN, &o, false, &scratch, &m), "a rain moment");
    CHECK(moment_view_start(&m, T), "starts");
    moments_cue_start(&m, T);
    bot_act_begin(&ref, T);
    MU.busy = true;
    step(0.1);
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    CHECK(!MV.busy && MV.stops == 1 && MV.aborts == 0, "a scene stops a running weather moment at once");
    CHECK(!MU.busy && MU.stops == 1 && MU.aborts == 0, "and a music moment");
    bot_act_end(&ref, true, 0.3, T);
    poses_same_for("the acting ends", 0.5);

    fresh();
    music_started();
    bot_act_begin(&ref, T);
    step(0.1);
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    CHECK(!MU.busy && MU.stops == 1 && !bot.acting, "a music moment's acting ends with it");
    bot_act_end(&ref, true, 0.3, T);
    poses_same_for("the music acting ends", 0.5);

    fresh();
    wxm_moment_from(WXM_RAIN, &o, false, &scratch, &m);
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_MOMENT, .moment = WXM_RAIN});
    scene_frame(T);
    scene_act(T, &o, true);
    CHECK(MV.starts == 1 && bot.acting, "the scene's moment acts");
    bot_act_begin(&ref, T);
    step(0.1);
    D.st.playing = false;
    D.st.gen++;
    scene_frame(T);
    CHECK(MV.aborts == 1 && MV.stops == 0 && !bot.acting, "ending the scene cuts its moment short");
    bot_act_end(&ref, true, 0.3, T);
    poses_same_for("the cut acting", 0.5);
}

static void test_moment_scene(void)
{
    wx_obs_t o = obs("61", 9, 2);
    demo_act_t act = {.kind = DEMO_ACT_MOMENT, .moment = WXM_RAIN};

    fresh();
    offer(DEMO_PAGE_BOT, -1, act);
    scene_frame(T);
    MV.ok = false;
    for (double end = T + SCENE_START_S - 0.1; T < end; T += DT) scene_act(T, &o, true);
    CHECK(D.acts == 0 && MV.starts > 1, "it keeps trying while the view is busy");
    step(0.2);
    scene_act(T, &o, true);
    CHECK(D.acts == 1 && D.act_gen == D.st.gen, "a moment that cannot start finishes after SCENE_START_S");
    CHECK(!bot.acting, "without acting");
    int starts = MV.starts;
    step(0.1);
    scene_act(T, &o, true);
    CHECK(D.acts == 1 && MV.starts == starts, "and stays done");

    fresh();
    offer(DEMO_PAGE_POMODORO, -1, act);
    scene_frame(T);
    scene_act(T, &o, false);
    CHECK(D.acts == 1 && MV.starts == 0, "off the bot page it is done at once");

    fresh();
    offer(DEMO_PAGE_BOT, -1, act);
    scene_frame(T);
    scene_act(T, NULL, true);
    CHECK(D.acts == 1 && MV.starts == 0, "without weather data too");

    fresh();
    offer(DEMO_PAGE_BOT, -1, act);
    scene_frame(T);
    scene_act(T, &o, true);
    CHECK(MV.starts == 1 && MV.last.kind == WXM_RAIN && !MV.last.quiet && D.acts == 0, "the act plays the scene's moment");
    int n;
    const wxm_cue_t *c = wxm_script(&MV.last, &n);
    bot_act_begin(&ref, T);
    MV.tau = -1;
    step(DT);
    scene_act(T, &o, true);
    poses_same("before the moment plays");
    MV.tau = c[0].t + 0.001;
    step(DT);
    scene_act(T, &o, true);
    cue_apply(&ref, &c[0], T);
    CHECK(D.acts == 0, "busy, not done");
    poses_same_for("the scene's moment runs the same cue loop", 0.2);
    MV.busy = false;
    MV.tau = -1;
    step(DT);
    scene_act(T, &o, true);
    CHECK(D.acts == 1, "the moment ending finishes the act");

    fresh();
    act.quiet = true;
    offer(DEMO_PAGE_BOT, -1, act);
    scene_frame(T);
    scene_act(T, &o, true);
    CHECK(MV.starts == 1 && MV.last.quiet && !bot.acting, "a quiet moment has no eye acting");
}

static void test_chase_scene(void)
{
    fresh();
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_CHASE, .chase_style = GC_STYLE_HALF});
    scene_frame(T);
    CHECK(glint_forced, "a chase scene forces the glint");
    CHECK(scene_on(), "on");
    CHECK(chase_ctl_request(GC_STYLE_FULL, 0, 0) == 1, "the USB op is busy from the scene's start");
    scene_act(T, NULL, true);
    gc_out_t c = chase_frame(true, false);
    CHECK(c.chasing && c.style == GC_STYLE_HALF, "the scene's own request starts its chase");
    CHECK(chase_ctl_busy(), "the chase runs");
    step(DT);
    scene_act(T, NULL, true);
    chase_frame(true, false);
    CHECK(D.acts == 0, "not done while it runs");
    int req = scene_chase_take();
    CHECK(req == -1, "the request goes once, got %d", req);
    for (double end = T + 30; T < end && D.acts == 0; T += DT) {
        scene_act(T, NULL, true);
        chase_frame(true, false);
    }
    CHECK(D.acts == 1, "the chase ending finishes the act");
    step(1);
    D.st.playing = false;
    D.st.gen++;
    scene_frame(T);
    CHECK(!glint_forced, "the glint is let go at the scene's end");

    fresh();
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_CHASE});
    scene_frame(T);
    scene_act(T, NULL, true);
    chase_frame(true, false);
    for (int i = 0; i < 10; i++, T += DT) chase_frame(true, false);
    CHECK(bot.tracking, "mid-chase the eyes track");
    D.st.playing = false;
    D.st.gen++;
    scene_frame(T);
    CHECK(!bot.tracking, "a scene ending mid-chase stops the tracking");

    fresh();
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_CHASE});
    scene_frame(T);
    scene_act(T, NULL, true);
    chase_frame(true, false);
    step(DT);
    scene_act(T, NULL, true);
    chase_frame(false, false);
    step(DT);
    CHECK(!chase_ctl_busy(), "the glint goes, the chase with it");
    scene_act(T, NULL, true);
    CHECK(D.acts == 1, "a chase cut short finishes the act at once, not at SCENE_START_S");
    CHECK(scene_chase_take() == -1, "and asks for no second one");

    fresh();
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_CHASE});
    scene_frame(T);
    for (double end = T + SCENE_START_S - 0.1; T < end; T += DT) {
        scene_act(T, NULL, true);
        chase_frame(false, false);
    }
    CHECK(D.acts == 0 && !chase_ctl_busy(), "no glint, no chase, still waiting");
    step(0.2);
    scene_act(T, NULL, true);
    CHECK(D.acts == 1, "a chase without the glint counts as done after SCENE_START_S");

    fresh();
    bot_set_mood(&bot, BOT_WORKING, T);
    chase_frame(true, false);
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    CHECK(!glint_forced, "other scenes leave the glint alone");
    scene_act(T, NULL, true);
    chase_frame(true, false);
    CHECK(chase_ctl_request(GC_STYLE_FULL, 0, 0) == 1, "busy for every scene");
    CHECK(chase_ctl_sample().phase == GC_IDLE, "held");
    step(GC_WAIT_MAX_S + 1);
    scene_act(T, NULL, true);
    chase_frame(true, false);
    CHECK(!chase_ctl_busy(), "the working time runs out during the scene, and still no chase");
}

static void test_real_chase_survives(void)
{
    fresh();
    bot_set_mood(&bot, BOT_WORKING, T);
    chase_frame(true, false);
    CHECK(chase_ctl_request(GC_STYLE_FULL, 32, 3) == 0, "a request is taken: %d", chase_ctl_request(GC_STYLE_FULL, 32, 3));
    CHECK(chase_ctl_fps() == 32, "its fps is kept");
    CHECK(chase_ctl_request(GC_STYLE_FULL, 60, 3) == 0 && chase_ctl_fps() == 60, "and set back");
    step(DT);
    gc_out_t c = chase_frame(true, false);
    CHECK(c.chasing && chase_ctl_busy(), "the requested chase starts next frame");
    chase_ctl_sample_t s0 = chase_ctl_sample();
    CHECK(s0.phase == GC_CHASE && s0.laps == 3 && s0.fps == 60, "sample: phase %d laps %d fps %d", s0.phase, s0.laps, s0.fps);
    CHECK(chase_ctl_request(GC_STYLE_FULL, 0, 0) == 1 || !chase_ctl_busy(), "busy");
    step(DT);
    chase_frame(true, false);
    CHECK(chase_ctl_request(GC_STYLE_FULL, 0, 0) == 1, "a chase answers busy");
    step(4);
    chase_frame(true, false);
    double scene_at = T;
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    CHECK(!chase_ctl_busy(), "a scene pauses the chase");
    chase_ctl_sample_t held = chase_ctl_sample();
    CHECK(held.work_s == s0.work_s && held.wait_s == s0.wait_s, "and keeps its working time");
    step(60);
    D.st.playing = false;
    D.st.gen++;
    scene_frame(T);
    CHECK(chase_ctl_busy() && chase_ctl_sample().phase == GC_CHASE, "the chase comes back");
    c = chase_frame(true, false);
    CHECK(c.chasing, "and goes on as if no time had passed");
    T = T + (9 - 4 - DT * 2) - 0.2;
    c = chase_frame(true, false);
    CHECK(c.chasing, "its last lap still runs after %.1f s of scene", T - scene_at);
    step(0.4);
    c = chase_frame(true, false);
    CHECK(!c.chasing && chase_ctl_sample().phase == GC_REST, "and ends on time, phase %d", chase_ctl_sample().phase);
}

static void test_half_chase(void)
{
    fresh();
    bot_set_mood(&bot, BOT_WORKING, T);
    chase_frame(true, false);
    CHECK(chase_ctl_request(GC_STYLE_HALF, 0, 2) == 0, "taken");
    bool tracked = false, stopped = false;
    for (double end = T + 6; T < end; T += DT) {
        gc_out_t c = chase_frame(true, false);
        CHECK(c.chasing && c.style == GC_STYLE_HALF, "two laps of the half chase");
        tracked |= bot.tracking;
        stopped |= tracked && !bot.tracking;
        bot_pose(&bot, T);
    }
    CHECK(tracked && stopped, "the eyes follow, then stop to snap: %d %d", tracked, stopped);
    for (double end = T + 1; T < end; T += DT) chase_frame(true, false);
    CHECK(!bot.tracking && !chase_frame(true, false).chasing, "tracking ends with the chase");
}

static void test_calm_and_glint_gates(void)
{
    fresh();
    bot_set_mood(&bot, BOT_WORKING, T);
    chase_frame(false, false);
    CHECK(chase_ctl_request(GC_STYLE_FULL, 0, 0) == 2, "no glint answers not working");
    chase_frame(true, false);
    CHECK(chase_ctl_request(GC_STYLE_FULL, 0, 0) == 0, "with it, ready");
    step(DT);
    gc_out_t c = chase_frame(true, true);
    CHECK(!c.chasing, "calm starts no chase");
}

static void test_sleepy_scene(void)
{
    fresh();
    bot_set_sleep_after(&bot, 600);
    bot_set_mood(&bot, BOT_SLEEPY, T);
    bot_sleep_in(&bot, 100, T);
    double left = bot_asleep_in(&bot, T);
    CHECK(fabs(left - 100) < 1e-6, "asleep in 100 s, got %.2f", left);
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_SLEEPY});
    scene_frame(T);
    CHECK(isinf(bot_asleep_in(&bot, T)), "the timer stops for the scene");
    step(50);
    bot_pose(&bot, T);
    CHECK(!bot.asleep, "no sleep during the scene");
    D.st.playing = false;
    D.st.gen++;
    scene_frame(T);
    CHECK(fabs(bot_asleep_in(&bot, T) - 100) < 1e-6, "the sleep timer comes back, got %.2f", bot_asleep_in(&bot, T));
    CHECK(bot.mood == BOT_SLEEPY, "still sleepy");

    fresh();
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_SLEEPY});
    scene_frame(T);
    CHECK(scene_act_kind() == DEMO_ACT_SLEEPY, "the kind reads back");
    bot_set_mood(&bot, BOT_SLEEPY, T);
    step(1);
    D.st.playing = false;
    D.st.gen++;
    scene_frame(T);
    CHECK(bot.mood == BOT_IDLE, "woken after, unless it was sleepy before: %d", (int)bot.mood);

    fresh();
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_ASLEEP});
    scene_frame(T);
    bot_sleep(&bot, T);
    CHECK(bot.asleep, "asleep for the act");
    step(1);
    D.st.playing = false;
    D.st.gen++;
    scene_frame(T);
    CHECK(bot.mood == BOT_IDLE && !bot.asleep, "and woken after: %d", (int)bot.mood);

    fresh();
    bot_set_mood(&bot, BOT_SLEEPY, T);
    bot_sleep(&bot, T);
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    CHECK(bot.asleep, "a scene does not rouse a bot already asleep");
    step(1);
    D.st.playing = false;
    D.st.gen++;
    scene_frame(T);
    CHECK(bot.mood == BOT_SLEEPY, "and it stays sleepy");

    fresh();
    bot_set_mood(&bot, BOT_SLEEPY, T);
    bot_sleep(&bot, T);
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_SLEEPY});
    scene_frame(T);
    CHECK(!bot.asleep && isinf(bot_asleep_in(&bot, T)), "the sleepy act rouses it and holds the timer");
}

static void test_gate(void)
{
    fresh();
    CHECK(!scene_gate(T, true), "no demo: input passes");
    CHECK(D.inputs == 0, "and ends nothing");
    offer(DEMO_PAGE_WEATHER, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    CHECK(scene_gate(T, false), "an offered scene takes the input path");
    CHECK(scene_gate(T, true) && D.inputs == 1, "input ends even an offered scene");
    D.have = false;

    fresh();
    offer(DEMO_PAGE_WEATHER, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    CHECK(scene_on() && nav.page == 2, "scene on");
    D.st.playing = false;
    CHECK(scene_gate(T, false), "the scene still on takes the input, whatever the demo says");
    D.st.playing = true;
    calls[0] = 0;
    drops = 0;
    step(1);
    in_pending = true;
    scene_frame(T);
    CHECK(D.inputs == 1 && drops == 1, "input ends the scene and is dropped, %d %d", D.inputs, drops);
    CHECK(strncmp(calls, "drop tick", 9) == 0, "the gate runs first: %s", calls);
    CHECK(!scene_on(), "ended on this frame");
    CHECK(!scene_resuming() && nav.page == 0, "the user's page comes back at once");
    CHECK(scene_gate(T, false), "the rest of the gesture is dropped for the quiet time");
    step(DEMO_GATE_QUIET_S + 0.1);
    in_held = true;
    CHECK(scene_gate(T, false), "and while held");
    in_held = false;
    D.st.view_on = true;
    CHECK(scene_gate(T, false), "and until the real view is back");
    D.st.view_on = false;
    CHECK(!scene_gate(T, false), "then input passes");

    fresh();
    offer(DEMO_PAGE_WEATHER, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    step(1);
    D.timed = true;
    scene_frame(T);
    CHECK(scene_resuming() && nav.page == 2, "held after a natural end");
    in_pending = true;
    scene_frame(T);
    CHECK(!scene_resuming() && nav.page == 0, "an input during the hold brings the user's page back at once");

    fresh();
    offer(DEMO_PAGE_WEATHER, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    in_pending = true;
    scene_frame(T);
    offer(DEMO_PAGE_POMODORO, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    CHECK(scene_on(), "a new scene after an input");
    step(1);
    D.timed = true;
    scene_frame(T);
    CHECK(scene_resuming(), "clears the gate, so its own natural end holds its page");

    fresh();
    offer(DEMO_PAGE_WEATHER, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    D.offer_on_input = true;
    in_pending = true;
    step(1);
    scene_frame(T);
    CHECK(!scene_on() && !scene_resuming() && nav.page == 0, "a server offer racing the input still brings the user's page back at once");
    D.offer_on_input = false;
}

static void cue_apply(bot_t *b, const wxm_cue_t *c, double t)
{
    switch (c->op) {
    case WXM_GLIDE: bot_act_gaze(b, c->a, c->b, true, t); break;
    case WXM_SACCADE: bot_act_gaze(b, c->a, c->b, false, t); break;
    case WXM_LID: bot_act_lid(b, c->a, c->b, t); break;
    case WXM_BLINK: bot_act_blink(b, c->a, t); break;
    case WXM_EYES:
        bot_act_eyes(b, c->eyes == WXM_EYES_ROUND ? BOT_EYES_ROUND : c->eyes == WXM_EYES_HAPPY ? BOT_EYES_HAPPY : -1, t);
        break;
    case WXM_HOP: bot_act_hop(b, t); break;
    case WXM_END: bot_act_end(b, c->a > 0, c->b, t); break;
    }
}

static void test_cue_loop(void)
{
    static wx_temp_t scratch;
    static wxm_moment_t m;
    wx_obs_t all[] = {obs("2", 12, 0), obs("0", 2, 0), obs("0", -3, 0), obs("0", 31, 0), obs("2", 12, 0), obs("2", 12, 0), obs("71", -2, 2), obs("61", 9, 2), obs("95", 9, 3)};
    all[1].night = 1;
    all[4].wind_kmh = 45;
    unsigned ops = 0;
    for (int kind = WXM_GLANCE; kind <= WXM_STORM; kind++) {
        fresh();
        if (!wxm_moment_from((wxm_kind_t)kind, &all[kind - 1], false, &scratch, &m)) {
            CHECK(kind == WXM_NIGHT, "kind %d has its moment", kind);
            continue;
        }
        moments_cue_start(&m, T);
        bot_act_begin(&ref, T);
        CHECK(bot.acting, "kind %d: the eyes act", kind);
        int n, cue = 0;
        const wxm_cue_t *c = wxm_script(&m, &n);
        CHECK(n > 0 && c[n - 1].op == WXM_END, "kind %d: the script ends with END", kind);
        step(0.1);
        moments_cue_poll(T);
        poses_same("no cue before it plays");
        double start = T;
        for (double tau = 0; cue < n; tau += DT) {
            T = start + tau;
            MV.tau = tau;
            moments_cue_poll(T);
            while (cue < n && tau >= c[cue].t) {
                ops |= 1u << c[cue].op;
                cue_apply(&ref, &c[cue++], T);
            }
            poses_same("every cue on its tau");
        }
        CHECK(!bot.acting, "kind %d: END ends the acting", kind);
        MV.tau = 99;
        step(0.1);
        moments_cue_poll(T);
        poses_same_for("nothing after the end", 0.3);
        bot_act_begin(&bot, T);
        moment_unact(T);
        CHECK(bot.acting, "kind %d: after END a later unact leaves another moment's acting alone", kind);
        bot_act_end(&bot, true, 0.3, T);
    }
    CHECK(ops == (1u << (WXM_END + 1)) - 1, "the scripts cover every cue op, got %#x", ops);

    fresh();
    wxm_moment_from(WXM_GLANCE, &all[0], true, &scratch, &m);
    moments_cue_start(&m, T);
    MV.tau = 99;
    moments_cue_poll(T);
    CHECK(!bot.acting, "a quiet moment has no cues");

    fresh();
    wxm_moment_from(WXM_RAIN, &all[7], false, &scratch, &m);
    moments_cue_start(&m, T);
    bot_act_begin(&ref, T);
    MV.tau = 0;
    moments_cue_poll(T);
    int n;
    const wxm_cue_t *c = wxm_script(&m, &n);
    for (int i = 0; i < n && c[i].t <= 0; i++) cue_apply(&ref, &c[i], T);
    step(1);
    moment_unact(T);
    bot_act_end(&ref, true, 0.3, T);
    poses_same("unact ends the acting");
    MV.tau = 99;
    step(0.1);
    moments_cue_poll(T);
    poses_same_for("unact ends the cues too", 0.3);
}

static double run_moment(const wx_obs_t *o, bool calm, bool page, double s)
{
    for (double end = T + s; T < end; T += 0.5) {
        moments_tick(T, o, calm, page);
        if (MV.starts) return T;
    }
    return -1;
}

static void test_moment_trigger(void)
{
    wx_obs_t o = obs("2", 12, 0);

    fresh();
    double t0 = T, at = run_moment(&o, false, true, 90);
    CHECK(at >= t0 + 60 && at < t0 + 61, "the periodic moment comes after weather_every_min, at %.1f", at - t0);
    CHECK(MV.busy && !MV.last.quiet && bot.acting, "resting: the full variant with acting");

    fresh();
    ks.weather_every_min = 2;
    t0 = T;
    at = run_moment(&o, false, true, 150);
    CHECK(at >= t0 + 120 && at < t0 + 121, "every_s is the setting in minutes, at %.1f", at - t0);

    fresh();
    ks.weather_every_min = 0;
    CHECK(run_moment(&o, false, true, 200) < 0, "0 is none");

    fresh();
    bot_set_mood(&bot, BOT_WORKING, T);
    at = run_moment(&o, false, true, 90);
    CHECK(at > 0 && MV.last.quiet && !bot.acting, "working: the quiet variant, no acting");

    fresh();
    bot_set_mood(&bot, BOT_ERROR, T);
    CHECK(run_moment(&o, false, true, 200) < 0, "error: none");
    fresh();
    bot_set_mood(&bot, BOT_WAITING, T);
    CHECK(run_moment(&o, false, true, 200) < 0, "waiting: none");

    fresh();
    ks.weather_moments = false;
    CHECK(run_moment(&o, false, true, 200) < 0, "off: none");

    fresh();
    CHECK(run_moment(&o, false, false, 200) < 0, "off the bot page: none");
    t0 = T;
    at = run_moment(&o, false, true, 90);
    CHECK(at >= t0 + 59, "a slot missed off the page starts a full interval later, at %.1f", at - t0);

    fresh();
    bot_set_mood(&bot, BOT_SLEEPY, T);
    bot_sleep(&bot, T);
    CHECK(run_moment(&o, false, true, 200) < 0, "asleep: none");

    fresh();
    CHECK(run_moment(&o, true, true, 200) < 0, "calm: none");
    at = run_moment(&o, false, true, 90);
    CHECK(at > 0, "after calm it comes");

    fresh();
    run_moment(&o, false, true, 59);
    bot_set_mood(&bot, BOT_DONE, T);
    t0 = T;
    at = run_moment(&o, false, true, 10);
    CHECK(at >= t0 + WXM_SETTLE_S, "a change of the bot's own mood restarts the settle, at %.1f", at - t0);

    fresh();
    MU.busy = true;
    CHECK(run_moment(&o, false, true, 200) < 0, "a music moment playing blocks it");
    MU.busy = false;
    CHECK(run_moment(&o, false, true, 90) > 0, "then it comes");

    fresh();
    CHECK(run_moment(&o, false, true, 90) > 0, "a first moment");
    MV.starts = 0;
    MV.tau = 1;
    wx_obs_t warmer = obs("2", 15, 0);
    for (double end = T + WXM_SAME_S + 100; T < end; T += 1) moments_tick(T, &warmer, false, true);
    CHECK(MV.starts == 0, "one at a time: a playing one blocks the next past the gap and the same-kind wait");
    MV.busy = false;
    for (double end = T + 70; T < end && !MV.starts; T += 1) moments_tick(T, &warmer, false, true);
    CHECK(MV.starts == 1, "then the next comes");

    fresh();
    bot_set_mood(&bot, BOT_WORKING, T);
    chase_frame(true, false);
    chase_ctl_request(GC_STYLE_FULL, 0, 10);
    chase_frame(true, false);
    CHECK(chase_ctl_busy(), "a chase runs");
    CHECK(run_moment(&o, false, true, 200) < 0, "a chase blocks it");

    fresh();
    MV.ok = false;
    run_moment(&o, false, true, 90);
    for (double end = T + 1; T < end; T += 0.1) moments_tick(T, &o, false, true);
    CHECK(MV.starts > 3, "a start that fails is offered again, %d", MV.starts);
    CHECK(!bot.acting, "without acting");
    MV.ok = true;
    int starts = MV.starts;
    moments_tick(T, &o, false, true);
    CHECK(MV.starts == starts + 1 && MV.busy, "and starts as soon as it can, so the failed ones did not count");
    step(1);
    MV.busy = false;
    int after = MV.starts;
    for (double end = T + WXM_GAP_S - 2; T < end; T += 1) moments_tick(T, &o, false, true);
    CHECK(MV.starts == after, "the one that started counts: the gap holds");
}

static void test_moment_cut(void)
{
    wx_obs_t o = obs("2", 12, 0);

    fresh();
    ks.weather_moments = false;
    moments_tick(T, &o, false, true);
    CHECK(MV.releases == 1, "moments off and idle: the arena goes");
    MV.busy = true;
    moments_tick(T, &o, false, true);
    CHECK(MV.releases == 1, "never while one bakes or plays");
    CHECK(!MV.busy && MV.stops == 1 && MV.aborts == 0, "turning them off stops a playing one");
    ks.weather_moments = true;
    MV.releases = 0;
    moments_tick(T, &o, false, true);
    CHECK(MV.releases == 0, "moments on keep the arena");

    fresh();
    run_moment(&o, false, true, 90);
    bot_act_begin(&ref, T);
    step(1);
    moments_tick(T, &o, false, false);
    CHECK(!MV.busy && MV.stops == 1 && MV.aborts == 0, "leaving the bot page stops it");
    bot_act_end(&ref, true, 0.3, T);
    poses_same_for("the acting ends", 0.5);

    fresh();
    run_moment(&o, false, true, 90);
    double started = T;
    bot_act_begin(&ref, started);
    step(0.8);
    bot_set_mood(&bot, BOT_DONE, T);
    bot_set_mood(&ref, BOT_DONE, T);
    moments_tick(T, &o, false, true);
    CHECK(!MV.busy && MV.aborts == 1 && MV.stops == 0, "a mood change cuts it short");
    bot_act_drop(&ref, T);
    poses_same_for("a mood change drops the acting: the new mood's glance stays", 0.5);

    fresh();
    run_moment(&o, false, true, 90);
    bot_act_begin(&ref, T);
    step(0.8);
    moments_tick(T, &o, true, true);
    CHECK(!MV.busy && MV.aborts == 1, "calm cuts it short");
    bot_act_end(&ref, true, 0.3, T);
    poses_same_for("calm ends the acting", 0.5);

    fresh();
    run_moment(&o, false, true, 90);
    step(1);
    moments_tick(T, &o, false, true);
    CHECK(MV.busy && MV.aborts == 0 && MV.stops == 0, "nothing else cuts it");

    fresh();
    bot_set_mood(&bot, BOT_DONE, T);
    run_moment(&o, false, true, 90);
    step(1);
    moments_tick(T, &o, false, true);
    CHECK(MV.busy && MV.aborts == 0, "the mood it started in is what a change is measured against");
    moment_view_stop();
    moment_unact(T);
}

static void hold_check(const char *what, bool held)
{
    int req = chase_ctl_request(GC_STYLE_FULL, 0, 2);
    CHECK(req == 0, "%s: the request is taken, %d", what, req);
    step(DT);
    chase_frame(true, false);
    CHECK(chase_ctl_busy() != held, "%s: chase %s", what, held ? "held" : "free");
}

static void test_chase_hold(void)
{
    wx_obs_t o = obs("2", 12, 0);

    fresh();
    chase_frame(true, false);
    run_moment(&o, false, true, 90);
    hold_check("a weather moment", true);
    MV.busy = false;
    moments_tick(T, &o, false, true);
    step(DT);
    chase_frame(true, false);
    CHECK(chase_ctl_busy(), "the chase waits for the moment, then starts");

    fresh();
    chase_frame(true, false);
    music_started();
    moments_tick(T, &o, false, true);
    moments_music_tick(T, false, false, true, true);
    hold_check("a music moment", true);
    MU.busy = false;
    moments_tick(T, &o, false, true);
    moments_music_tick(T, false, false, true, true);
    step(DT);
    chase_frame(true, false);
    CHECK(chase_ctl_busy(), "the music moment lets go through moments_tick");

    fresh();
    bot_set_mood(&bot, BOT_WORKING, T);
    chase_frame(true, false);
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_HOP});
    scene_frame(T);
    scene_act(T, NULL, true);
    step(DT);
    chase_frame(true, false);
    CHECK(!chase_ctl_busy(), "a scene without a chase holds it");
}

static void test_scene_acts(void)
{
    struct {
        demo_act_kind_t kind;
        size_t flag;
    } acts[] = {{DEMO_ACT_HOP, offsetof(bot_t, hopping)}, {DEMO_ACT_POP, offsetof(bot_t, popping)}, {DEMO_ACT_BLINK, offsetof(bot_t, blinking)}};
    for (int i = 0; i < 3; i++) {
        fresh();
        offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = acts[i].kind});
        scene_frame(T);
        scene_act(T, NULL, true);
        bool *flag = (bool *)((char *)&bot + acts[i].flag);
        CHECK(*flag && D.acts == 0, "act %d starts", (int)acts[i].kind);
        for (double end = T + 5; T < end && D.acts == 0; T += DT) {
            bot_pose(&bot, T);
            scene_act(T, NULL, true);
        }
        CHECK(!*flag && D.acts == 1, "act %d finishes when it ends", (int)acts[i].kind);
        CHECK(D.act_gen == D.st.gen, "with the scene's gen");
        step(1);
        scene_act(T, NULL, true);
        CHECK(D.acts == 1, "once");
    }
    fresh();
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_NONE});
    scene_frame(T);
    scene_act(T, NULL, true);
    CHECK(D.acts == 0, "no act has nothing to finish");
    fresh();
    offer(DEMO_PAGE_BOT, -1, (demo_act_t){.kind = DEMO_ACT_SLEEPY});
    scene_frame(T);
    scene_act(T, NULL, true);
    CHECK(D.acts == 0, "sleepy runs for the scene");
}

static void test_music_start(void)
{
    fresh();
    music_started();
    bot_act_begin(&ref, T);
    step(0.1);
    poses_same("the acting begins with the moment");

    fresh();
    np_set("A", NP_PLAYING, false);
    CHECK(music_run(false, false, true, true, 20) < 0, "the first block is the baseline");
    np_set("A", NP_PLAYING, false);
    CHECK(music_run(false, false, true, true, 20) < 0, "the same track stays one");

    fresh();
    music_owed();
    CHECK(music_run(true, false, true, true, 8) < 0, "calm: none");
    fresh();
    music_owed();
    CHECK(music_run(false, true, true, true, 8) < 0, "offline: none");
    fresh();
    music_owed();
    CHECK(music_run(false, false, false, true, 8) < 0, "off the bot page: none");
    fresh();
    music_owed();
    CHECK(music_run(false, false, true, false, 8) < 0, "no block for a demo: none");
    fresh();
    music_owed();
    MV.busy = true;
    CHECK(music_run(false, false, true, true, 8) < 0, "a weather moment playing: none");
    fresh();
    music_owed();
    bot_set_mood(&bot, BOT_WORKING, T);
    CHECK(music_run(false, false, true, true, 8) < 0, "working: none");
    bot_set_mood(&bot, BOT_IDLE, T);
    CHECK(music_run(false, false, true, true, 8) > 0, "resting again: the owed moment comes");
    fresh();
    music_owed();
    bot_set_mood(&bot, BOT_SLEEPY, T);
    bot_sleep(&bot, T);
    CHECK(music_run(false, false, true, true, 8) < 0, "asleep: none");
    fresh();
    bot_set_mood(&bot, BOT_WORKING, T);
    chase_frame(true, false);
    chase_ctl_request(GC_STYLE_FULL, 0, 10);
    chase_frame(true, false);
    bot_set_mood(&bot, BOT_IDLE, T);
    music_owed();
    CHECK(chase_ctl_busy() && music_run(false, false, true, true, 8) < 0, "a chase: none");

    fresh();
    wx_obs_t o = obs("2", 12, 0);
    run_moment(&o, false, true, 90);
    double shown = T;
    MV.busy = false;
    music_owed();
    CHECK(music_run(false, false, true, true, 200) < 0, "a weather moment shown: none for the gap");
    T = shown + MM_GAP_S - 1;
    CHECK(music_run(false, false, true, true, 0.5) < 0, "still none");
    T = shown + MM_GAP_S + 0.5;
    CHECK(music_run(false, false, true, true, 1) > 0, "after the gap it comes");

    fresh();
    MU.ok = false;
    music_owed();
    music_run(false, false, true, true, 8);
    for (double end = T + 1; T < end; T += 0.1) music_tick(false, false, true, true);
    CHECK(MU.starts > 3 && !bot.acting, "a start that fails is offered again, without acting: %d", MU.starts);
    MU.ok = true;
    music_tick(false, false, true, true);
    CHECK(MU.busy, "and starts as soon as it can");
    music_view_stop();
    music_unact(T);
    np_set("C", NP_PLAYING, false);
    int starts = MU.starts;
    for (double end = T + 8; T < end; T += 0.25) music_tick(false, false, true, true);
    CHECK(MU.starts == starts, "the one that started counts: the next track is within MM_SAME_S");

    fresh();
    music_started();
    MU.busy = false;
    music_unact(T);
    wx_obs_t w = obs("2", 12, 0);
    MV.starts = 0;
    for (double end = T + MM_GAP_S - 2; T < end; T += 1) moments_tick(T, &w, false, true);
    CHECK(MV.starts == 0, "a music moment holds the next weather moment off for its gap (mm_recent)");
    for (double end = T + 70; T < end && !MV.starts; T += 1) moments_tick(T, &w, false, true);
    CHECK(MV.starts == 1, "then it comes");
}

static void test_music_demo(void)
{
    fresh();
    np_set("A", NP_PLAYING, false);
    music_tick(false, false, true, true);
    step(0.1);
    np_set("Ember Demo Track", NP_PLAYING, true);
    CHECK(music_run(false, false, true, true, 8) < 0, "a demo block starts nothing, even with the flags stale");
    np_set("B", NP_PLAYING, false);
    CHECK(music_run(false, false, true, true, 8) < 0, "and the real track after it is a baseline");

    fresh();
    music_started();
    np_set("Ember Demo Track", NP_PLAYING, true);
    step(0.1);
    music_tick(false, false, true, true);
    CHECK(!MU.busy && MU.stops == 1 && MU.aborts == 0 && !bot.acting, "a demo block stops a playing moment at once");
}

static void test_music_cut(void)
{
    struct {
        const char *what;
        bool calm, offline, page, on;
        np_play_t state;
        bool mood;
        bool abort;
    } cuts[] = {
        {"a mood change", false, false, true, true, NP_PLAYING, true, true},
        {"calm", true, false, true, true, NP_PLAYING, false, true},
        {"offline", false, true, true, true, NP_PLAYING, false, true},
        {"a pause", false, false, true, true, NP_PAUSED, false, true},
        {"leaving the bot page", false, false, false, true, NP_PLAYING, false, false},
        {"a demo", false, false, true, false, NP_PLAYING, false, false},
    };
    for (size_t i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
        fresh();
        music_started();
        bot_act_begin(&ref, T);
        step(0.8);
        if (cuts[i].mood) {
            bot_set_mood(&bot, BOT_DONE, T);
            bot_set_mood(&ref, BOT_DONE, T);
        }
        if (cuts[i].state != NP_PLAYING) np_set("B", cuts[i].state, false);
        music_tick(cuts[i].calm, cuts[i].offline, cuts[i].page, cuts[i].on);
        CHECK(!MU.busy && MU.aborts == (int)cuts[i].abort && MU.stops == (int)!cuts[i].abort, "%s: abort %d stop %d", cuts[i].what, MU.aborts, MU.stops);
        if (cuts[i].mood) bot_act_drop(&ref, T);
        else bot_act_end(&ref, true, 0.3, T);
        poses_same_for(cuts[i].what, 0.5);
    }

    fresh();
    music_started();
    step(1);
    music_tick(false, false, true, true);
    CHECK(MU.busy && !MU.aborts && !MU.stops, "nothing else cuts it");
    music_view_stop();
    music_unact(T);
    bot_act_begin(&bot, T);
    music_unact(T);
    CHECK(bot.acting, "a cut moment's unact is spent: it leaves the next acting alone");
    bot_act_end(&bot, true, 0.3, T);

    fresh();
    bot_set_mood(&bot, BOT_DONE, T);
    music_started();
    step(1);
    music_tick(false, false, true, true);
    CHECK(MU.busy && !MU.aborts, "the mood it started in is what a change is measured against");
    music_view_stop();
    music_unact(T);

    fresh();
    music_started();
    bot_act_begin(&ref, T);
    int n;
    const mm_cue_t *c = mm_script(&n);
    CHECK(n == 2 && c[0].op == MM_HAPPY, "happy eyes, then the end");
    MU.tau = c[0].t - 0.01;
    step(DT);
    music_tick(false, false, true, true);
    poses_same("before the happy cue");
    MU.tau = c[0].t;
    step(DT);
    music_tick(false, false, true, true);
    bot_act_eyes(&ref, BOT_EYES_HAPPY, T);
    poses_same_for("the happy cue", 0.3);
    MU.tau = c[1].t;
    music_tick(false, false, true, true);
    CHECK(!bot.acting && MU.busy, "the end cue ends the acting while the headphones lift");
    bot_act_end(&ref, true, 0.3, T);
    poses_same_for("the end cue", 0.3);
    MU.tau = c[1].t + 1;
    music_tick(false, false, true, true);
    CHECK(!bot.acting, "once");
}

static void test_orbit_task(void)
{
    fresh();
    chase_ctl_orbit_start();
    CHECK(TK.fn && strcmp(TK.name, "orbit") == 0 && TK.stack == 3328 && TK.prio == 1 && TK.core == 1, "the orbit task as before");
    chase_ctl_orbit_reap();
    CHECK(TK.deletes == 0, "not reaped while it fills");
    if (!setjmp(task_end)) TK.fn(NULL);
    chase_ctl_orbit_reap();
    CHECK(TK.deletes == 1, "reaped once it is done");
    chase_ctl_orbit_reap();
    CHECK(TK.deletes == 1, "and only once");
    fresh();
    TK.fail = true;
    chase_ctl_orbit_start();
    chase_ctl_orbit_reap();
    CHECK(TK.deletes == 0, "a task that never started is not deleted");
}

int main(void)
{
    moment_unact(0);
    bot_init(&bot, SEED, T);
    moments_init(&(moments_ctx_t){.bot = &bot, .ks = &ks});
    scene_init(&(scene_ctx_t){.bot = &bot, .nav = &nav});
    test_page_and_face();
    test_stops_moments();
    test_moment_scene();
    test_chase_scene();
    test_real_chase_survives();
    test_half_chase();
    test_calm_and_glint_gates();
    test_sleepy_scene();
    test_gate();
    test_scene_acts();
    test_cue_loop();
    test_moment_trigger();
    test_moment_cut();
    test_chase_hold();
    test_music_start();
    test_music_demo();
    test_music_cut();
    test_orbit_task();
    if (failures) {
        printf("scene: %d failures\n", failures);
        return 1;
    }
    printf("scene: all tests passed\n");
    return 0;
}
