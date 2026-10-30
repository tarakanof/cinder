#include "controls_client.h"
#include "controls_view.h"
#include "esp_attr.h"
#include "page_frame.h"
#include "page_ops.h"
#include "pomo_client.h"

static EXT_RAM_BSS_ATTR ctl_batch_t s_ctl_batch;

void page_controls_show(bool on)
{
    controls_view_show(on);
    ctl_batch_init(&s_ctl_batch);
}

void page_controls_input(const page_input_t *in, double t)
{
    if (in->detents) ctl_batch_turn(&s_ctl_batch, in->detents);
    if (in->pushes || in->longs) {
        int steps = ctl_batch_flush(&s_ctl_batch, t);
        if (steps) controls_client_send(CTL_TURN, steps);
    }
    for (int n = in->pushes; n > 0; n--) controls_client_send(CTL_PUSH, 0);
    for (int n = in->longs; n > 0; n--) controls_client_send(CTL_LONG_PUSH, 0);
}

void page_controls_frame(const page_frame_t *f)
{
    int steps = ctl_batch_take(&s_ctl_batch, f->t);
    if (steps) controls_client_send(CTL_TURN, steps);
    static EXT_RAM_BSS_ATTR ctl_state_t st;
    controls_client_get(&st);
    controls_view_update(st.target, ctl_note(st.res, pomo_client_now() - st.at, f->offline));
}
