#include "print_moment.h"

#include <math.h>
#include <string.h>

void print_seen_init(print_seen_t *s) { s->id = 0; }

bool print_event_is_demo(const print_event_t *e) { return e->id > 0 && e->id < PM_DEMO_ID_LIMIT; }

bool print_seen_new(const print_seen_t *s, const print_event_t *e) { return e->id >= PM_DEMO_ID_LIMIT && e->id > s->id; }

void print_seen_mark(print_seen_t *s, const print_event_t *e)
{
    if (print_seen_new(s, e)) s->id = e->id;
}

void pm_init(pm_trigger_t *tr)
{
    memset(tr, 0, sizeof *tr);
    tr->steady_at = -INFINITY;
    tr->mood_was = -1;
}

static bool job_runs(print_phase_t p)
{
    return p == PRINT_HEATING || p == PRINT_PREPARING || p == PRINT_PRINTING || p == PRINT_FINISHING || p == PRINT_PAUSED ||
           p == PRINT_ATTENTION;
}

static void take_event(pm_trigger_t *tr, const pm_in_t *in)
{
    const print_block_t *b = in->b;
    if (!b || (b->event.kind != PRINT_EV_FINISHED && b->event.kind != PRINT_EV_ATTENTION)) return;
    const print_event_t *e = &b->event;
    pm_kind_t k = e->kind == PRINT_EV_FINISHED ? PM_DONE : PM_ATTENTION;
    if (print_event_is_demo(e)) {
        if (e->id == tr->demo_id) return;
        tr->demo_id = e->id;
        tr->demo_pend = k;
        tr->demo_took_s = print_took_s(b);
        return;
    }
    if (!in->real_ok || !in->have_srv || !print_seen_new(&tr->seen, e)) return;
    print_seen_mark(&tr->seen, e);
    if (!in->on || in->srv_now - (double)e->at > PM_FRESH_S) return;
    if (k == PM_DONE) {
        tr->pend = PM_DONE;
        tr->pend_at = e->at;
        tr->pend_took_s = print_took_s(b);
    } else {
        tr->armed = true;
        tr->next_at = in->t;
    }
}

bool pm_tick(pm_trigger_t *tr, const pm_in_t *in, pm_out_t *out)
{
    double t = in->t;
    if (in->shown != tr->shown_was || in->bot_mood != tr->mood_was) {
        tr->shown_was = in->shown;
        tr->mood_was = in->bot_mood;
        tr->steady_at = t;
    }
    take_event(tr, in);
    print_phase_t phase = in->b ? in->b->phase : PRINT_UNKNOWN;
    if (in->real_ok && phase != PRINT_ATTENTION) tr->armed = false;
    if (in->real_ok && tr->pend == PM_DONE &&
        (job_runs(phase) || (in->have_srv && in->srv_now - (double)tr->pend_at > PM_FRESH_S)))
        tr->pend = PM_NONE;
    if (tr->demo_pend != PM_NONE && (in->real_ok || !in->b || in->b->event.id != tr->demo_id)) tr->demo_pend = PM_NONE;
    memset(out, 0, sizeof *out);
    out->quiet = in->mood == 1;
    if (in->busy) return false;
    if (tr->demo_pend != PM_NONE && in->page) {
        out->kind = tr->demo_pend;
        out->demo = true;
        out->took_s = tr->demo_took_s;
        return true;
    }
    if (!in->shown || !in->real_ok || !in->on || in->calm || in->mood == 2 || t - tr->steady_at < PM_SETTLE_S) return false;
    if (tr->pend == PM_DONE) {
        out->kind = PM_DONE;
        out->took_s = tr->pend_took_s;
        return true;
    }
    if (tr->armed && t >= tr->next_at) {
        out->kind = PM_ATTENTION;
        out->took_s = -1;
        return true;
    }
    return false;
}

void pm_shown(pm_trigger_t *tr, const pm_out_t *out, double t)
{
    if (out->demo) {
        tr->demo_pend = PM_NONE;
        return;
    }
    if (out->kind == PM_DONE) tr->pend = PM_NONE;
    else if (out->kind == PM_ATTENTION) tr->next_at = t + PM_REPEAT_S;
}
