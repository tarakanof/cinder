#pragma once

#include <stddef.h>

#define PAGE_CANVAS_W 472
#define PAGE_CANVAS_H 466
#define PAGE_CANVAS_BYTES (PAGE_CANVAS_W * PAGE_CANVAS_H * 2)

typedef enum {
    PAGE_CANVAS_FREE,
    PAGE_CANVAS_POMO,
    PAGE_CANVAS_USAGE,
    PAGE_CANVAS_WEATHER,
    PAGE_CANVAS_NP,
} page_canvas_owner_t;

/* All calls on the LVGL task. A buffer under PAGE_CANVAS_BYTES is refused: every take then returns NULL. */
void page_canvas_init(void *buf, size_t size);
/* NULL while another owner holds it. Pixels are the last owner's: clear or repaint before they show. */
void *page_canvas_take(page_canvas_owner_t owner);
/* Ignored unless owner holds it. Repoint every LVGL object at other memory first. */
void page_canvas_give(page_canvas_owner_t owner);
page_canvas_owner_t page_canvas_owner(void);
