#include "scene.h"

#include <math.h>
#include <string.h>

#include "bot_view.h"
#include "chase_ctl.h"
#include "ember_client.h"
#include "esp_attr.h"
#include "input_route.h"
#include "moment_view.h"
#include "moments.h"
#include "music_view.h"
#include "temp_face.h"
#include "wx_faces.h"

_Static_assert(WXM_GLANCE == 1 && WXM_STORM == 9 && WXF_TEMP == 0 && WXF_AIR == 6, "demo.c's moment and face order");
#define SCENE_START_S 3.0
#define SCENE_NEXT_S 1.5
typedef enum { SCENE_ACT_WAIT, SCENE_ACT_RUN, SCENE_ACT_DONE } scene_act_st_t;
static bot_t *s_bot;
static pages_nav_t *s_nav;
static bool s_scene_on;
static unsigned s_scene_n;
static unsigned s_scene_gen;
static demo_act_t s_scene_act;
static scene_act_st_t s_scene_act_st;
static double s_scene_t0;
static bool s_scene_face, s_scene_chased;
static int s_scene_face_saved;
static bot_mood_t s_scene_mood0;
static double s_scene_sleep_in;
static EXT_RAM_BSS_ATTR glint_chase_t s_scene_chase;
static gc_half_chase_t s_scene_half;
static int s_scene_chase_req = -1;
static double s_scene_resume_at;
static demo_gate_t s_scene_gate;
static EXT_RAM_BSS_ATTR wx_temp_t s_scene_scratch;
static EXT_RAM_BSS_ATTR demo_block_t s_scene_block;

void scene_init(const scene_ctx_t *ctx)
{
    s_bot = ctx->bot;
    s_nav = ctx->nav;
}

bool scene_on(void) { return s_scene_on; }

unsigned scene_n(void) { return s_scene_n; }

demo_act_kind_t scene_act_kind(void) { return s_scene_act.kind; }

bool scene_resuming(void) { return s_scene_resume_at != 0; }

void scene_resume_cancel(void) { s_scene_resume_at = 0; }

int scene_chase_take(void)
{
    int req = s_scene_chase_req;
    s_scene_chase_req = -1;
    return req;
}

static void scene_act_done(void)
{
    s_scene_act_st = SCENE_ACT_DONE;
    ember_client_demo_act_done(s_scene_gen);
}

static void scene_moment(double t, const wx_obs_t *wx, bool bot_page)
{
    static EXT_RAM_BSS_ATTR wxm_moment_t m;
    const demo_act_t *a = &s_scene_act;
    if (s_scene_act_st == SCENE_ACT_WAIT) {
        if (!bot_page || !wx || !wxm_moment_from((wxm_kind_t)a->moment, wx, a->quiet, &s_scene_scratch, &m)) {
            scene_act_done();
        } else if (moment_view_start(&m, t)) {
            s_scene_act_st = SCENE_ACT_RUN;
            moments_cue_start(&m, t);
        } else if (t - s_scene_t0 > SCENE_START_S) {
            scene_act_done();
        }
        return;
    }
    moments_cue_poll(t);
    if (!moment_view_busy()) scene_act_done();
}

__attribute__((noinline)) void scene_act(double t, const wx_obs_t *wx, bool bot_page)
{
    const demo_act_t *a = &s_scene_act;
    chase_ctl_hold(a->kind != DEMO_ACT_CHASE);
    if (a->kind == DEMO_ACT_NONE || a->kind == DEMO_ACT_SLEEPY || a->kind == DEMO_ACT_ASLEEP || s_scene_act_st == SCENE_ACT_DONE)
        return;
    bool wait = s_scene_act_st == SCENE_ACT_WAIT;
    switch (a->kind) {
    case DEMO_ACT_MOMENT:
        scene_moment(t, wx, bot_page);
        return;
    case DEMO_ACT_CHASE:
        if (chase_ctl_busy()) s_scene_act_st = SCENE_ACT_RUN;
        else if (!wait) scene_act_done();
        else if (t - s_scene_t0 > SCENE_START_S) scene_act_done();
        else s_scene_chase_req = a->chase_style;
        return;
    case DEMO_ACT_HOP:
        if (wait) bot_hop(s_bot, t);
        else if (!s_bot->hopping) scene_act_done();
        break;
    case DEMO_ACT_POP:
        if (wait) bot_pop(s_bot, t);
        else if (!s_bot->popping) scene_act_done();
        break;
    case DEMO_ACT_BLINK:
        if (wait) bot_blink(s_bot, t);
        else if (!s_bot->blinking) scene_act_done();
        break;
    default:
        scene_act_done();
        return;
    }
    if (wait) s_scene_act_st = SCENE_ACT_RUN;
}

static int page_index(const char *id)
{
    for (int i = 0; i < PAGES_N; i++)
        if (strcmp(PAGES[i].id, id) == 0) return i;
    return 0;
}

bool scene_gate(double t, bool input)
{
    ember_demo_status_t st;
    ember_client_demo_status(&st);
    demo_gate_in_t in = {.playing = st.playing || s_scene_on || s_scene_resume_at > 0,
                         .input = input,
                         .held = input_route_held(),
                         .view_on = st.view_on};
    demo_gate_act_t a = demo_gate(&s_scene_gate, &in, t);
    if (a == DEMO_GATE_END) {
        ember_client_demo_input();
        if (s_scene_resume_at > 0) s_scene_resume_at = t;
    }
    return a != DEMO_GATE_PASS;
}

static void scene_resume(double t)
{
    s_scene_resume_at = 0;
    if (s_nav->page != s_nav->order[s_nav->pos]) {
        pages_resume(s_nav);
        wipe_start(-1, t);
    }
}

void scene_end(double t)
{
    s_scene_on = false;
    if (s_scene_act.kind == DEMO_ACT_MOMENT && s_scene_act_st == SCENE_ACT_RUN && moment_view_busy()) {
        moment_view_abort(t);
        moment_unact(t);
    }
    chase_ctl_restore(&s_scene_chase, &s_scene_half, s_scene_t0, t);
    if (s_scene_chased) {
        if (s_bot->tracking) bot_track_end(s_bot, t);
        bot_view_glint_force(false);
        s_scene_chased = false;
    }
    bool slept = s_scene_act.kind == DEMO_ACT_SLEEPY || s_scene_act.kind == DEMO_ACT_ASLEEP;
    if (slept && s_scene_mood0 != BOT_SLEEPY) bot_wake(s_bot, t);
    else if (s_scene_mood0 == BOT_SLEEPY) bot_sleep_in(s_bot, s_scene_sleep_in, t);
    if (s_scene_face) temp_face_pick(s_scene_face_saved);
    s_scene_face = false;
    s_scene_resume_at = t + SCENE_NEXT_S;
}

static void scene_start(const demo_block_t *b, double t)
{
    s_scene_on = true;
    chase_ctl_status_busy();
    if (!++s_scene_n) s_scene_n = 1;
    s_scene_resume_at = 0;
    s_scene_act = b->act;
    s_scene_act_st = SCENE_ACT_WAIT;
    s_scene_t0 = t;
    s_scene_mood0 = s_bot->mood;
    s_scene_sleep_in = bot_asleep_in(s_bot, t);
    chase_ctl_pause(&s_scene_chase, &s_scene_half);
    demo_gate_start(&s_scene_gate);
    if (moment_view_busy()) {
        moment_view_stop();
        moment_unact(t);
    }
    if (music_view_busy()) {
        music_view_stop();
        music_unact(t);
    }
    int page = page_index(demo_page_id(b->page));
    if (page != s_nav->page) {
        pages_show(s_nav, page);
        wipe_start(1, t);
    }
    if (b->face >= 0) {
        s_scene_face_saved = temp_face_picked();
        temp_face_pick(b->face);
        s_scene_face = true;
    }
    if (b->act.kind == DEMO_ACT_CHASE) {
        bot_view_glint_force(true);
        s_scene_chased = true;
    }
    if (!s_bot->asleep || b->act.kind == DEMO_ACT_SLEEPY) bot_sleep_in(s_bot, INFINITY, t);
}

void scene_frame(double t)
{
    if (scene_gate(t, input_route_pending())) input_route_drop();
    bool timed = ember_client_demo_tick(t);
    ember_demo_status_t st;
    ember_client_demo_status(&st);
    bool ended = s_scene_on && (!st.playing || st.gen != s_scene_gen);
    if (ended) scene_end(t);
    unsigned gen;
    if (ember_client_demo_take(&s_scene_block, &gen)) {
        if (s_scene_on) scene_end(t);
        s_scene_gen = gen;
        scene_start(&s_scene_block, t);
    } else if (ended && (s_scene_gate.on || (!st.playing && !timed))) {
        scene_resume(t);
    }
    if (s_scene_resume_at > 0 && t >= s_scene_resume_at) scene_resume(t);
}
