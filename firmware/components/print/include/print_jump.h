#pragma once

#include <stdbool.h>

#include "print_moment.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The jump to the print page when a print starts (cinder#324). Pure; the glue is moments_print_jump in main/moments.c.
   It takes `started` events from the same print_seen_t the bot's print moments use (the moments take `finished` and
   `attention`), so one store holds the newest real id any consumer took. */

typedef struct {
    bool pend;
    long long pend_id;
    bool have_boot;
    double boot_srv;   /* server Unix s at this boot: the first clock seen minus the uptime then */
} pj_t;

typedef struct {
    double t;               /* monotonic s since boot */
    const print_block_t *b; /* NULL: no print block */
    bool have_srv;
    double srv_now;         /* server Unix s; real events are judged by it */
    bool page_on;           /* the print page is in the page list */
    bool real_ok;           /* the block is the real view (no scene, demo view, gap between scenes or offline link) */
    bool blocked;           /* an agent waits, the bot sleeps or the usage screen is open: the jump waits */
} pj_in_t;

void pj_init(pj_t *j);
/* Every frame. True once per `started` event id, when the page should switch to the print page now. An event is taken at
   first sight (print_seen_take: marked, and fresh only within PM_FRESH_S of its `at`), but one that happened before
   this boot never jumps: a reboot while the print runs does not bring the page back. A taken event waits while
   `blocked` and fires when that ends if its job is still under way and it is still the view's event; the page being
   off, a demo event or a later event drops it. */
bool pj_tick(pj_t *j, print_seen_t *seen, const pj_in_t *in);

#ifdef __cplusplus
}
#endif
