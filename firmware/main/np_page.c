#include "ember_client.h"
#include "nowplaying_view.h"
#include "page_frame.h"
#include "page_ops.h"

void page_np_show(bool on) { np_view_show(on); }

void page_np_input(const page_input_t *in, double t)
{
    if (in->detents || in->pushes || in->longs || in->touches || in->wakes || in->hswipes) np_view_input(t);
    if (in->detents) np_view_turn(in->detents, t);
    for (int n = in->pushes; n > 0; n--) np_view_push(t);
    for (int z = 0; z < PAGE_TAP_ZONES; z++)
        for (int n = in->taps[z]; n > 0; n--) np_view_tap(z, t);
    if (in->hswipes) np_view_skip(in->hswipes, t);
}

void page_np_frame(const page_frame_t *f)
{
    char age[24] = "";
    if (f->offline) ember_client_age_text(age, sizeof age);
    np_view_update(f->t, f->offline, age);
}
