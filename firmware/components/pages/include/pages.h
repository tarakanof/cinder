#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAGES_N 4

/* PAGE_TOUCH_RAW: taps reach the page as touches and wakes, not as pushes. PAGE_GLINT: the working glint and chase run. */
enum { PAGE_TOUCH_RAW = 1, PAGE_GLINT = 2 };

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

int pages_ids(const char **out, int max);

page_input_t pages_route(const page_desc_t *p, page_input_t in);

#ifdef __cplusplus
}
#endif
