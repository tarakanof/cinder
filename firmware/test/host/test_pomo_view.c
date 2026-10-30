#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "lvgl.h"
#include "page_canvas.h"
#include "pomo.h"
#include "pomo_view.h"

#define W 472
#define H 466
#define ARC 0xFF6A3D

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t s_fb[W * H];
static uint16_t s_canvas[W * H];

static uint32_t tick_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) memcpy(&s_fb[y * W + a->x1], &src[(y - a->y1) * w], (size_t)w * 2);
    lv_display_flush_ready(d);
}

static uint32_t rgb_at(int x, int y)
{
    uint16_t p = s_fb[y * W + x];
    return ((uint32_t)((p >> 11) * 255 / 31) << 16) | ((uint32_t)(((p >> 5) & 63) * 255 / 63) << 8) | (uint32_t)((p & 31) * 255 / 31);
}

static bool near_rgb(uint32_t a, uint32_t b, int tol)
{
    for (int s = 0; s <= 16; s += 8)
        if (abs((int)((a >> s) & 0xFF) - (int)((b >> s) & 0xFF)) > tol) return false;
    return true;
}

static int arc_px(void)
{
    int n = 0;
    for (int y = 225; y < 241; y++)
        for (int x = 450; x < 466; x++) n += near_rgb(rgb_at(x, y), ARC, 12);
    return n;
}

static pomo_est_t running(void)
{
    pomo_est_t e = {.has_state = true};
    e.state = (pomo_state_t){.phase = POMO_PHASE_FOCUS, .running = true, .remaining_sec = 750, .planned_sec = 1500, .round = 1};
    return e;
}

int main(void)
{
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *d = lv_display_create(W, H);
    static uint16_t buf[W * 60];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    page_canvas_init(s_canvas, sizeof s_canvas);
    pomo_view_create(scr);
    pomo_view_set_colors(ARC, 0x4FA9FF);
    pomo_est_t e = running();

    pomo_view_show(true);
    pomo_view_update(&e, 0);
    lv_refr_now(d);
    CHECK(page_canvas_owner() == PAGE_CANVAS_POMO, "showing takes the canvas");
    CHECK(arc_px() > 40, "the ring is drawn on the first show (%d arc pixels)", arc_px());

    pomo_view_show(false);
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "hiding gives the canvas back");
    memset(s_canvas, 0xFF, sizeof s_canvas);
    memset(s_fb, 0, sizeof s_fb);
    lv_refr_now(d);

    pomo_view_show(true);
    pomo_view_update(&e, 1);
    lv_refr_now(d);
    CHECK(page_canvas_owner() == PAGE_CANVAS_POMO, "the second show takes the canvas again");
    CHECK(arc_px() > 40, "the ring is whole after a hide and show, not a partial update on a cleared buffer (%d arc pixels)", arc_px());
    CHECK(s_canvas[5 * W + 5] == 0, "the taken canvas is zeroed, not the last owner's pixels");
    CHECK(rgb_at(5, 5) == 0, "no stale pixels show outside the ring");

    pomo_view_show(false);
    CHECK(page_canvas_take(PAGE_CANVAS_USAGE) != NULL, "another page can take the freed canvas");
    memset(s_fb, 0, sizeof s_fb);
    pomo_view_show(true);
    pomo_view_update(&e, 2);
    lv_refr_now(d);
    CHECK(page_canvas_owner() == PAGE_CANVAS_USAGE, "a busy canvas is not taken");
    CHECK(arc_px() == 0, "no ring while another page holds the canvas (%d arc pixels)", arc_px());

    page_canvas_give(PAGE_CANVAS_USAGE);
    pomo_view_update(&e, 3);
    lv_refr_now(d);
    CHECK(page_canvas_owner() == PAGE_CANVAS_POMO, "the next update retries the take");
    CHECK(arc_px() > 40, "the ring appears once the canvas frees (%d arc pixels)", arc_px());

    if (failures) {
        printf("test_pomo_view: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_pomo_view: ok\n");
    return 0;
}
