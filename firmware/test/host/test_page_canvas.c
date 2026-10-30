#include <stdint.h>
#include <stdio.h>

#include "page_canvas.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

_Static_assert(PAGE_CANVAS_BYTES == 439904, "the 472 x 466 RGB565 frame");

int main(void)
{
    static uint8_t buf[PAGE_CANVAS_BYTES];

    CHECK(!page_canvas_take(PAGE_CANVAS_POMO), "no buffer before init");
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "free before init");

    page_canvas_init(buf, PAGE_CANVAS_BYTES - 1);
    CHECK(!page_canvas_take(PAGE_CANVAS_USAGE), "a buffer too small for the widest user is refused");
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "a refused take holds nothing");

    page_canvas_init(buf, sizeof buf);
    CHECK(page_canvas_take(PAGE_CANVAS_POMO) == buf && page_canvas_owner() == PAGE_CANVAS_POMO, "the first taker holds it");
    CHECK(page_canvas_take(PAGE_CANVAS_POMO) == buf, "taking it again keeps it");
    CHECK(!page_canvas_take(PAGE_CANVAS_USAGE) && !page_canvas_take(PAGE_CANVAS_WEATHER) && !page_canvas_take(PAGE_CANVAS_NP),
          "one owner at a time");
    CHECK(page_canvas_owner() == PAGE_CANVAS_POMO, "a failed take changes nothing");

    page_canvas_give(PAGE_CANVAS_NP);
    page_canvas_give(PAGE_CANVAS_FREE);
    CHECK(page_canvas_owner() == PAGE_CANVAS_POMO, "only the owner gives it back");
    page_canvas_give(PAGE_CANVAS_POMO);
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "given back");
    page_canvas_give(PAGE_CANVAS_POMO);
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "giving twice is harmless");

    CHECK(!page_canvas_take(PAGE_CANVAS_FREE) && page_canvas_owner() == PAGE_CANVAS_FREE, "nobody is not an owner");

    static const page_canvas_owner_t ALL[] = {PAGE_CANVAS_POMO, PAGE_CANVAS_USAGE, PAGE_CANVAS_WEATHER, PAGE_CANVAS_NP};
    for (int i = 0; i < (int)(sizeof ALL / sizeof ALL[0]); i++) {
        CHECK(page_canvas_take(ALL[i]) == buf && page_canvas_owner() == ALL[i], "owner %d takes it in turn", (int)ALL[i]);
        page_canvas_give(ALL[i]);
    }

    CHECK(page_canvas_take(PAGE_CANVAS_NP) == buf, "held before a new init");
    page_canvas_init(buf, sizeof buf);
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "init starts free");

    if (failures) {
        printf("page_canvas: %d failure(s)\n", failures);
        return 1;
    }
    printf("page_canvas: all tests passed\n");
    return 0;
}
