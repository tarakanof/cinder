#pragma once

#include <stdbool.h>

#include "print_moment.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The jump to the print page when a print starts (cinder#324). Pure; the glue is moments_print_jump in main/moments.c.
   It takes `started` events from the same print_seen_t the bot's print moments use (the moments take `finished` and
   `attention`), so one store holds the newest real id any consumer took. */

#define PJ_SETTLE_S 0.5

typedef struct {
    bool pend;
    double ready_at;   /* monotonic s: a jump fires no sooner, so the mood and the block of one view poll agree */
    bool have_boot;
    double boot_srv;   /* server Unix s at this boot: the first clock seen minus the uptime then */
} pj_t;

typedef struct {
    double t;               /* monotonic s since boot */
    const print_block_t *b; /* NULL: no print block */
    bool have_srv;
    double srv_now;         /* server Unix s; real events are judged by it */
    bool page_on;           /* the print page is in the page list */
    bool on_page;           /* the print page is the user's page now: there is nothing to jump to, and a held jump is spent */
    bool real_ok;           /* the block is the real view (no scene, demo view, gap between scenes or offline link) */
    bool blocked;           /* an agent waits, the usage screen is open or a long push is held: the jump waits */
} pj_in_t;

void pj_init(pj_t *j);
/* Every frame. True once per `started` event id, when the page should switch to the print page now. A new event is
   marked in the shared store at first sight (not only when fresh: the jump is for a print that is still running, and
   Ember keeps the event 30 min), but one that happened before this boot never jumps: a reboot while the print runs does
   not bring the page back. A taken event waits while `blocked` or not `real_ok`, and fires PJ_SETTLE_S after that ends
   if the print is still under way: a later event or the event ageing out does not cancel it, a demo view's block does not
   either. The page off or already shown, or the print ending, drops it. */
bool pj_tick(pj_t *j, print_seen_t *seen, const pj_in_t *in);

#ifdef __cplusplus
}
#endif
