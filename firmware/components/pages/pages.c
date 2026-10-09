#include "pages.h"

#include <stddef.h>

#include "page_ops.h"
#include "touch_swipe.h"

const page_desc_t PAGES[PAGES_N] = {
    {"bot", "mood", PAGE_TURN | PAGE_GLINT, page_bot_show, page_bot_input, page_bot_frame},
    {"pomodoro", "pomo", 0, page_pomo_show, page_pomo_input, page_pomo_frame},
    {"weather", "weather", 0, page_weather_show, NULL, NULL},
    {"nowplaying", "nowplaying", PAGE_TOUCH_RAW | PAGE_TURN, page_np_show, page_np_input, page_np_frame},
};

int pages_ids(const char **out, int max)
{
    int n = 0;
    for (int i = 0; i < PAGES_N && n < max; i++) out[n++] = PAGES[i].id;
    return n;
}

page_input_t pages_route(const page_desc_t *p, page_input_t in)
{
    if (!p->input) return (page_input_t){0};
    if (!(p->flags & PAGE_TOUCH_RAW)) {
        in.pushes += in.touches;
        in.touches = in.wakes = 0;
    }
    if (!(p->flags & PAGE_TURN)) in.detents = 0;
    return in;
}

const page_desc_t *pages_current(const pages_nav_t *nav) { return &PAGES[nav->page]; }

void pages_show(pages_nav_t *nav, int page)
{
    for (int i = 0; i < PAGES_N; i++) PAGES[i].show(i == page);
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

void pages_settings(pages_nav_t *nav, const knob_settings_t *ks, bool first)
{
    const char *ids[PAGES_N];
    int idx[PAGES_N], home;
    nav->n = knob_settings_page_order(ks, ids, pages_ids(ids, PAGES_N), idx, &home);
    int pos = -1;
    for (int i = 0; i < nav->n; i++) {
        nav->order[i] = idx[i];
        if (idx[i] == nav->page) pos = i;
    }
    if (first || pos < 0) {
        nav->pos = home;
        pages_show(nav, nav->order[home]);
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
