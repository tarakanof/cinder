#pragma once

#include <stdbool.h>

#include "knob_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAGES_N 4

/* PAGE_TOUCH_RAW: taps reach the page as touches and wakes, not as pushes. PAGE_TURN: the page takes detents. PAGE_GLINT: the working glint and chase run. */
enum { PAGE_TOUCH_RAW = 1, PAGE_TURN = 2, PAGE_GLINT = 4 };

typedef struct {
    int detents, pushes, longs, touches, wakes;
} page_input_t;

typedef struct page_frame page_frame_t;

typedef struct {
    const char *id;
    const char *view_block;
    unsigned char flags;
    void (*show)(bool on);
    void (*input)(const page_input_t *in, double t);
    void (*frame)(const page_frame_t *f);
} page_desc_t;

extern const page_desc_t PAGES[PAGES_N];

typedef struct {
    int order[PAGES_N];
    int n, pos, page;
} pages_nav_t;

int pages_ids(const char **out, int max);

page_input_t pages_route(const page_desc_t *p, page_input_t in);

const page_desc_t *pages_current(const pages_nav_t *nav);

void pages_show(pages_nav_t *nav, int page);

void pages_hide_all(void);

void pages_resume(pages_nav_t *nav);

bool pages_step(pages_nav_t *nav, int steps);

void pages_settings(pages_nav_t *nav, const knob_settings_t *ks, bool first);

void pages_dispatch(const pages_nav_t *nav, page_input_t raw, double t);

void pages_frame(const pages_nav_t *nav, const page_frame_t *f);

#ifdef __cplusplus
}
#endif
