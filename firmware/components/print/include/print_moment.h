#pragma once

#include <stdbool.h>

#include "print.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The bot's print moments: B3 (done) and B4 (needs you). Pure; the glue is moments_print_tick in main/moments.c. */

#define PM_FRESH_S 300.0
#define PM_REPEAT_S 120.0
#define PM_SETTLE_S 3.0
#define PM_DEMO_ID_LIMIT 4294967296LL

/* The newest real event id a consumer has taken. Each consumer of print.event owns one, so the moments and the page's
   jump on `started` (cinder#324) never take an event from each other. Demo ids (Ember "Demo mode": below 2^32) are
   never read from it or written to it. */
typedef struct {
    long long id;
} print_seen_t;

void print_seen_init(print_seen_t *s);
bool print_event_is_demo(const print_event_t *e);
/* A real event with an id above the last marked one. */
bool print_seen_new(const print_seen_t *s, const print_event_t *e);
/* Marks a real event; a demo event or an id not above the last one changes nothing. */
void print_seen_mark(print_seen_t *s, const print_event_t *e);

typedef enum { PM_NONE, PM_DONE, PM_ATTENTION } pm_kind_t;

typedef struct {
    print_seen_t seen;
    long long demo_id;
    pm_kind_t pend, demo_pend;
    long long pend_at, pend_took_s, demo_took_s;
    bool armed;
    double next_at;
    double steady_at;
    bool shown_was;
    int mood_was;
} pm_trigger_t;

typedef struct {
    double t;                /* monotonic s */
    const print_block_t *b;  /* NULL: no print block */
    bool have_srv;
    double srv_now;          /* server Unix s; real events are judged by it */
    int mood;                /* 0 rest, 1 working (quiet variant), 2 busy (waiting, error) */
    int bot_mood;            /* the bot's own mood: any change restarts the settle */
    bool shown;              /* the bot page is on screen and the bot is awake */
    bool busy;               /* a moment plays or bakes, or a glint chase runs */
    bool calm;               /* quiet hours */
    bool on;                 /* bot.print_moments */
    bool real_ok;            /* the block is the real view and real moments may play (no scene, demo view or gap between scenes) */
} pm_in_t;

typedef struct {
    pm_kind_t kind;
    bool demo, quiet;
    long long took_s;        /* B3: the job's length, -1 unknown */
} pm_out_t;

void pm_init(pm_trigger_t *tr);
/* Every frame. True and out filled when a moment is due now; pm_shown records it. Real events, B4's repeat and the phase rules only run with real_ok. A demo event plays at once on the bot
   page whatever `on`, the settle, quiet hours and the mood say, and leaves the real memory and B4's repeat alone. */
bool pm_tick(pm_trigger_t *tr, const pm_in_t *in, pm_out_t *out);
void pm_shown(pm_trigger_t *tr, const pm_out_t *out, double t);

#ifdef __cplusplus
}
#endif
