#include "print_jump.h"

#include <string.h>

void pj_init(pj_t *j) { memset(j, 0, sizeof *j); }

bool pj_tick(pj_t *j, print_seen_t *seen, const pj_in_t *in)
{
    const print_block_t *b = in->b;
    if (in->have_srv && !j->have_boot) {
        j->have_boot = true;
        j->boot_srv = in->srv_now - in->t;
    }
    if (j->pend && (!b || !in->page_on || b->event.id != j->pend_id || !print_phase_active(b->phase))) j->pend = false;
    if (b && in->real_ok && in->have_srv && b->event.kind == PRINT_EV_STARTED && print_seen_new(seen, &b->event)) {
        bool before_boot = (double)b->event.at + 1.0 <= j->boot_srv;
        bool fresh = print_seen_take(seen, &b->event, true, in->srv_now);
        if (fresh && !before_boot && in->page_on && print_phase_active(b->phase)) {
            j->pend = true;
            j->pend_id = b->event.id;
        }
    }
    if (!j->pend || !in->real_ok || in->blocked) return false;
    j->pend = false;
    return true;
}
