#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "dim_ctl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "page_canvas.h"
#include "page_frame.h"
#include "page_ops.h"
#include "room_face.h"
#include "room_mod.h"
#include "room_page.h"
#include "room_report.h"
#include "src/display/lv_display_private.h"
#include "wx_temp.h"

LV_FONT_DECLARE(font_digits_96);

#define W 472
#define H 466
#define FX 3
#define CX (FX + 233)

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t s_fb[W * H];
static int s_areas, s_px;
static atomic_int s_bakes;
static pthread_t s_main;
static pthread_mutex_t s_gate_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_gate_cv = PTHREAD_COND_INITIALIZER;
static bool s_gate_shut;
static const char *s_dir;
static room_t s_room;
static int s_level = 128;
static _Atomic int64_t s_now_us = 1000000000;
static lv_display_t *s_disp;

int64_t esp_timer_get_time(void)
{
    if (!pthread_equal(pthread_self(), s_main)) {
        pthread_mutex_lock(&s_gate_mu);
        while (s_gate_shut) pthread_cond_wait(&s_gate_cv, &s_gate_mu);
        pthread_mutex_unlock(&s_gate_mu);
    }
    return atomic_load(&s_now_us);
}

static void gate(bool shut)
{
    pthread_mutex_lock(&s_gate_mu);
    s_gate_shut = shut;
    pthread_cond_broadcast(&s_gate_cv);
    pthread_mutex_unlock(&s_gate_mu);
}
void room_mod_get(room_t *out) { *out = s_room; }
uint32_t room_mod_found(void) { return s_room.has; }
int dim_ctl_panel_level(void) { return s_level; }
void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)level;
    if (strcmp(tag, "roompage") == 0 && strncmp(fmt, "ring face", 9) == 0) atomic_fetch_add(&s_bakes, 1);
    if (!getenv("RV_LOG")) return;
    va_list ap;
    va_start(ap, fmt);
    printf("  %s: ", tag);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
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
    for (uint32_t i = 0; i < d->inv_p; i++)
        if (!d->inv_area_joined[i]) {
            s_areas++;
            s_px += lv_area_get_size(&d->inv_areas[i]);
        }
}

static int64_t now_ms(void) { return atomic_load(&s_now_us) / 1000; }

static void readings(void)
{
    int64_t t = now_ms();
    s_room = (room_t){.has = ROOM_HAS_CLIMATE | ROOM_HAS_AIR | ROOM_HAS_LIGHT | ROOM_HAS_PRESENCE, .temp_c = 22.4f, .rh_pct = 43.6f,
                      .climate_ms = t - 4000, .aqi = 2, .tvoc_ppb = 60, .eco2_ppm = 612, .air_validity = ENS160_NORMAL,
                      .air_ms = t - 1000, .lux = 312, .light_ms = t, .present = true, .presence_ms = t};
}

static void frame(double dt)
{
    atomic_fetch_add(&s_now_us, (int64_t)llround(dt * 1e6));
    page_frame_t f = {.t = (double)atomic_load(&s_now_us) / 1e6, .dt = dt};
    page_room_frame(&f);
}

static void refresh(void)
{
    s_areas = s_px = 0;
    lv_refr_now(s_disp);
}

static int ink(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += s_fb[y * W + x] != 0;
    return n;
}

static int count(int x0, int y0, int x1, int y1, uint16_t want)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += s_fb[y * W + x] == want;
    return n;
}

static uint16_t at(float x, float y) { return s_fb[(int)lroundf(y) * W + (int)lroundf(x) + FX]; }

static bool landed(void)
{
    for (int i = 0; i < 2000; i++) {
        frame(1.0 / 60);
        refresh();
        if (ink(0, 0, W, H) > 10000) return true;
        usleep(1000);
    }
    return false;
}

static int s_max_areas, s_max_px, s_sum_areas;

static void arrive(void)
{
    s_max_areas = s_max_px = s_sum_areas = 0;
    for (int i = 0; i < 120; i++) {
        frame(1.0 / 60);
        refresh();
        if (s_areas > s_max_areas) s_max_areas = s_areas;
        if (s_px > s_max_px) s_max_px = s_px;
        s_sum_areas += s_areas;
    }
}

static void save(const char *name)
{
    if (!s_dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/room-%s.ppm", s_dir, name);
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

static void input(int detents)
{
    page_input_t in = {.detents = detents};
    page_room_input(&in, 0);
}

static int value_ink_outside_band(void)
{
    return ink(CX - 190, 175, CX - 150, 260) + ink(CX + 150, 175, CX + 190, 260);
}

static int marker_white(float frac)
{
    float x, y;
    wx_gauge_xy(frac, &x, &y);
    return count((int)x + FX - 14, (int)y - 14, (int)x + FX + 14, (int)y + 14, wx_rgb565(0xF4F4F2));
}

static void test_widths(void)
{
    for (int face = 0; face < ROOM_FACE_N; face++) {
        room_view_t v = {.face = face, .have = true};
        int lo = face == ROOM_FACE_CLIMATE ? -40 : 0, hi = face == ROOM_FACE_CLIMATE ? 85 : ROOM_VALUE_MAX, widest = 0, at_v = 0;
        for (int x = lo; x <= hi; x++) {
            char buf[16];
            room_value_text(&v, x, buf, sizeof buf);
            lv_point_t sz;
            lv_text_get_size(&sz, buf, &font_digits_96, -2, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (sz.x > widest) widest = sz.x, at_v = x;
        }
        CHECK(widest <= 300, "face %d: the widest value (%d) is %d px, over the 300 px band", face, at_v, widest);
        printf("room_view: face %d widest value %d: %d px\n", face, at_v, widest);
        v.have = false;
        char buf[16];
        room_value_text(&v, 0, buf, sizeof buf);
        lv_point_t sz;
        lv_text_get_size(&sz, buf, &font_digits_96, -2, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        CHECK(sz.x > 60 && sz.x <= 300, "face %d: the dashes fit (%d px)", face, sz.x);
    }
    lv_point_t sz;
    lv_text_get_size(&sz, "20000", &font_digits_96, -2, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    CHECK(sz.x > 300, "20000 would not fit (%d px): the clamp is needed", sz.x);
}

int main(int argc, char **argv)
{
    s_dir = argc > 1 ? argv[1] : NULL;
    s_main = pthread_self();
    lv_init();
    lv_tick_set_cb(tick_ms);
    s_disp = lv_display_create(W, H);
    static uint16_t buf[W * 60];
    lv_display_set_buffers(s_disp, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_add_event_cb(s_disp, render_cb, LV_EVENT_RENDER_START, NULL);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    static uint16_t canvas[PAGE_CANVAS_BYTES / 2];
    page_canvas_init(canvas, sizeof canvas);
    test_widths();

    CHECK(!page_room_visible(), "no chip: not visible");
    s_room.has = ROOM_HAS_PRESENCE | ROOM_HAS_PRESSURE;
    CHECK(!page_room_visible(), "the radar and the BMP280 alone: not visible");
    readings();
    CHECK(page_room_visible(), "a chip with a face: visible");
    page_room_show(false);
    CHECK(lv_obj_get_child_count(scr) == 0, "a hide before the first show creates nothing");
    page_room_show(true);
    CHECK(lv_obj_get_child_count(scr) == 0, "a show before room_page_init creates nothing");
    page_room_show(false);
    room_page_init(scr);

    page_room_show(true);
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "the canvas is taken on the first frame, not on show");
    frame(1.0 / 60);
    refresh();
    CHECK(page_canvas_owner() == PAGE_CANVAS_ROOM && ink(0, 0, W, H) == 0, "black while the first ring bakes");
    CHECK(landed(), "the Climate face lands");
    CHECK(s_bakes == 1, "one ring bake: %d", s_bakes);
    CHECK(marker_white(0) > 20 && marker_white(6.4f / 12) == 0, "arrival: the marker starts at the gauge start");
    arrive();
    printf("room_view: Climate arrival over 2 s at 60 Hz: %d areas, at most %d areas and %d px in a frame\n", s_sum_areas, s_max_areas, s_max_px);
    CHECK(s_max_areas <= 6, "the arrival stays within weather's budget (<= 6 areas a frame): %d", s_max_areas);
    save("climate");
    CHECK(marker_white(6.4f / 12) > 20 && marker_white(0) == 0, "after the arrival the marker sits at 22.4° on 16-28°");
    CHECK(at(233 - 222 * 0.7071f, 233 + 222 * 0.7071f) == wx_rgb565(wx_temp_rgb(16)), "the ring starts at 16° in the temperature colour");
    CHECK(at(233 + 222 * 0.7071f, 233 + 222 * 0.7071f) == wx_rgb565(wx_temp_rgb(28)), "and ends at 28°");
    CHECK(count(CX - 150, 160, CX + 150, 272, wx_rgb565(wx_temp_rgb(22))) > 400, "22° in its temperature colour");
    CHECK(value_ink_outside_band() == 0, "the value stays inside the 300 px band");
    CHECK(ink(CX - 100, 280, CX + 100, 320) > 200, "the humidity under it");
    CHECK(ink(CX - 110, 380, CX - 40, 420) > 100 && ink(CX + 40, 380, CX + 110, 420) > 100, "16 and 28 at the gauge ends");
    float dx, dy;
    wx_face_dot_xy(0, 3, &dx, &dy);
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_ON), "three face dots, the first lit");
    wx_face_dot_xy(1, 3, &dx, &dy);
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_OFF), "the second grey");
    frame(1.0 / 60);
    refresh();
    CHECK(s_areas == 0, "no ambient motion: a still frame redraws nothing (%d areas)", s_areas);

    input(1);
    frame(1.0 / 60);
    refresh();
    CHECK(ink(0, 0, W, H) == 0, "a turn: black for the bake");
    CHECK(landed(), "Air lands");
    arrive();
    save("air");
    CHECK(count(CX - 150, 160, CX + 150, 272, wx_rgb565(0x50CCAA)) > 400, "612 in the Good band colour");
    CHECK(count(CX - 120, 325, CX + 120, 350, wx_rgb565(0x50CCAA)) > 50, "the band word in its colour");
    CHECK(value_ink_outside_band() == 0, "inside the band");
    CHECK(ink(CX - 110, 380, CX - 40, 420) == 0, "no end labels on Air");
    wx_face_dot_xy(1, 3, &dx, &dy);
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_ON), "the second dot lit");
    CHECK(marker_white(212.0f / 1600) > 20, "the marker at 612 on 400-2000 ppm");
    CHECK(at(233 - 222 * 0.7071f, 233 + 222 * 0.7071f) == wx_rgb565(0x4FD6CC), "the air ring starts at the Excellent colour");

    s_room.air_validity = ENS160_WARM_UP;
    for (int i = 0; i < 400; i++) {
        frame(1.0 / 60);
        refresh();
        usleep(500);
    }
    save("air-warmup");
    CHECK(s_bakes == 3, "warming up re-bakes the ring only (%d bakes)", s_bakes);
    CHECK(ink(0, 0, W, H) > 10000, "a ring-only change keeps the face on screen");
    CHECK(at(233 - 222 * 0.7071f, 233 + 222 * 0.7071f) == wx_rgb565(ROOM_TRACK_RGB) &&
              at(233, 11) == wx_rgb565(ROOM_TRACK_RGB),
          "warming up: a grey ring");
    CHECK(marker_white(212.0f / 1600) == 0, "no marker");
    CHECK(count(CX - 150, 160, CX + 150, 272, wx_rgb565(ROOM_GREY_RGB)) > 300, "grey dashes");
    CHECK(count(CX - 120, 325, CX + 120, 350, wx_rgb565(0x50CCAA)) == 0, "no band word");
    s_room.air_validity = ENS160_NORMAL;
    s_room.aqi = 5;
    s_room.eco2_ppm = 65000;
    for (int i = 0; i < 400; i++) {
        frame(1.0 / 60);
        refresh();
        usleep(500);
    }
    save("air-unhealthy");
    CHECK(count(CX - 150, 160, CX + 150, 272, wx_rgb565(0xD03060)) > 400 && value_ink_outside_band() == 0,
          "65000 ppm shows as 19999, inside the band");

    input(1);
    CHECK(landed(), "Light lands");
    arrive();
    save("light");
    CHECK(value_ink_outside_band() == 0, "lux inside the band");
    CHECK(marker_white(room_lux_frac(312)) > 20, "the marker on the log gauge");
    CHECK(ink(CX - 120, 325, CX + 120, 350) > 100, "Panel 50 %%");
    s_room.lux = 125000;
    s_level = 255;
    frame(1.0 / 60);
    refresh();
    save("light-bright");
    CHECK(value_ink_outside_band() == 0, "19999 lux inside the band");

    s_room.climate_ms = now_ms() - ROOM_FRESH_MS;
    input(1);
    CHECK(landed(), "Climate again");
    arrive();
    save("climate-stale");
    CHECK(count(CX - 150, 160, CX + 150, 272, wx_rgb565(ROOM_GREY_RGB)) > 300, "a 60 s old reading: grey dashes");
    CHECK(ink(CX - 110, 380, CX - 40, 420) == 0 && ink(CX - 100, 290, CX + 100, 320) == 0, "no end labels, no humidity");
    CHECK(marker_white(0) == 0 && marker_white(0.5f) == 0, "no marker");

    page_room_show(false);
    refresh();
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE && ink(0, 0, W, H) == 0, "hidden: canvas given back, nothing on screen");
    input(1);
    readings();
    s_room.has = ROOM_HAS_CLIMATE | ROOM_HAS_LIGHT;
    page_room_show(true);
    CHECK(landed(), "opens again");
    arrive();
    save("two-faces");
    wx_face_dot_xy(0, 2, &dx, &dy);
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_ON), "two chips: two dots, Climate first (no Air face to open on)");
    wx_face_dot_xy(0, 3, &dx, &dy);
    CHECK(at(dx, dy) != wx_rgb565(WX_DOT_OFF) && at(dx, dy) != wx_rgb565(WX_DOT_ON), "no third dot");
    s_room.has |= ROOM_HAS_AIR;
    s_room.air_ms = now_ms();
    for (int i = 0; i < 400; i++) {
        frame(1.0 / 60);
        refresh();
        usleep(500);
    }
    save("late-chip");
    wx_face_dot_xy(2, 3, &dx, &dy);
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_OFF), "a chip found late adds its dot");
    CHECK(count(CX - 150, 160, CX + 150, 272, wx_rgb565(wx_temp_rgb(22))) > 400, "the face stays on Climate");
    s_room.aqi = 4;
    CHECK(landed(), "AQI 4 while open on the auto face");
    wx_face_dot_xy(1, 3, &dx, &dy);
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_ON), "follows the air to Air, as Weather's auto face does");
    s_room.aqi = 3;
    for (int i = 0; i < 20; i++) {
        frame(1.0 / 60);
        refresh();
    }
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_ON), "AQI 3 keeps Air");

    page_room_show(false);
    page_room_show(true);
    CHECK(landed(), "AQI 3 on open");
    wx_face_dot_xy(0, 3, &dx, &dy);
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_ON), "opens on Climate");
    s_room.aqi = 4;
    page_room_show(false);
    page_room_show(true);
    CHECK(landed(), "AQI 4 on open");
    wx_face_dot_xy(1, 3, &dx, &dy);
    wx_face_dot_xy(1, 3, &dx, &dy);
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_ON), "opens on Air");

    page_room_show(false);
    page_canvas_take(PAGE_CANVAS_WEATHER);
    page_room_show(true);
    for (int i = 0; i < 50; i++) {
        frame(1.0 / 60);
        refresh();
    }
    CHECK(ink(0, 0, W, H) == 0, "while another page holds the canvas: black");
    page_canvas_give(PAGE_CANVAS_WEATHER);
    CHECK(landed(), "and the face once it is free");

    gate(true);
    int bakes = atomic_load(&s_bakes);
    input(1);
    frame(1.0 / 60);
    refresh();
    CHECK(ink(0, 0, W, H) == 0, "a turn while the bake is held: black");
    page_room_show(false);
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "a hide during a bake gives the canvas back");
    s_room.aqi = 2;
    page_room_show(true);
    frame(1.0 / 60);
    refresh();
    CHECK(ink(0, 0, W, H) == 0, "reopened while that bake runs: black");
    gate(false);
    CHECK(landed(), "the held bake lands, then the face the reopened page wants");
    wx_face_dot_xy(0, 3, &dx, &dy);
    CHECK(at(dx, dy) == wx_rgb565(WX_DOT_ON), "Climate, not the Light the turn asked for before the hide");
    CHECK(atomic_load(&s_bakes) == bakes + 2, "the stale Light bake and the Climate one: %d", atomic_load(&s_bakes) - bakes);
    arrive();
    CHECK(count(CX - 150, 160, CX + 150, 272, wx_rgb565(wx_temp_rgb(22))) > 400, "22° after the reopen");

    if (failures) {
        printf("room_view: %d failure(s)\n", failures);
        return 1;
    }
    printf("room_view: all tests passed\n");
    return 0;
}
