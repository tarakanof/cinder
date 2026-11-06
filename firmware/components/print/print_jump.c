#include "print_jump.h"

#include <string.h>

bool pj_blocked(const pj_gate_t *g)
{
    return !g->mood_known || g->waiting || g->usage_open || g->down || g->t - g->input_t < PJ_QUIET_S;
}

void pj_init(pj_t *j) { memset(j, 0, sizeof *j); }

bool pj_tick(pj_t *j, print_seen_t *seen, const pj_in_t *in)
{
    const print_block_t *b = in->b;
    if (in->have_srv && !j->have_boot) {
        j->have_boot = true;
        j->boot_srv = in->srv_now - in->t;
    }
    if (in->blocked || !in->real_ok) j->ready_at = in->t + PJ_SETTLE_S;
    if (j->pend && (!in->page_on || in->on_page || (in->real_ok && (!b || !print_phase_active(b->phase))))) j->pend = false;
    if (b && in->real_ok && in->have_srv && b->event.kind == PRINT_EV_STARTED && print_seen_new(seen, &b->event)) {
        print_seen_mark(seen, &b->event);
        bool before_boot = (double)b->event.at + 1.0 <= j->boot_srv;
        if (!before_boot && in->page_on && !in->on_page && print_phase_active(b->phase)) {
            j->pend = true;
            if (j->ready_at < in->t + PJ_SETTLE_S) j->ready_at = in->t + PJ_SETTLE_S;
        }
    }
    if (!j->pend || !in->real_ok || in->blocked || in->t < j->ready_at) return false;
    j->pend = false;
    return true;
}
