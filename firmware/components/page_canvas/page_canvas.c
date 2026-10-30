#include "page_canvas.h"

static void *s_buf;
static page_canvas_owner_t s_owner;

void page_canvas_init(void *buf, size_t size)
{
    s_buf = size >= PAGE_CANVAS_BYTES ? buf : NULL;
    s_owner = PAGE_CANVAS_FREE;
}

void *page_canvas_take(page_canvas_owner_t owner)
{
    if (!s_buf || owner == PAGE_CANVAS_FREE) return NULL;
    if (s_owner != PAGE_CANVAS_FREE && s_owner != owner) return NULL;
    s_owner = owner;
    return s_buf;
}

void page_canvas_give(page_canvas_owner_t owner)
{
    if (owner == s_owner) s_owner = PAGE_CANVAS_FREE;
}

page_canvas_owner_t page_canvas_owner(void) { return s_owner; }
