#include <stdio.h>
#include <string.h>

#include "day.h"

#define HALF_DAY (DAY_MINUTES / 2)

typedef struct {
    int on, brk, over;
    int now, stop;
} day_at_t;

static int wrap(int m) { return ((m % DAY_MINUTES) + DAY_MINUTES) % DAY_MINUTES; }

static int near(int d) { return wrap(d + HALF_DAY) - HALF_DAY; }

static int guess_now(const day_block_t *b, int e)
{
    if (b->state == DAY_ON && b->over_min > 0 && b->stop_at != DAY_NONE) return wrap(b->stop_at + b->over_min + e);
    if (b->state == DAY_ON && b->started != DAY_NONE) return wrap(b->started + b->on_min + b->break_min + e);
    if (b->state != DAY_BREAK || b->started == DAY_NONE) return DAY_NONE;
    int done = 0;
    for (int i = 0; i < b->n_breaks; i++) done += day_fwd(b->breaks[i][0], b->breaks[i][1]);
    return wrap(b->started + b->on_min + done + b->break_min + e);
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
    int over = b->over_min > 0 ? b->over_min + e : 0;
    if (ui->stop == DAY_NONE || ui->stop == b->stop_at) {
        a.over = over;
    } else if (over > 0 || (in->now_exact && in->now_min != DAY_NONE && b->stop_at != DAY_NONE)) {
        int d = (over > 0 ? over : -day_fwd(in->now_min, b->stop_at)) - ui->stop_delta;
        a.over = d > 0 ? d : 0;
    }
    return a;
}

void day_ui_init(day_ui_t *ui)
{
    memset(ui, 0, sizeof *ui);
    ui->stop = ui->offer_base = ui->offer_at = DAY_NONE;
}

void day_ui_sync(day_ui_t *ui, const day_in_t *in, double t)
{
    const day_block_t *b = in->b;
    if (!b) return;
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
    } else if (ui->stop != DAY_NONE) {
        if (b->stop_at != DAY_NONE && ui->stop_base != DAY_NONE && b->stop_at != ui->stop_base) {
            ui->stop_delta -= near(b->stop_at - ui->stop_base);
            ui->stop_base = b->stop_at;
        }
        if (!ui->stop_dirty && (b->stop_at == ui->stop || t - ui->stop_sent_at > DAY_STOP_HOLD_S)) ui->stop = DAY_NONE;
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
    case DAY_FACE_ON:
    case DAY_FACE_PAST: {
        day_at_t a = day_at(ui, in);
        int base = a.stop != DAY_NONE ? a.stop : a.now != DAY_NONE ? a.now : 0;
        if (ui->stop == DAY_NONE) {
            ui->stop_base = in->b->stop_at;
            ui->stop_delta = 0;
        }
        int next = day_step(base, detents, DAY_STOP_STEP);
        ui->stop_delta += near(next - base);
        ui->stop = next;
        ui->stop_dirty = true;
        ui->stop_turn_at = t;
        break;
    }
    case DAY_FACE_OFFER: {
        int next = day_step(ui->offer_at, detents, DAY_OFFER_STEP);
        int now = in->now_exact && in->now_min != DAY_NONE ? in->now_min : ui->offer_base;
        if (day_fwd(next, now) > HALF_DAY) next = detents > 0 ? now : ui->offer_at;
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
        out[0] = (day_act_t){.kind = DAY_ACT_START, .stop_at = default_stop(b), .at = DAY_NONE};
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

int day_ui_leave(day_ui_t *ui, double t, day_act_t out[DAY_ACTS]) { return flush_stop(ui, t, out); }

static void seg(day_model_t *m, int from, int to, uint32_t rgb)
{
    if (from == DAY_NONE || to == DAY_NONE || from == to || m->n_segs == DAY_SEGS) return;
    m->seg[m->n_segs++] = (day_seg_t){.from = from, .to = to, .rgb = rgb};
}

static void worked(day_model_t *m, const day_block_t *b, int from, int to, uint32_t rgb)
{
    if (from == DAY_NONE || to == DAY_NONE) return;
    int o = b->started != DAY_NONE ? b->started : from;
    int cur = day_fwd(o, from), hi = day_fwd(o, to);
    for (int i = 0; i < b->n_breaks; i++) {
        int bs = day_fwd(o, b->breaks[i][0]), be = day_fwd(o, b->breaks[i][1]);
        if (be < bs || be <= cur || bs >= hi) continue;
        if (bs > cur) seg(m, wrap(o + cur), wrap(o + bs), rgb);
        cur = be;
    }
    if (cur < hi) seg(m, wrap(o + cur), wrap(o + hi), rgb);
}

static void hm(int min, char *buf, size_t n)
{
    unsigned m = min < 0 ? 0 : min > 5999 ? 5999 : (unsigned)min;
    snprintf(buf, n, "%u:%02u", m / 60, m % 60);
}

static void length(int min, char *buf, size_t n)
{
    unsigned m = min < 0 ? 0 : min > 5999 ? 5999 : (unsigned)min;
    if (m < 60) snprintf(buf, n, "%u min", m);
    else if (m % 60 == 0) snprintf(buf, n, "%u h", m / 60);
    else snprintf(buf, n, "%u h %u min", m / 60, m % 60);
}

static void panel(day_panel_t *p, const char *val, const char *cap)
{
    snprintf(p->val, sizeof p->val, "%s", val);
    snprintf(p->cap, sizeof p->cap, "%s", cap);
}

static const char *const TIPS[] = {"stand up and stretch", "get a glass of water", "look out a window",
                                   "walk around the room"};

static void turning_panels(const day_at_t *a, bool clock, day_model_t *m)
{
    char v[16];
    m->top = m->mid;
    day_hhmm(a->stop, v, sizeof v);
    panel(&m->mid, v, "stop at");
    memset(&m->low, 0, sizeof m->low);
    if (a->over > 0) {
        length(a->over, v, sizeof v);
        panel(&m->low, v, "over");
    } else if (clock && a->stop != DAY_NONE) {
        length(day_fwd(a->now, a->stop), v, sizeof v);
        panel(&m->low, v, "planned");
    }
}

static void face_on(const day_block_t *b, const day_at_t *a, day_model_t *m)
{
    char v[16];
    hm(a->on, v, sizeof v);
    panel(&m->mid, v, "on the clock");
    if (b->break_hint) {
        panel(&m->low, "time for a break", "");
    } else if (a->stop != DAY_NONE) {
        day_hhmm(a->stop, v, sizeof v);
        panel(&m->low, v, "stop at");
    }
    m->marks = true;
    worked(m, b, b->started, a->now, DAY_ACCENT);
    seg(m, a->now, a->stop, day_scale(DAY_ACCENT, 0.3f));
    m->tick = a->stop;
    m->tick_rgb = m->turning ? DAY_TEXT : DAY_ACCENT;
    m->dot = a->now;
    m->dot_rgb = DAY_TEXT;
}

static void face_break(const day_block_t *b, const day_at_t *a, day_model_t *m)
{
    char v[16];
    hm(a->brk, v, sizeof v);
    panel(&m->mid, v, "step away");
    panel(&m->low, TIPS[(a->brk < 0 ? 0 : a->brk) % (int)(sizeof TIPS / sizeof TIPS[0])], "");
    m->marks = true;
    if (a->now != DAY_NONE) {
        int from = wrap(a->now - a->brk);
        if (b->started != DAY_NONE && day_fwd(b->started, from) <= day_fwd(b->started, a->now))
            worked(m, b, b->started, from, day_scale(DAY_ACCENT, 0.55f));
        seg(m, from, a->now, DAY_BREAK_RGB);
    }
    m->dot = a->now;
    m->dot_rgb = DAY_BREAK_RGB;
}

static void face_past(const day_block_t *b, const day_at_t *a, day_model_t *m)
{
    char v[16];
    day_hhmm(a->now, v, sizeof v);
    panel(&m->mid, v, "past your stop");
    day_hhmm(a->stop, v, sizeof v);
    panel(&m->low, v, "stop at");
    m->marks = true;
    bool before = b->started != DAY_NONE && a->now != DAY_NONE && a->stop != DAY_NONE &&
                  day_fwd(b->started, a->stop) > day_fwd(b->started, a->now);
    if (before) {
        worked(m, b, b->started, a->now, day_scale(DAY_ACCENT, 0.4f));
    } else {
        worked(m, b, b->started, a->stop, DAY_ACCENT);
        worked(m, b, a->stop, a->now, day_scale(DAY_ACCENT, 0.4f));
    }
    m->tick = a->stop;
    m->tick_rgb = DAY_TEXT;
    m->dot = a->now;
    m->dot_rgb = DAY_TEXT;
    m->glow = true;
}

static void face_offer(const day_ui_t *ui, const day_at_t *a, day_model_t *m)
{
    char v[16];
    day_hhmm(ui->offer_at, v, sizeof v);
    panel(&m->mid, v, "start from here?");
    panel(&m->low, "first reply seen", "");
    m->marks = true;
    if (a->now != DAY_NONE && day_fwd(ui->offer_at, a->now) <= HALF_DAY) seg(m, ui->offer_at, a->now, DAY_ACCENT);
    m->dot = ui->offer_at;
    m->dot_rgb = DAY_TEXT;
}

static void face_summary(const day_block_t *b, const day_at_t *a, day_model_t *m)
{
    const day_summary_t *s = &b->summary;
    char v[16];
    hm(s->on_min, v, sizeof v);
    panel(&m->mid, v, s->in_range ? "in range" : "past your stop");
    if (s->week_days > 0) {
        snprintf(m->low.val, sizeof m->low.val, "%d of %d", s->week_in_range, s->week_days);
        snprintf(m->low.cap, sizeof m->low.cap, "%s this week", s->week_days == 1 ? "day" : "days");
    }
    m->check = true;
    int end = a->now != DAY_NONE ? a->now : b->started != DAY_NONE ? wrap(b->started + s->on_min) : DAY_NONE;
    seg(m, b->started, end, DAY_ACCENT);
    m->tick = b->stop_at;
    m->tick_rgb = DAY_ACCENT;
}

static void face_off(const day_block_t *b, day_model_t *m)
{
    panel(&m->mid, "off", "");
    m->mid.big = 48;
    m->mid.rgb = DAY_OFF_TEXT;
    int r = b->replies_off_last_night;
    if (r > 0) {
        snprintf(m->low.val, sizeof m->low.val, "%d %s", r, r == 1 ? "reply" : "replies");
        snprintf(m->low.cap, sizeof m->low.cap, "off the clock last night");
    } else if (b->agents == 0) panel(&m->low, "no agents", "working");
    else if (b->agents > 0) {
        snprintf(m->low.val, sizeof m->low.val, "%d %s", b->agents, b->agents == 1 ? "agent" : "agents");
        snprintf(m->low.cap, sizeof m->low.cap, "working");
    }
    m->off_ring = true;
    m->moon = true;
}

static const char *hint(day_face_t f)
{
    switch (f) {
    case DAY_FACE_OFF: return "push to start the day";
    case DAY_FACE_ON:
    case DAY_FACE_PAST: return "push: break, hold: clock out";
    case DAY_FACE_BREAK: return "push: back to work, hold: clock out";
    case DAY_FACE_OFFER: return "push: start, hold: dismiss";
    default: return "";
    }
}

void day_model(const day_ui_t *ui, const day_in_t *in, double t, const char *note, day_model_t *m)
{
    memset(m, 0, sizeof *m);
    m->tick = m->dot = DAY_NONE;
    m->face = day_ui_face(ui, in, t);
    if (m->face == DAY_FACE_WAIT) {
        panel(&m->mid, "--:--", in->fed ? "day clock off" : "connecting");
        m->mid.rgb = DAY_DIM;
    } else {
        day_at_t a = day_at(ui, in);
        m->turning = (m->face == DAY_FACE_ON || m->face == DAY_FACE_PAST) && ui->stop_dirty;
        switch (m->face) {
        case DAY_FACE_ON: face_on(in->b, &a, m); break;
        case DAY_FACE_BREAK: face_break(in->b, &a, m); break;
        case DAY_FACE_PAST: face_past(in->b, &a, m); break;
        case DAY_FACE_OFFER: face_offer(ui, &a, m); break;
        case DAY_FACE_SUMMARY: face_summary(in->b, &a, m); break;
        default: face_off(in->b, m); break;
        }
        if (m->turning) turning_panels(&a, in->now_min != DAY_NONE, m);
        else snprintf(m->hint, sizeof m->hint, "%s", hint(m->face));
    }
    if (note) {
        memset(&m->low, 0, sizeof m->low);
        snprintf(m->low.val, sizeof m->low.val, "%s", note);
        m->low.rgb = DAY_NOTE;
    }
}
