#include "moments.h"

#include <stdio.h>

#include "chase_ctl.h"
#include "esp_attr.h"
#include "moment_view.h"
#include "music_moment.h"
#include "music_view.h"
#include "nowplaying_client.h"
#include "pomo_client.h"
#include "print_client.h"
#include "print_jump.h"
#include "print_moment.h"
#include "room_moment.h"

static bot_t *s_bot;
static const knob_settings_t *s_ks;
static EXT_RAM_BSS_ATTR wxm_trigger_t s_wxm;
static const wxm_cue_t *s_cues;
static int s_cue, s_cue_n;
static bot_mood_t s_moment_mood;
static bool s_acting;
static EXT_RAM_BSS_ATTR mm_trigger_t s_mm;
static EXT_RAM_BSS_ATTR np_snapshot_t s_mm_np;
static EXT_RAM_BSS_ATTR uint32_t s_mm_gen, s_mm_key;
static EXT_RAM_BSS_ATTR bot_mood_t s_mm_mood;
static EXT_RAM_BSS_ATTR bool s_mm_acting;
static EXT_RAM_BSS_ATTR int s_mm_cue;
static EXT_RAM_BSS_ATTR pm_trigger_t s_pm;
static EXT_RAM_BSS_ATTR pj_t s_pj;
static EXT_RAM_BSS_ATTR print_snapshot_t s_ps;
static bool s_print_owns, s_print_demo;
static bool (*s_room_air)(int *aqi);
static EXT_RAM_BSS_ATTR rm_trigger_t s_rm;
static bool s_room_owns;

void moments_init(const moments_ctx_t *ctx)
{
    s_bot = ctx->bot;
    s_ks = ctx->ks;
    s_room_air = ctx->room_air;
    wxm_init(&s_wxm);
    mm_init(&s_mm);
    pm_init(&s_pm);
    pj_init(&s_pj);
    rm_init(&s_rm);
    s_print_owns = s_print_demo = s_room_owns = false;
}

void moment_unact(double t)
{
    if (s_acting && s_bot->mood != s_moment_mood) bot_act_drop(s_bot, t);
    else if (s_acting) bot_act_end(s_bot, true, 0.3, t);
    s_acting = false;
    s_cue = s_cue_n;
}

void music_unact(double t)
{
    if (s_mm_acting && s_bot->mood != s_mm_mood) bot_act_drop(s_bot, t);
    else if (s_mm_acting) bot_act_end(s_bot, true, 0.3, t);
    s_mm_acting = false;
}

static wxm_mood_t moment_mood(bot_mood_t m)
{
    return m == BOT_WORKING ? WXM_WORKING : m == BOT_WAITING || m == BOT_ERROR ? WXM_BUSY : WXM_REST;
}

static void moment_cue(const wxm_cue_t *c, double t)
{
    switch (c->op) {
    case WXM_GLIDE: bot_act_gaze(s_bot, c->a, c->b, true, t); break;
    case WXM_SACCADE: bot_act_gaze(s_bot, c->a, c->b, false, t); break;
    case WXM_LID: bot_act_lid(s_bot, c->a, c->b, t); break;
    case WXM_BLINK: bot_act_blink(s_bot, c->a, t); break;
    case WXM_EYES:
        bot_act_eyes(s_bot, c->eyes == WXM_EYES_ROUND ? BOT_EYES_ROUND : c->eyes == WXM_EYES_HAPPY ? BOT_EYES_HAPPY : -1, t);
        break;
    case WXM_HOP: bot_act_hop(s_bot, t); break;
    case WXM_END:
        bot_act_end(s_bot, c->a > 0, c->b, t);
        s_acting = false;
        break;
    }
}

void moments_cue_start(const wxm_moment_t *m, double t)
{
    s_print_owns = s_room_owns = false;
    s_moment_mood = s_bot->mood;
    s_cues = wxm_script(m, &s_cue_n);
    s_cue = 0;
    if (s_cue_n > 0) {
        bot_act_begin(s_bot, t);
        s_acting = true;
    }
}

void moments_cue_poll(double t)
{
    double tau = moment_view_poll(t);
    while (tau >= 0 && s_cue < s_cue_n && tau >= s_cues[s_cue].t) moment_cue(&s_cues[s_cue++], t);
}

static void room_tick(double t, bool calm, bool bot_page, bool on)
{
    int aqi = 0;
    rm_in_t in = {.t = t, .valid = on && s_room_air(&aqi), .aqi = aqi, .mood = moment_mood(s_bot->mood), .bot_mood = (int)s_bot->mood,
                  .shown = bot_page && !s_bot->asleep,
                  .blocked = moment_view_busy() || music_view_busy() || mm_recent(&s_mm, t) || calm || chase_ctl_busy(),
                  .on = on, .weather_first = s_ks->weather_moments && s_wxm.pending != WXM_NONE, .last_at = s_wxm.last_at};
    rm_out_t out;
    static EXT_RAM_BSS_ATTR wxm_moment_t m;
    if (rm_tick(&s_rm, &in, &out) && wxm_room_moment(out.kind, out.quiet, room_aqi_rgb(out.aqi), &m) && moment_view_start(&m, t)) {
        rm_shown(&s_rm, &out, t);
        wxm_note_other(&s_wxm, t);
        moments_cue_start(&m, t);
        s_room_owns = true;
    }
}

__attribute__((noinline)) void moments_tick(double t, const wx_obs_t *wx, bool calm, bool bot_page)
{
    bool room = s_room_air && s_ks->room_enabled && s_ks->room_moments;
    if (s_room_owns && !moment_view_busy()) s_room_owns = false;
    if (!s_ks->weather_moments && !room && !moment_view_busy()) moment_view_release();
    bool plays = moment_view_busy() && !s_print_owns;
    bool on = s_room_owns ? room : s_ks->weather_moments;
    if (plays && (!bot_page || !on)) {
        moment_view_stop();
        moment_unact(t);
    } else if (plays && (s_bot->mood != s_moment_mood || calm)) {
        moment_view_abort(t);
        moment_unact(t);
    }
    wxm_in_t in = {.t = t, .obs = wx, .mood = moment_mood(s_bot->mood), .bot_mood = (int)s_bot->mood, .shown = bot_page && !s_bot->asleep,
                   .blocked = moment_view_busy() || music_view_busy() || mm_recent(&s_mm, t) || calm || chase_ctl_busy(),
                   .on = s_ks->weather_moments,
                   .every_s = s_ks->weather_every_min * 60};
    static EXT_RAM_BSS_ATTR wxm_moment_t m;
    if (wxm_tick(&s_wxm, &in, &m) && moment_view_start(&m, t)) {
        wxm_shown(&s_wxm, &m, t);
        moments_cue_start(&m, t);
    }
    if (s_room_air) room_tick(t, calm, bot_page, room);
    moments_cue_poll(t);
    chase_ctl_hold(moment_view_busy());
}

__attribute__((noinline)) void moments_music_tick(double t, bool calm, bool offline, bool bot_page, bool on)
{
    if (np_client_get(&s_mm_np, &s_mm_gen))
        s_mm_key = mm_track_key(s_mm_np.info.title, s_mm_np.info.artist, s_mm_np.info.album, s_mm_np.info.source);
    on = mm_block_on(on, s_mm_np.demo);
    bool playing = s_mm_np.have && s_mm_np.info.state == NP_PLAYING;
    if (music_view_busy() && (!on || !bot_page)) {
        music_view_stop();
        music_unact(t);
    } else if (music_view_busy() && (s_bot->mood != s_mm_mood || calm || offline || !playing)) {
        music_view_abort(t);
        music_unact(t);
    }
    mm_in_t in = {.t = t, .have = s_mm_np.have && on, .playing = playing, .track = s_mm_key, .bot_mood = (int)s_bot->mood,
                  .rest = moment_mood(s_bot->mood) == WXM_REST, .shown = bot_page && !s_bot->asleep,
                  .blocked = !on || offline || calm || moment_view_busy() || music_view_busy() || chase_ctl_busy(),
                  .other_at = s_wxm.last_at};
    if (mm_tick(&s_mm, &in) && music_view_start(t)) {
        mm_shown(&s_mm, t);
        s_mm_mood = s_bot->mood;
        bot_act_begin(s_bot, t);
        s_mm_acting = true;
        s_mm_cue = 0;
    }
    int n;
    const mm_cue_t *c = mm_script(&n);
    double tau = music_view_poll(t);
    for (; tau >= 0 && s_mm_acting && s_mm_cue < n && tau >= c[s_mm_cue].t; s_mm_cue++) {
        if (c[s_mm_cue].op == MM_HAPPY) bot_act_eyes(s_bot, BOT_EYES_HAPPY, t);
        else music_unact(t);
    }
    if (music_view_busy()) chase_ctl_hold(true);
}

static void moment_text(const pm_out_t *o, char *buf, size_t n)
{
    if (o->kind == PM_ATTENTION) {
        snprintf(buf, n, "Printer needs you");
    } else if (o->took_s >= 0) {
        char d[16];
        print_duration_text(o->took_s, d, sizeof d);
        snprintf(buf, n, "Print done \xE2\x80\xA2 %s", d);
    } else {
        snprintf(buf, n, "Print done");
    }
}

bool moments_print_jump(double t, bool page_on, bool on_page, bool real_ok, bool blocked)
{
    print_client_get(&s_ps);
    pj_in_t in = {.t = t, .b = s_ps.has ? &s_ps.b : NULL, .have_srv = s_ps.have_srv,
                  .srv_now = s_ps.have_srv ? pomo_client_now() + s_ps.srv_offset : 0, .page_on = page_on, .on_page = on_page, .real_ok = real_ok,
                  .blocked = blocked};
    return pj_tick(&s_pj, &s_pm.seen, &in);
}

__attribute__((noinline)) void moments_print_tick(double t, bool calm, bool bot_page, bool real_ok, bool scene)
{
    print_client_get(&s_ps);
    if (s_print_owns && !moment_view_busy()) s_print_owns = false;
    if (s_print_owns) {
        if (s_print_demo && real_ok) {
            moment_view_abort(t);
            moment_unact(t);
        } else if (!bot_page || (!s_print_demo && !s_ks->print_moments)) {
            moment_view_stop();
            moment_unact(t);
        } else if (s_bot->mood != s_moment_mood || (calm && !s_print_demo)) {
            moment_view_abort(t);
            moment_unact(t);
        }
    }
    pm_in_t in = {.t = t, .b = s_ps.has ? &s_ps.b : NULL, .have_srv = s_ps.have_srv,
                  .srv_now = s_ps.have_srv ? pomo_client_now() + s_ps.srv_offset : 0,
                  .mood = (int)moment_mood(s_bot->mood), .bot_mood = (int)s_bot->mood, .shown = bot_page && !s_bot->asleep,
                  .page = bot_page && scene, .busy = moment_view_busy() || music_view_busy() || chase_ctl_busy(), .calm = calm, .on = s_ks->print_moments,
                  .real_ok = real_ok};
    pm_out_t out;
    static EXT_RAM_BSS_ATTR wxm_moment_t m;
    char text[WXM_TEXT_MAX];
    if (pm_tick(&s_pm, &in, &out)) {
        moment_text(&out, text, sizeof text);
        if (wxm_print_moment(out.kind == PM_DONE ? WXM_PRINT_DONE : WXM_PRINT_ATTENTION, out.quiet, text, &m) &&
            moment_view_start(&m, t)) {
            pm_shown(&s_pm, &out, t);
            if (!out.demo) wxm_note_other(&s_wxm, t);
            moments_cue_start(&m, t);
            s_print_owns = true;
            s_print_demo = out.demo;
        }
    }
    moments_cue_poll(t);
    if (moment_view_busy()) chase_ctl_hold(true);
}
