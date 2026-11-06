#pragma once

#include <stdbool.h>

#include "print_moment.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The jump to the print page on a new `started` event (cinder#324). Pure; the glue is moments_print_jump. It marks events in the same print_seen_t as the print moments. */

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
    bool blocked;           /* an agent waits, the usage screen is open, input is down or recent, the screen is off or the OTA face shows */
} pj_in_t;

void pj_init(pj_t *j);
/* Every frame; true once per `started` event id, when the page should switch now. Held while blocked or not real_ok, firing PJ_SETTLE_S after that ends; an event from before this boot never fires. Design: docs/features.md "Print page". */
bool pj_tick(pj_t *j, print_seen_t *seen, const pj_in_t *in);

#ifdef __cplusplus
}
#endif
