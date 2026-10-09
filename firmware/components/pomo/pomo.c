#include "pomo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

pomo_phase_t pomo_phase_from_wire(const char *s)
{
    if (!s || !*s || strcmp(s, "idle") == 0) return POMO_PHASE_IDLE;
    if (strcmp(s, "focus") == 0) return POMO_PHASE_FOCUS;
    if (strcmp(s, "short_break") == 0) return POMO_PHASE_SHORT_BREAK;
    if (strcmp(s, "long_break") == 0) return POMO_PHASE_LONG_BREAK;
    return POMO_PHASE_UNKNOWN;
}

bool pomo_phase_is_break(pomo_phase_t p) { return p == POMO_PHASE_SHORT_BREAK || p == POMO_PHASE_LONG_BREAK; }

pomo_mode_t pomo_mode(const pomo_state_t *s)
{
    if (s->phase == POMO_PHASE_IDLE) return POMO_MODE_IDLE;
    if (s->paused) return POMO_MODE_PAUSED;
    if (s->running) return POMO_MODE_RUNNING;
    return POMO_MODE_PARKED;
}

pomo_action_t pomo_action_for(const pomo_state_t *s, pomo_input_t in)
{
    if (in == POMO_INPUT_LONG_PUSH) return s->phase == POMO_PHASE_IDLE ? POMO_ACT_NONE : POMO_ACT_STOP;
    if (s->running && !s->paused) return POMO_ACT_PAUSE;
    if (s->phase == POMO_PHASE_IDLE) return POMO_ACT_START;
    return POMO_ACT_RESUME;
}

const char *pomo_action_path(pomo_action_t a)
{
    switch (a) {
    case POMO_ACT_START: return "start";
    case POMO_ACT_PAUSE: return "pause";
    case POMO_ACT_RESUME: return "resume";
    case POMO_ACT_STOP: return "stop";
    case POMO_ACT_SKIP: return "skip";
    default: return NULL;
    }
}

float pomo_fraction(const pomo_state_t *s)
{
    if (s->planned_sec <= 0) return 0;
    float f = (float)s->remaining_sec / (float)s->planned_sec;
    return f < 0 ? 0 : (f > 1 ? 1 : f);
}

int pomo_format_mmss(int seconds, char *buf, size_t n)
{
    if (seconds < 0) seconds = 0;
    return snprintf(buf, n, "%02d:%02d", seconds / 60, seconds % 60);
}

void pomo_clock_init(pomo_clock_t *c) { memset(c, 0, sizeof *c); }

static bool ticking(const pomo_state_t *s) { return s->phase != POMO_PHASE_IDLE && s->running && !s->paused; }

static int clock_remaining(const pomo_clock_t *c, double now)
{
    if (!ticking(&c->state)) return c->rem_base;
    double dt = now - c->t_base;
    int r = c->rem_base - (dt > 0 ? (int)floor(dt) : 0);
    return r < 0 ? 0 : r;
}

void pomo_clock_sync(pomo_clock_t *c, const pomo_state_t *polled, double now)
{
    bool keep = c->valid && ticking(&c->state) && ticking(polled) && c->state.phase == polled->phase &&
                c->state.planned_sec == polled->planned_sec &&
                abs(clock_remaining(c, now) - polled->remaining_sec) <= 1;
    c->state = *polled;
    c->valid = true;
    if (!keep) {
        c->t_base = now;
        c->rem_base = polled->remaining_sec;
    }
}

pomo_state_t pomo_clock_at(const pomo_clock_t *c, double now)
{
    pomo_state_t s = c->state;
    s.remaining_sec = clock_remaining(c, now);
    return s;
}

void pomo_srv_clock_init(pomo_srv_clock_t *c) { memset(c, 0, sizeof *c); }

void pomo_srv_clock_note(pomo_srv_clock_t *c, long long server_now, double sent, double received)
{
    if (received < sent) received = sent;
    double lo = (double)server_now - received, hi = (double)server_now + 1.0 - sent;
    if (c->valid) {
        double grow = (received - c->t_last) * POMO_SRV_DRIFT_PPM * 1e-6;
        if (grow < 0) grow = 0;
        double klo = c->lo - grow, khi = c->hi + grow;
        double nlo = lo > klo ? lo : klo, nhi = hi < khi ? hi : khi;
        c->t_last = received;
        if (nlo <= nhi) {
            c->lo = nlo;
            c->hi = nhi;
            return;
        }
        c->resets++;
    }
    c->valid = true;
    c->lo = lo;
    c->hi = hi;
    c->t_last = received;
}

double pomo_srv_clock_offset(const pomo_srv_clock_t *c) { return c->valid ? (c->lo + c->hi) / 2 : 0; }

double pomo_secs_left(long long ends_at, double offset, double now) { return (double)ends_at + 0.5 - (now + offset); }

void pomo_clock_sync_end(pomo_clock_t *c, const pomo_state_t *polled, long long ends_at, double offset, double now)
{
    double left = pomo_secs_left(ends_at, offset, now);
    if (left < 0) left = 0;
    pomo_state_t s = *polled;
    s.remaining_sec = (int)ceil(left);
    double d = (now + left) - (c->t_base + c->rem_base);
    bool keep = c->valid && ticking(&c->state) && ticking(&s) && c->state.phase == s.phase &&
                c->state.planned_sec == s.planned_sec && fabs(d) < 1.0;
    c->state = s;
    c->valid = true;
    if (keep && d < 0) c->t_base -= d < -0.05 ? 0.05 : -d;
    if (!keep) {
        c->rem_base = s.remaining_sec;
        c->t_base = now - (c->rem_base - left);
    }
}

pomo_est_t pomo_estimate(const pomo_clock_t *c, bool offline, double now)
{
    pomo_est_t e = {.offline = offline};
    if (!c->valid) return e;
    e.has_state = true;
    e.state = pomo_clock_at(c, now);
    e.waiting = offline && ticking(&e.state) && e.state.remaining_sec <= 0;
    return e;
}

const char *pomo_est_label(const pomo_est_t *e)
{
    if (!e->offline) return NULL;
    if (e->waiting) return "WAITING FOR EMBER";
    if (e->has_state && pomo_mode(&e->state) == POMO_MODE_PAUSED) return "PAUSED OFFLINE";
    return "OFFLINE";
}
