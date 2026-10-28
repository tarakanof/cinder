#include <stdio.h>
#include <string.h>

#include "day.h"

#define HALF_DAY (DAY_MINUTES / 2)

typedef struct {
    int on, brk, over;
    int now, stop;
} day_at_t;

static int wrap(int m) { return ((m % DAY_MINUTES) + DAY_MINUTES) % DAY_MINUTES; }

static int guess_now(const day_block_t *b, int e)
{
    if (b->state == DAY_ON && b->over_min > 0 && b->stop_at != DAY_NONE) return wrap(b->stop_at + b->over_min + e);
    if (b->state == DAY_ON && b->started != DAY_NONE) return wrap(b->started + b->on_min + e);
    if (b->state == DAY_BREAK && b->started != DAY_NONE) return wrap(b->started + b->on_min + b->break_min + e);
    return DAY_NONE;
}

static day_at_t day_at(const day_ui_t *ui, const day_in_t *in)
{
    const day_block_t *b = in->b;
    int e = in->age_s > 0 ? (int)(in->age_s / 60) : 0;
    day_at_t a = {.on = b->on_min, .brk = b->break_min, .now = in->now_min,
                  .stop = ui->stop != DAY_NONE ? ui->stop : b->stop_at};
    if (b->state == DAY_ON) a.on += e;
    if (b->state == DAY_BREAK) a.brk += e;
    if (a.now == DAY_NONE) a.now = guess_now(b, e);
    if (b->state != DAY_ON) return a;
    if (a.now != DAY_NONE && b->started != DAY_NONE && a.stop != DAY_NONE) {
        int d = day_fwd(b->started, a.now) - day_fwd(b->started, a.stop);
        a.over = d > 0 ? d : 0;
    } else if (b->over_min > 0) {
        a.over = b->over_min + e;
    }
    return a;
}

void day_ui_init(day_ui_t *ui)
{
    memset(ui, 0, sizeof *ui);
    ui->set_stop = ui->stop = ui->offer_base = ui->offer_at = DAY_NONE;
}

void day_ui_sync(day_ui_t *ui, const day_in_t *in, double t)
{
    const day_block_t *b = in->b;
    if (!b) return;
    if (b->state != DAY_OFF || t - ui->set_at > DAY_SET_IDLE_S) ui->set = false;
    if (!b->summary.has || b->state != DAY_OFF) {
        ui->summary_seen = ui->summary_done = false;
    } else if (!ui->summary_seen) {
        ui->summary_seen = true;
        ui->summary_at = t;
    }
    if (b->state != DAY_OFF || b->offer_start == DAY_NONE) {
        ui->offer_base = ui->offer_at = DAY_NONE;
    } else if (ui->offer_base != b->offer_start) {
        ui->offer_base = ui->offer_at = b->offer_start;
    }
    if (b->state == DAY_OFF) {
        ui->stop = DAY_NONE;
        ui->stop_dirty = false;
    } else if (ui->stop != DAY_NONE && !ui->stop_dirty &&
               (b->stop_at == ui->stop || t - ui->stop_sent_at > DAY_STOP_HOLD_S)) {
        ui->stop = DAY_NONE;
    }
}

day_face_t day_ui_face(const day_ui_t *ui, const day_in_t *in, double t)
{
    const day_block_t *b = in->b;
    if (!b) return DAY_FACE_WAIT;
    switch (b->state) {
    case DAY_ON: return day_at(ui, in).over > 0 ? DAY_FACE_PAST : DAY_FACE_ON;
    case DAY_BREAK: return DAY_FACE_BREAK;
    default: break;
    }
    if (b->summary.has && !ui->summary_done && (!ui->summary_seen || t - ui->summary_at < DAY_SUMMARY_S))
        return DAY_FACE_SUMMARY;
    if (ui->set) return DAY_FACE_SET;
    if (b->offer_start != DAY_NONE) return DAY_FACE_OFFER;
    return DAY_FACE_OFF;
}

static int flush_stop(day_ui_t *ui, double t, day_act_t *out)
{
    if (!ui->stop_dirty) return 0;
    ui->stop_dirty = false;
    ui->stop_sent_at = t;
    *out = (day_act_t){.kind = DAY_ACT_STOP_AT, .stop_at = ui->stop, .at = DAY_NONE};
    return 1;
}

static int default_stop(const day_block_t *b) { return b->stop_at != DAY_NONE ? b->stop_at : 0; }

int day_ui_turn(day_ui_t *ui, const day_in_t *in, int detents, double t, day_act_t out[DAY_ACTS])
{
    (void)out;
    if (!detents) return 0;
    switch (day_ui_face(ui, in, t)) {
    case DAY_FACE_SET:
        ui->set_stop = day_step(ui->set_stop, detents, DAY_STOP_STEP);
        ui->set_at = t;
        break;
    case DAY_FACE_ON:
    case DAY_FACE_PAST: {
        day_at_t a = day_at(ui, in);
        int base = a.stop != DAY_NONE ? a.stop : a.now != DAY_NONE ? a.now : 0;
        ui->stop = day_step(base, detents, DAY_STOP_STEP);
        ui->stop_dirty = true;
        ui->stop_turn_at = t;
        break;
    }
    case DAY_FACE_OFFER: {
        int next = day_step(ui->offer_at, detents, DAY_OFFER_STEP);
        int now = day_at(ui, in).now;
        if (now != DAY_NONE && day_fwd(next, now) > HALF_DAY) next = detents > 0 ? now : ui->offer_at;
        ui->offer_at = next;
        break;
    }
    default:
        break;
    }
    return 0;
}

int day_ui_push(day_ui_t *ui, const day_in_t *in, double t, day_act_t out[DAY_ACTS])
{
    if (in->busy) return 0;
    const day_block_t *b = in->b;
    int n = 0;
    switch (day_ui_face(ui, in, t)) {
    case DAY_FACE_OFF:
        ui->set = true;
        ui->set_stop = default_stop(b);
        ui->set_at = t;
        return 0;
    case DAY_FACE_SET:
        ui->set_at = t;
        out[0] = (day_act_t){.kind = DAY_ACT_START, .stop_at = ui->set_stop, .at = DAY_NONE};
        return 1;
    case DAY_FACE_ON:
    case DAY_FACE_PAST:
        n = flush_stop(ui, t, out);
        out[n++] = (day_act_t){.kind = DAY_ACT_BREAK, .stop_at = DAY_NONE, .at = DAY_NONE};
        return n;
    case DAY_FACE_BREAK:
        n = flush_stop(ui, t, out);
        out[n++] = (day_act_t){.kind = DAY_ACT_RESUME, .stop_at = DAY_NONE, .at = DAY_NONE};
        return n;
    case DAY_FACE_OFFER:
        out[0] = (day_act_t){.kind = DAY_ACT_START, .stop_at = default_stop(b), .at = ui->offer_at};
        return 1;
    case DAY_FACE_SUMMARY:
        ui->summary_done = true;
        return 0;
    default:
        return 0;
    }
}

int day_ui_long(day_ui_t *ui, const day_in_t *in, double t, day_act_t out[DAY_ACTS])
{
    if (in->busy) return 0;
    int n = 0;
    switch (day_ui_face(ui, in, t)) {
    case DAY_FACE_SET:
        ui->set = false;
        return 0;
    case DAY_FACE_ON:
    case DAY_FACE_PAST:
    case DAY_FACE_BREAK:
        n = flush_stop(ui, t, out);
        out[n++] = (day_act_t){.kind = DAY_ACT_STOP, .stop_at = DAY_NONE, .at = DAY_NONE};
        return n;
    case DAY_FACE_OFFER:
        out[0] = (day_act_t){.kind = DAY_ACT_DISMISS, .stop_at = DAY_NONE, .at = DAY_NONE};
        return 1;
    default:
        return 0;
    }
}

int day_ui_tick(day_ui_t *ui, const day_in_t *in, double t, day_act_t out[DAY_ACTS])
{
    if (!ui->stop_dirty || t - ui->stop_turn_at < DAY_TURN_DEBOUNCE_S) return 0;
    if (!in->b || in->b->state == DAY_OFF) {
        ui->stop_dirty = false;
        ui->stop = DAY_NONE;
        return 0;
    }
    return flush_stop(ui, t, out);
}

static void seg(day_model_t *m, int from, int to, uint32_t rgb)
{
    if (from == DAY_NONE || to == DAY_NONE || from == to || m->n_segs == DAY_SEGS) return;
    m->seg[m->n_segs++] = (day_seg_t){.from = from, .to = to, .rgb = rgb};
}

static void hm(int min, char *buf, size_t n) { snprintf(buf, n, "%d:%02d", min / 60, min % 60); }

static void length(int min, char *buf, size_t n)
{
    if (min < 60) snprintf(buf, n, "%d min", min);
    else if (min % 60 == 0) snprintf(buf, n, "%d h", min / 60);
    else snprintf(buf, n, "%d h %02d", min / 60, min % 60);
}

static void face_set(const day_ui_t *ui, const day_block_t *b, const day_at_t *a, day_model_t *m)
{
    day_hhmm(ui->set_stop, m->hero, sizeof m->hero);
    snprintf(m->line2, sizeof m->line2, "stop at");
    int r = b->replies_off_last_night;
    if (r > 0) snprintf(m->line3, sizeof m->line3, "%d %s off the clock last night", r, r == 1 ? "reply" : "replies");
    else if (a->now != DAY_NONE) length(day_fwd(a->now, ui->set_stop), m->line3, sizeof m->line3);
    m->marks = true;
    seg(m, a->now, ui->set_stop, day_scale(DAY_ACCENT, 0.3f));
    m->dot = a->now;
    m->dot_rgb = DAY_TEXT;
    m->tick = ui->set_stop;
    m->tick_rgb = DAY_ACCENT;
}

static void face_on(const day_block_t *b, const day_at_t *a, day_model_t *m)
{
    hm(a->on, m->hero, sizeof m->hero);
    snprintf(m->line2, sizeof m->line2, "on the clock");
    if (b->break_hint) {
        snprintf(m->line3, sizeof m->line3, "time for a break");
    } else if (a->stop != DAY_NONE) {
        char stop[8];
        day_hhmm(a->stop, stop, sizeof stop);
        snprintf(m->line3, sizeof m->line3, "until %s", stop);
    }
    m->marks = true;
    seg(m, b->started, a->now, DAY_ACCENT);
    seg(m, a->now, a->stop, day_scale(DAY_ACCENT, 0.3f));
    m->tick = a->stop;
    m->tick_rgb = DAY_ACCENT;
    m->dot = a->now;
    m->dot_rgb = DAY_TEXT;
}

static void face_break(const day_block_t *b, const day_at_t *a, day_model_t *m)
{
    hm(a->brk, m->hero, sizeof m->hero);
    snprintf(m->line2, sizeof m->line2, "break");
    snprintf(m->line3, sizeof m->line3, "look far away");
    m->marks = true;
    if (a->now != DAY_NONE) {
        int from = wrap(a->now - a->brk);
        if (b->started != DAY_NONE && day_fwd(b->started, from) <= day_fwd(b->started, a->now))
            seg(m, b->started, from, day_scale(DAY_ACCENT, 0.55f));
        seg(m, from, a->now, DAY_BREAK_RGB);
    }
    m->dot = a->now;
    m->dot_rgb = DAY_BREAK_RGB;
}

static void face_past(const day_block_t *b, const day_at_t *a, day_model_t *m)
{
    day_hhmm(a->now, m->hero, sizeof m->hero);
    snprintf(m->line2, sizeof m->line2, "past your stop");
    snprintf(m->line3, sizeof m->line3, "long push to end");
    m->marks = true;
    seg(m, b->started, a->stop, DAY_ACCENT);
    seg(m, a->stop, a->now, day_scale(DAY_ACCENT, 0.4f));
    m->tick = a->stop;
    m->tick_rgb = DAY_TEXT;
    m->dot = a->now;
    m->dot_rgb = DAY_TEXT;
    m->glow = true;
}

static void face_offer(const day_ui_t *ui, const day_at_t *a, day_model_t *m)
{
    day_hhmm(ui->offer_at, m->hero, sizeof m->hero);
    snprintf(m->line2, sizeof m->line2, "start from here?");
    snprintf(m->line3, sizeof m->line3, "first reply seen");
    m->marks = true;
    if (a->now != DAY_NONE && day_fwd(ui->offer_at, a->now) <= HALF_DAY) seg(m, ui->offer_at, a->now, DAY_ACCENT);
    m->dot = ui->offer_at;
    m->dot_rgb = DAY_TEXT;
}

static void face_summary(const day_block_t *b, const day_at_t *a, day_model_t *m)
{
    const day_summary_t *s = &b->summary;
    hm(s->on_min, m->hero, sizeof m->hero);
    snprintf(m->line2, sizeof m->line2, "%s", s->in_range ? "in range" : "past your stop");
    if (s->week_days > 0)
        snprintf(m->line3, sizeof m->line3, "%d of %d %s this week", s->week_in_range, s->week_days,
                 s->week_days == 1 ? "day" : "days");
    m->line3_rgb = DAY_TEXT;
    m->check = true;
    int end = a->now != DAY_NONE ? a->now : b->started != DAY_NONE ? wrap(b->started + s->on_min) : DAY_NONE;
    seg(m, b->started, end, DAY_ACCENT);
    m->tick = b->stop_at;
    m->tick_rgb = DAY_ACCENT;
}

static void face_off(const day_block_t *b, day_model_t *m)
{
    snprintf(m->hero, sizeof m->hero, "off");
    m->hero_digits = false;
    m->hero_rgb = DAY_OFF_TEXT;
    if (b->agents == 0) snprintf(m->line2, sizeof m->line2, "no agents working");
    else if (b->agents > 0)
        snprintf(m->line2, sizeof m->line2, "%d %s working", b->agents, b->agents == 1 ? "agent" : "agents");
    m->line2_rgb = DAY_DIM;
    m->off_ring = true;
    m->moon = true;
}

void day_model(const day_ui_t *ui, const day_in_t *in, double t, const char *note, day_model_t *m)
{
    memset(m, 0, sizeof *m);
    m->tick = m->dot = DAY_NONE;
    m->hero_digits = true;
    m->hero_rgb = DAY_TEXT;
    m->line2_rgb = DAY_NOTE;
    m->line3_rgb = DAY_DIM;
    m->face = day_ui_face(ui, in, t);
    if (m->face == DAY_FACE_WAIT) {
        snprintf(m->hero, sizeof m->hero, "--:--");
        m->hero_rgb = DAY_DIM;
        snprintf(m->line2, sizeof m->line2, "%s", in->fed ? "day clock off" : "connecting");
    } else {
        day_at_t a = day_at(ui, in);
        switch (m->face) {
        case DAY_FACE_SET: face_set(ui, in->b, &a, m); break;
        case DAY_FACE_ON: face_on(in->b, &a, m); break;
        case DAY_FACE_BREAK: face_break(in->b, &a, m); break;
        case DAY_FACE_PAST: face_past(in->b, &a, m); break;
        case DAY_FACE_OFFER: face_offer(ui, &a, m); break;
        case DAY_FACE_SUMMARY: face_summary(in->b, &a, m); break;
        default: face_off(in->b, m); break;
        }
    }
    if (note) {
        snprintf(m->line3, sizeof m->line3, "%s", note);
        m->line3_rgb = DAY_NOTE;
    }
}
