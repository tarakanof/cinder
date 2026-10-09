#include "pages.h"

#include <stddef.h>

#include "page_ops.h"

const page_desc_t PAGES[PAGES_N] = {
    {"bot", "mood", PAGE_GLINT, page_bot_show, page_bot_input, page_bot_frame},
    {"pomodoro", "pomo", 0, page_pomo_show, page_pomo_input, page_pomo_frame},
    {"weather", "weather", 0, page_weather_show, NULL, NULL},
    {"nowplaying", "nowplaying", PAGE_TOUCH_RAW, page_np_show, page_np_input, page_np_frame},
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
    return in;
}
