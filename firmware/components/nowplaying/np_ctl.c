#include "np_ctl.h"

#include <stdio.h>
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void np_ctl_init(np_ctl_t *c)
{
    memset(c, 0, sizeof *c);
    c->server_vol = -1;
    c->vol = -1;
    c->vol_sent_t = -1e9;
    c->vol_t = -1e9;
    c->last_t = -1e9;
    c->info_t = -1e9;
    c->track_t = -1e9;
    c->resume_t = -1e9;
    c->track_pos = -1;
}

void np_ctl_server(np_ctl_t *c, np_play_t state, int volume, double now)
{
    bool was_paused = c->server_state != NP_NONE && np_ctl_play_shown(c, now) == NP_PAUSED;
    if (state != NP_NONE) c->held = state;
    else if (c->server_state != NP_NONE) c->held = np_ctl_play_shown(c, now);
    c->server_state = state;
    c->server_vol = volume >= 0 && volume <= 100 ? volume : -1;
    if (c->play_set && state == c->play) c->play_set = false;
    if (state == NP_NONE) c->play_set = c->touch_on = false;
    if (was_paused && state != NP_NONE && np_ctl_play_shown(c, now) != NP_PAUSED) {
        c->touch_on = true;
        c->touch_t = now;
    }
}

np_play_t np_ctl_held(const np_ctl_t *c) { return c->held; }

void np_ctl_push(np_ctl_t *c, double now)
{
    if (c->server_state == NP_NONE) return;
    c->touch_rest = false;
    c->play = np_ctl_play_shown(c, now) == NP_PLAYING ? NP_PAUSED : NP_PLAYING;
    c->play_set = true;
    c->play_t = now;
    c->play_due = true;
    c->info_t = now;
    c->touch_on = true;
    c->touch_t = now;
}

void np_ctl_skip(np_ctl_t *c, int dir, double now)
{
    if (c->server_state == NP_NONE || !dir) return;
    c->touch_rest = false;
    if (dir > 0) c->next_due = true;
    else c->prev_due = true;
    if (np_ctl_touch_opa(c, now) > 0) {
        c->touch_on = true;
        c->touch_t = c->info_t = now;
    }
}

int np_fade_opa(double age, double hold)
{
    double k = (age - hold) / NP_TOUCH_FADE_S;
    if (k <= 0) return 255;
    if (k >= 1) return 0;
    int step = (int)(k * NP_TOUCH_FADE_STEPS) + 1;
    return 255 * (NP_TOUCH_FADE_STEPS + 1 - step) / (NP_TOUCH_FADE_STEPS + 1);
}

int np_ctl_touch_opa(const np_ctl_t *c, double now)
{
    if (c->server_state == NP_NONE) return 0;
    int opa = c->touch_on ? np_fade_opa(now - c->touch_t, NP_TOUCH_SHOW_S) : 0;
    if (np_ctl_play_shown(c, now) != NP_PAUSED) return opa;
    int hold = c->touch_rest ? NP_TOUCH_REST_OPA : 255;
    return opa > hold ? opa : hold;
}

void np_ctl_rest(np_ctl_t *c, bool rest) { c->touch_rest = rest; }

static uint32_t key_add(uint32_t h, const char *s)
{
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return (h ^ 0xFF) * 16777619u;
}

void np_ctl_track(np_ctl_t *c, const np_info_t *np, double now)
{
    if (np->state == NP_NONE) {
        c->track_set = false;
        c->track_pos = -1;
        return;
    }
    uint32_t h = 2166136261u;
    h = key_add(h, np->title);
    h = key_add(h, np->artist);
    h = key_add(h, np->album);
    h = key_add(h, np->source);
    h = key_add(h, np->track_id);
    if (c->track_set && h == c->track_key) return;
    c->track_set = true;
    c->track_key = h;
    c->track_t = now;
    c->track_pos = -1;
}

void np_ctl_track_pos(np_ctl_t *c, long long pos_ms, long long dur_ms, double now)
{
    if (c->track_set && c->track_pos >= 0 && dur_ms > NP_RESTART_MIN_MS &&
        c->track_pos >= dur_ms - NP_RESTART_TAIL_MS && pos_ms < NP_RESTART_HEAD_MS && pos_ms < c->track_pos)
        c->track_t = now;
    c->track_pos = pos_ms;
}

int np_ctl_info_opa(np_ctl_t *c, double now, bool paused)
{
    if (paused) {
        c->was_paused = true;
        return 255;
    }
    if (c->was_paused) {
        c->was_paused = false;
        c->resume_t = now;
    }
    int a = np_fade_opa(now - c->track_t, NP_INFO_TRACK_S), b = np_fade_opa(now - c->info_t, NP_TOUCH_SHOW_S);
    int p = np_fade_opa(now - c->resume_t, 0);
    a = a > b ? a : b;
    return a > p ? a : p;
}

bool np_ctl_tap(np_ctl_t *c, np_cmd_kind_t cmd, double now)
{
    if (c->server_state == NP_NONE) return false;
    bool shown = np_ctl_touch_opa(c, now) > 0;
    c->touch_on = true;
    c->touch_rest = false;
    c->touch_t = now;
    c->info_t = now;
    if (!shown) {
        c->last_shown = true;
        return false;
    }
    switch (cmd) {
    case NP_CMD_PLAY:
    case NP_CMD_PAUSE: np_ctl_push(c, now); return true;
    case NP_CMD_NEXT: np_ctl_skip(c, 1, now); return true;
    case NP_CMD_PREVIOUS: np_ctl_skip(c, -1, now); return true;
    default: return false;
    }
}

static bool vol_recent(const np_ctl_t *c, double now) { return c->vol_ever && now - c->vol_t < NP_HOLD_S; }

void np_ctl_turn(np_ctl_t *c, int detents, double now)
{
    if (!detents) return;
    c->touch_rest = false;
    int pts = detents * NP_VOL_STEP;
    if (now - c->vol_t > NP_VOL_SHOW_S) c->vol_gesture = 0;
    int base = vol_recent(c, now) && c->vol >= 0 ? c->vol : c->server_vol;
    c->vol = base >= 0 ? clampi(base + pts, 0, 100) : -1;
    c->vol_unsent = clampi(c->vol_unsent + pts, -100, 100);
    c->vol_gesture = clampi(c->vol_gesture + pts, -100, 100);
    c->vol_t = now;
    c->vol_ever = true;
}

void np_ctl_failed(np_ctl_t *c)
{
    c->play_set = false;
    c->vol_ever = false;
    c->vol_unsent = 0;
}

np_play_t np_ctl_play_shown(const np_ctl_t *c, double now)
{
    if (c->play_set && now - c->play_t < NP_HOLD_S && c->server_state != NP_NONE) return c->play;
    return c->server_state;
}

int np_ctl_volume_shown(const np_ctl_t *c, double now)
{
    if (vol_recent(c, now) && c->vol >= 0) return c->vol;
    return c->server_vol;
}

bool np_ctl_volume_active(const np_ctl_t *c, double now) { return c->vol_ever && now - c->vol_t < NP_VOL_SHOW_S; }

static int emit(np_ctl_t *c, np_cmd_t *out, int n, int max, np_cmd_kind_t k, int delta, double now)
{
    if (n >= max) return n;
    out[n].kind = k;
    out[n].delta = delta;
    c->last = k;
    c->last_t = now;
    c->last_shown = np_ctl_touch_opa(c, now) > 0;
    return n + 1;
}

np_fb_t np_ctl_feedback(const np_ctl_t *c, double now, np_cmd_kind_t *cmd)
{
    double age = now - c->last_t;
    if (c->last == NP_CMD_NONE || c->last == NP_CMD_VOLUME || age >= NP_FLASH_S) return NP_FB_NONE;
    if (c->last_shown && age >= NP_PULSE_S) return NP_FB_NONE;
    *cmd = c->last;
    return c->last_shown ? NP_FB_PULSE : NP_FB_BADGE;
}

int np_ctl_tick(np_ctl_t *c, double now, np_cmd_t *out, int max)
{
    if (c->server_state == NP_NONE) {
        c->play_due = c->next_due = c->prev_due = false;
        c->vol_unsent = 0;
        return 0;
    }
    int n = 0;
    if (c->play_due && n < max) {
        c->play_due = false;
        n = emit(c, out, n, max, c->play == NP_PAUSED ? NP_CMD_PAUSE : NP_CMD_PLAY, 0, now);
    }
    if (c->next_due && n < max) {
        c->next_due = false;
        n = emit(c, out, n, max, NP_CMD_NEXT, 0, now);
    }
    if (c->prev_due && n < max) {
        c->prev_due = false;
        n = emit(c, out, n, max, NP_CMD_PREVIOUS, 0, now);
    }
    if (c->vol_unsent && now - c->vol_sent_t >= NP_VOL_SEND_S && n < max) {
        int d = clampi(c->vol_unsent, -100, 100);
        c->vol_unsent -= d;
        c->vol_sent_t = now;
        n = emit(c, out, n, max, NP_CMD_VOLUME, d, now);
    }
    return n;
}

const char *np_cmd_action(np_cmd_kind_t k)
{
    switch (k) {
    case NP_CMD_PLAY: return "play";
    case NP_CMD_PAUSE: return "pause";
    case NP_CMD_NEXT: return "next";
    case NP_CMD_PREVIOUS: return "previous";
    case NP_CMD_VOLUME: return "volume";
    default: return NULL;
    }
}

static bool id_ok(const char *v)
{
    size_t n = v ? strlen(v) : 0;
    if (n == 0 || n > 128) return false;
    for (size_t i = 0; i < n; i++) {
        char ch = v[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' ||
              ch == '-' || ch == '.' || ch == ':'))
            return false;
    }
    return true;
}

int np_cmd_body(const np_cmd_t *cmd, const char *source, const char *track_id, char *buf, int cap)
{
    const char *a = np_cmd_action(cmd->kind);
    if (!a || cap <= 0) return -1;
    int n = snprintf(buf, cap, "{\"action\":\"%s\"", a);
    if (n >= 0 && n < cap && cmd->kind == NP_CMD_VOLUME)
        n += snprintf(buf + n, cap - n, ",\"delta\":%d", clampi(cmd->delta, -100, 100));
    if (n >= 0 && n < cap && id_ok(source)) n += snprintf(buf + n, cap - n, ",\"source\":\"%s\"", source);
    if (n >= 0 && n < cap && id_ok(track_id)) n += snprintf(buf + n, cap - n, ",\"track_id\":\"%s\"", track_id);
    if (n >= 0 && n < cap) n += snprintf(buf + n, cap - n, "}");
    return n >= 0 && n < cap ? n : -1;
}
