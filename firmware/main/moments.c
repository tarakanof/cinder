#include "moments.h"

#include "chase_ctl.h"
#include "esp_attr.h"
#include "moment_view.h"
#include "music_moment.h"
#include "music_view.h"
#include "nowplaying_client.h"

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

void moments_init(const moments_ctx_t *ctx)
{
    s_bot = ctx->bot;
    s_ks = ctx->ks;
    wxm_init(&s_wxm);
    mm_init(&s_mm);
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

__attribute__((noinline)) void moments_tick(double t, const wx_obs_t *wx, bool calm, bool bot_page)
{
    if (!s_ks->weather_moments && !moment_view_busy()) moment_view_release();
    if (moment_view_busy() && (!bot_page || !s_ks->weather_moments)) {
        moment_view_stop();
        moment_unact(t);
    } else if (moment_view_busy() && (s_bot->mood != s_moment_mood || calm)) {
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
