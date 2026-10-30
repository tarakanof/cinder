#include "page_frame.h"
#include "page_ops.h"
#include "pomo_client.h"
#include "pomo_view.h"

void page_pomo_show(bool on) { pomo_view_show(on); }

void page_pomo_input(const page_input_t *in, double t)
{
    (void)t;
    for (int n = in->pushes; n > 0; n--) pomo_client_action(POMO_INPUT_PUSH);
    for (int n = in->longs; n > 0; n--) pomo_client_action(POMO_INPUT_LONG_PUSH);
}

void page_pomo_frame(const page_frame_t *f)
{
    pomo_snapshot_t snap;
    pomo_client_get(&snap);
    pomo_est_t e = pomo_estimate(&snap.clock, !snap.online, f->t);
    if (snap.disabled) e.has_state = false;
    pomo_view_set_note(pomo_client_note(&snap, f->t));
    pomo_view_update(&e, f->t);
}
