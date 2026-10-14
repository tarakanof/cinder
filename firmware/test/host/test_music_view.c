#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "src/display/lv_display_private.h"
#include "lvgl.h"
#include "music_moment.h"
#include "music_view.h"
#include "esp_timer.h"

#define W 472
#define H 466

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t s_fb[W * H];
static int s_areas;
static long s_px;
static const char *s_dir;

int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

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
    for (uint32_t i = 0; i < d->inv_p; i++) {
        if (d->inv_area_joined[i]) continue;
        s_areas++;
        s_px += (long)lv_area_get_width(&d->inv_areas[i]) * lv_area_get_height(&d->inv_areas[i]);
    }
}

static double refresh(lv_display_t *d)
{
    s_areas = 0;
    s_px = 0;
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    lv_refr_now(d);
    clock_gettime(CLOCK_MONOTONIC, &b);
    return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

static void rgb(int x, int y, int *r, int *g, int *b)
{
    uint16_t p = s_fb[y * W + x];
    *r = (p >> 11) << 3;
    *g = ((p >> 5) & 63) << 2;
    *b = (p & 31) << 3;
}

static bool soft(int x, int y)
{
    int r, g, b;
    rgb(x, y, &r, &g, &b);
    return abs(r - 0xF2) <= 8 && abs(g - 0xA0) <= 8 && abs(b - 0x7E) <= 8;
}

static long lit_in(const mm_box_t *e)
{
    long n = 0;
    for (int y = e->y0; y <= e->y1; y++)
        for (int x = e->x0; x <= e->x1; x++) n += s_fb[y * W + x] != 0;
    return n;
}

static void shot(const char *name)
{
    if (!s_dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        int r, g, b;
        rgb(i % W, i / W, &r, &g, &b);
        fputc(r, f);
        fputc(g, f);
        fputc(b, f);
    }
    fclose(f);
}

static double tick_t(int j) { return MM_PRE_S + (j + 0.5) / MM_FPS; }

static void test_play(lv_display_t *d, lv_obj_t *root)
{
    mm_box_t ext;
    mm_extent(&ext);
    uint32_t cbs = lv_obj_get_event_count(root);
    CHECK(music_view_start(0), "starts");
    CHECK(!music_view_start(0.1), "one at a time");
    CHECK(music_view_busy(), "busy in the pre-roll");
    CHECK(music_view_poll(0.2) < 0 && music_view_eye_px(0.2) == 0, "nothing during the pre-roll");
    refresh(d);
    CHECK(s_areas == 0, "the pre-roll draws nothing");
    int max_areas = 0, frames = 0;
    long max_px = 0, total_px = 0;
    double ms = 0, max_ms = 0;
    for (int j = 0; j < MM_TICKS + 2; j++) {
        double t = tick_t(j);
        double tau = music_view_poll(t);
        if (j < MM_TICKS) CHECK(fabs(tau - (j + 0.5) / MM_FPS) < 1e-9, "tick %d: tau %.3f", j, tau);
        mm_frame_t f;
        mm_frame(j, &f);
        CHECK(music_view_eye_px(t) == f.eye_px, "tick %d: eye px", j);
        music_view_frame(t);
        double r = refresh(d);
        if (s_areas) {
            frames++;
            ms += r;
            if (r > max_ms) max_ms = r;
        }
        if (s_areas > max_areas) max_areas = s_areas;
        if (s_px > max_px) max_px = s_px;
        total_px += s_px;
        CHECK(s_areas <= 1, "tick %d: %d areas", j, s_areas);
        if (j == MM_IN + MM_BEAT * 3) {
            CHECK(soft(MM_BAND_CX, MM_BAND_CY - MM_BAND_R), "the band");
            CHECK(soft(MM_CUP_L_X + 18, MM_CUP_Y + 36 + f.cup_dy) && soft(MM_CUP_R_X + 18, MM_CUP_Y + 36 + f.cup_dy), "the cups");
            CHECK(s_fb[MM_BAND_CY * W + MM_BAND_CX] == 0, "nothing between them");
            shot("music_hold");
        }
        if (j == MM_IN + 40) {
            int best = 0;
            for (int i = 1; i < MM_NOTES; i++)
                if (f.note[i].opa > f.note[best].opa) best = i;
            int r, g, b, o = f.note[best].opa;
            rgb(f.note[best].x + MM_NOTE_OX, f.note[best].y + MM_NOTE_OY, &r, &g, &b);
            CHECK(o > 128 && abs(r - 0xF2 * o / 255) <= 12 && abs(g - 0xA0 * o / 255) <= 12 && abs(b - 0x7E * o / 255) <= 12,
                  "the brightest note (opa %d) drawn at its place: %d,%d,%d", o, r, g, b);
            shot("music_notes");
        }
        if (j == 6) shot("music_arrive");
    }
    CHECK(!music_view_busy() && lv_obj_get_event_count(root) == cbs, "over: the draw callback is gone");
    CHECK(lit_in(&ext) == 0, "nothing left on screen: %ld px", lit_in(&ext));
    printf("music view: %d redraws, max %d area, max %ld px, avg %ld px, render avg %.3f ms max %.3f ms (host)\n", frames, max_areas, max_px,
           frames ? total_px / frames : 0, frames ? ms / frames : 0, max_ms);
    int shown, paint_ms, failed;
    music_view_stats(&shown, &paint_ms, &failed);
    CHECK(shown == 1 && failed == 0, "stats: %d shown, %d failed", shown, failed);
}

static void test_abort(lv_display_t *d)
{
    double base = 100;
    CHECK(music_view_start(base), "starts again");
    for (int j = 0; j < 60; j++) {
        double t = base + tick_t(j);
        music_view_poll(t);
        music_view_frame(t);
        refresh(d);
    }
    double t = base + tick_t(60);
    music_view_abort(t);
    music_view_poll(t);
    CHECK(music_view_eye_px(t) == 0, "the bob stops at once");
    int ticks = 0;
    for (; music_view_busy() && ticks < 40; ticks++) {
        t = base + tick_t(60 + ticks);
        music_view_poll(t);
        music_view_frame(t);
        refresh(d);
        CHECK(s_areas <= 1, "abort tick %d: %d areas", ticks, s_areas);
    }
    CHECK(!music_view_busy() && ticks <= MM_OUT_LEN + 1, "out in %d ticks", ticks);
    mm_box_t ext;
    mm_extent(&ext);
    CHECK(lit_in(&ext) == 0, "nothing left after the abort");

    CHECK(music_view_start(200), "and again");
    music_view_abort(200.1);
    CHECK(!music_view_busy(), "a cut in the pre-roll ends it");
    CHECK(music_view_start(300), "then starts");
    for (int j = 0; j < 30; j++) {
        music_view_poll(300 + tick_t(j));
        music_view_frame(300 + tick_t(j));
        refresh(d);
    }
    music_view_stop();
    refresh(d);
    CHECK(!music_view_busy() && lit_in(&ext) == 0, "stop: gone at once");
    music_view_hold(true);
    CHECK(!music_view_start(400), "held");
    music_view_hold(false);
    CHECK(music_view_start(400), "released");
    music_view_stop();
    refresh(d);
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
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, W, H);
    music_view_create(root);
    refresh(d);
    test_play(d, root);
    test_abort(d);
    if (failures) {
        printf("test_music_view: %d failures\n", failures);
        return 1;
    }
    printf("test_music_view: ok\n");
    return 0;
}
