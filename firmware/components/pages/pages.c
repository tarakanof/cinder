#include "pages.h"

#include <stddef.h>
#include <string.h>

#include "page_ops.h"
#include "touch_swipe.h"

_Static_assert(PAGE_TAP_ZONES == TS_ZONES, "page_input_t.taps is indexed by ts_zone_t");
_Static_assert(PAGES_N <= 32, "pages_visible is a 32-bit mask");

const page_desc_t PAGES[PAGES_N] = {
    {"bot", "mood", PAGE_TOUCH_RAW | PAGE_TURN | PAGE_GLINT, page_bot_show, page_bot_input, page_bot_frame, NULL},
    {"day", "day", PAGE_TURN, page_day_show, page_day_input, page_day_frame, NULL},
    {"weather", "weather", PAGE_TURN, page_weather_show, page_weather_input, NULL, NULL},
    {"room", NULL, PAGE_TURN, page_room_show, page_room_input, page_room_frame, page_room_visible},
    {"nowplaying", "nowplaying", PAGE_TOUCH_RAW | PAGE_TURN, page_np_show, page_np_input, page_np_frame, NULL},
    {"controls", NULL, PAGE_TURN, page_controls_show, page_controls_input, page_controls_frame, NULL},
    {"print", "print", PAGE_TURN, page_print_show, page_print_input, page_print_frame, NULL},
    {"pomodoro", "pomo", 0, page_pomo_show, page_pomo_input, page_pomo_frame, NULL},
};

int pages_ids(const char **out, int max)
{
    int n = 0;
    for (int i = 0; i < PAGES_N && n < max; i++) out[n++] = PAGES[i].id;
    return n;
}

uint32_t pages_visible(void)
{
    uint32_t m = 0;
    for (int i = 0; i < PAGES_N; i++)
        if (!PAGES[i].visible || PAGES[i].visible()) m |= 1u << i;
    return m;
}

page_input_t pages_route(const page_desc_t *p, page_input_t in)
{
    if (!p->input) return (page_input_t){0};
    if (!(p->flags & PAGE_TOUCH_RAW)) {
        in.pushes += in.touches;
        in.touches = in.wakes = in.hswipes = 0;
        for (int z = 0; z < PAGE_TAP_ZONES; z++) in.taps[z] = 0;
    }
    if (!(p->flags & PAGE_TURN)) in.detents = 0;
    return in;
}

const page_desc_t *pages_current(const pages_nav_t *nav) { return &PAGES[nav->page]; }

void pages_show(pages_nav_t *nav, int page)
{
    for (int i = 0; i < PAGES_N; i++)
        if (i != page) PAGES[i].show(false);
    PAGES[page].show(true);
    nav->page = page;
}

void pages_hide_all(void)
{
    for (int i = 0; i < PAGES_N; i++) PAGES[i].show(false);
}

void pages_resume(pages_nav_t *nav) { pages_show(nav, nav->order[nav->pos]); }

bool pages_step(pages_nav_t *nav, int steps)
{
    int pos = ts_page_step(nav->pos, nav->n, steps);
    if (pos == nav->pos) return false;
    nav->pos = pos;
    pages_show(nav, nav->order[pos]);
    return true;
}

int pages_find(const pages_nav_t *nav, const char *id)
{
    for (int i = 0; i < nav->n; i++)
        if (strcmp(PAGES[nav->order[i]].id, id) == 0) return i;
    return -1;
}

bool pages_goto(pages_nav_t *nav, int pos)
{
    if (pos < 0 || pos >= nav->n || pos == nav->pos) return false;
    nav->pos = pos;
    pages_show(nav, nav->order[pos]);
    return true;
}

void pages_settings(pages_nav_t *nav, const knob_settings_t *ks, bool first)
{
    const char *ids[PAGES_N];
    int idx[PAGES_N], home;
    int own = first ? -1 : nav->order[nav->pos];
    bool shown = first || nav->page == own;
    nav->n = knob_settings_page_order(ks, ids, pages_ids(ids, PAGES_N), pages_visible(), idx, &home);
    int pos = -1;
    for (int i = 0; i < nav->n; i++) {
        nav->order[i] = idx[i];
        if (idx[i] == own) pos = i;
    }
    if (first || pos < 0) {
        nav->pos = home;
        if (shown) pages_show(nav, nav->order[home]);
    } else {
        nav->pos = pos;
    }
}

void pages_dispatch(const pages_nav_t *nav, page_input_t raw, double t)
{
    const page_desc_t *p = pages_current(nav);
    page_input_t in = pages_route(p, raw);
    if (p->input) p->input(&in, t);
}

void pages_frame(const pages_nav_t *nav, const page_frame_t *f)
{
    const page_desc_t *p = pages_current(nav);
    if (p->frame) p->frame(f);
}
