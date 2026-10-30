#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "controls_view.h"
#include "lvgl.h"
#include "src/display/lv_display_private.h"

#define W 472
#define H 466

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t s_fb[W * H];
static int s_areas;
static const char *s_dir;

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

static void render_cb(lv_event_t *e)
{
    lv_display_t *d = lv_event_get_target(e);
    for (uint32_t i = 0; i < d->inv_p; i++) s_areas += !d->inv_area_joined[i];
}

static void refresh(lv_display_t *d)
{
    s_areas = 0;
    lv_refr_now(d);
}

static int ink(int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) n += s_fb[y * W + x] != 0;
    return n;
}

static const char *child_text(lv_obj_t *root, int i) { return lv_label_get_text(lv_obj_get_child(root, i)); }

static void save(const char *name)
{
    if (!s_dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/controls-%s.ppm", s_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t p = s_fb[i];
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    s_dir = argc > 1 ? argv[1] : NULL;
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *d = lv_display_create(W, H);
    static uint16_t buf[W * 60];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_display_add_event_cb(d, render_cb, LV_EVENT_RENDER_START, NULL);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    controls_view_show(false);
    controls_view_update("M4", "Sent");
    controls_view_create(scr);
    CHECK(lv_obj_get_child_count(scr) == 0, "no objects before the page shows");
    controls_view_update("M4", "Sent");
    controls_view_show(false);
    CHECK(lv_obj_get_child_count(scr) == 0, "hiding a hidden page is nothing");

    controls_view_show(true);
    controls_view_show(true);
    CHECK(lv_obj_get_child_count(scr) == 1, "one root, once");
    lv_obj_t *root = lv_obj_get_child(scr, 0);
    CHECK(lv_obj_get_child_count(root) == 3, "title, Mac, result");
    CHECK(strcmp(child_text(root, 0), "Controls") == 0, "title");
    controls_view_update("", "");
    refresh(d);
    CHECK(ink(120, 180) > 500 && ink(190, 400) == 0, "before a reply: the title alone");
    save("title");

    controls_view_update("M4", "Sent");
    refresh(d);
    CHECK(strcmp(child_text(root, 1), "M4") == 0 && strcmp(child_text(root, 2), "Sent") == 0, "Mac and result");
    CHECK(ink(190, 270) > 500 && ink(270, 330) > 200, "both drawn");
    save("sent");

    refresh(d);
    controls_view_update("M4", "Sent");
    refresh(d);
    CHECK(s_areas == 0, "no change, no redraw: %d areas", s_areas);

    const char *no_mac = "No Mac listening";
    controls_view_update("M4", no_mac);
    refresh(d);
    CHECK(strcmp(child_text(root, 2), no_mac) == 0, "a new result");
    save("no-mac");

    controls_view_update("A Mac with a very long name that does not fit", "Too fast");
    refresh(d);
    const char *t = child_text(root, 1);
    CHECK(t[0] == 'A' && strstr(t, "...") != NULL, "a long name ends in dots: %s", t);
    CHECK(ink(0, 190) > 500 && ink(330, H) == 0, "stays on its own line");
    save("long");

    controls_view_show(false);
    CHECK(lv_obj_get_child_count(scr) == 0, "hiding frees every object");
    controls_view_update("M4", "Sent");
    refresh(d);
    CHECK(ink(0, H) == 0, "nothing left on screen");
    controls_view_show(true);
    root = lv_obj_get_child(scr, 0);
    controls_view_update("M4", "Offline");
    refresh(d);
    CHECK(strcmp(child_text(root, 1), "M4") == 0 && strcmp(child_text(root, 2), "Offline") == 0, "shown again: the state comes back");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("controls_view: ok\n");
    return 0;
}
