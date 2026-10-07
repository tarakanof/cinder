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
}

void np_ctl_server(np_ctl_t *c, np_play_t state, int volume, double now)
{
    (void)now;
    c->server_state = state;
    c->server_vol = volume >= 0 && volume <= 100 ? volume : -1;
    if (c->play_set && state == c->play) c->play_set = false;
    if (state == NP_NONE) c->play_set = false;
}

void np_ctl_push(np_ctl_t *c, double now)
{
    if (c->push_waiting && now - c->push_t <= NP_DOUBLE_S) {
        c->push_waiting = false;
        c->next_due = true;
        return;
    }
    c->push_waiting = true;
    c->push_t = now;
}

void np_ctl_long_push(np_ctl_t *c, double now)
{
    (void)now;
    c->prev_due = true;
}

static bool vol_recent(const np_ctl_t *c, double now) { return c->vol_ever && now - c->vol_t < NP_HOLD_S; }

void np_ctl_turn(np_ctl_t *c, int detents, double now)
{
    if (!detents) return;
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
    return n + 1;
}

int np_ctl_tick(np_ctl_t *c, double now, np_cmd_t *out, int max)
{
    if (c->server_state == NP_NONE) {
        c->push_waiting = c->next_due = c->prev_due = false;
        c->vol_unsent = 0;
        return 0;
    }
    int n = 0;
    if (c->push_waiting && now - c->push_t > NP_DOUBLE_S && n < max) {
        c->push_waiting = false;
        np_play_t shown = np_ctl_play_shown(c, now);
        c->play = shown == NP_PLAYING ? NP_PAUSED : NP_PLAYING;
        c->play_set = true;
        c->play_t = now;
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
