#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEMO_ID_MAX 64
#define DEMO_VIEW_S 8.0
#define DEMO_HOLD_S 60.0
#define DEMO_ACT_MAX_S 60.0
#define DEMO_SEEN 8
#define DEMO_QUERY_MAX (sizeof "demo_id=&demo_state=playing" + 3 * DEMO_ID_MAX)

typedef enum { DEMO_PAGE_BOT, DEMO_PAGE_POMODORO, DEMO_PAGE_WEATHER, DEMO_PAGE_NOWPLAYING, DEMO_PAGES } demo_page_t;

typedef enum { DEMO_ACT_NONE, DEMO_ACT_MOMENT, DEMO_ACT_CHASE, DEMO_ACT_HOP, DEMO_ACT_POP, DEMO_ACT_BLINK } demo_act_kind_t;

/* moment: 1 glance .. 9 storm, wxm_kind_t's order; chase_style: GC_STYLE_FULL 0, GC_STYLE_HALF 1. */
typedef struct {
    demo_act_kind_t kind;
    int moment;
    bool quiet;
    int chase_style;
} demo_act_t;

/* face: -1 the page's own choice, else 0 temp .. 6 air, wx_face_id_t's order. */
typedef struct {
    bool present;
    char id[DEMO_ID_MAX + 1];
    demo_page_t page;
    int face;
    bool has_view;
    demo_act_t act;
    bool offline;
    bool hold;
} demo_block_t;

bool demo_act_parse(const char *s, demo_act_t *out);
bool demo_page_parse(const char *s, demo_page_t *page, int *face);
const char *demo_page_id(demo_page_t p);
/* d: the view's demo value. False (out->present false) when absent or not a usable block. */
bool demo_block_parse(const cJSON *d, demo_block_t *out);

typedef enum { DEMO_IDLE, DEMO_PLAYING, DEMO_DONE } demo_phase_t;
typedef enum { DEMO_EV_NONE, DEMO_EV_START, DEMO_EV_STOP } demo_ev_t;

typedef struct {
    demo_phase_t phase;
    demo_block_t cur;
    unsigned gen;
    bool ready, taken, act_done, report;
    double start, min_until, max_until;
    char seen[DEMO_SEEN][DEMO_ID_MAX + 1];
    int seen_next;
} demo_t;

void demo_init(demo_t *d);
/* Every view answer: b is the view's block (NULL or !present: none). START also ends a playing one. */
demo_ev_t demo_offer(demo_t *d, const demo_block_t *b, double t);
/* True when the demo playing ends now (timeout or its act done). */
bool demo_tick(demo_t *d, double t);
/* True when a demo was playing: it ends and the input is consumed. */
bool demo_input(demo_t *d);
void demo_act_finished(demo_t *d);
/* The view the demo needs is applied: the start can be taken. */
void demo_ready(demo_t *d);
/* Once per demo, after demo_ready. */
bool demo_take_start(demo_t *d, demo_block_t *out);
bool demo_playing(const demo_t *d);
bool demo_view_wanted(const demo_t *d);
/* "demo_id=<id>&demo_state=playing|done", or "" when there is nothing to report. False when cap is too small. */
bool demo_query(const demo_t *d, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
