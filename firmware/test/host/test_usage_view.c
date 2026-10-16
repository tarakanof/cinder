#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"
#include "pomo_client.h"
#include "src/display/lv_display_private.h"
#include "usage_client.h"
#include "usage_view.h"

#define W 472
#define H 466
#define T0 1782000000LL

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t s_fb[W * H];
static int s_areas;
static long s_px;
static const char *s_dir;

static usage_block_t s_blk;
static usage_clock_t s_clk;
static unsigned s_gen = 1;
static double s_now = 1000;

bool usage_client_take(unsigned *gen, usage_block_t *out, usage_clock_t *clk)
{
    *clk = s_clk;
    if (*gen == s_gen) return false;
    *out = s_blk;
    *gen = s_gen;
    return true;
}

double pomo_client_now(void) { return s_now; }
int64_t esp_timer_get_time(void) { return (int64_t)(s_now * 1e6); }
void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)level, (void)tag, (void)fmt;
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

static void refresh(lv_display_t *d)
{
    s_areas = 0;
    s_px = 0;
    lv_refr_now(d);
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

static int count_rgb(int x0, int y0, int x1, int y1, uint32_t rgb, int tol)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += near_rgb(rgb_at(x, y), rgb, tol);
    return n;
}

static void save(const char *name)
{
    if (!s_dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint32_t c = rgb_at(i % W, i / W);
        fputc((int)(c >> 16), f);
        fputc((int)(c >> 8 & 0xFF), f);
        fputc((int)(c & 0xFF), f);
    }
    fclose(f);
}

static usage_acct_t acct(const char *id, int tool, const char *name, int p5, double pace, int p7)
{
    long long now = T0 + 10000;
    long long start = now - (long long)(pace * USAGE_FIVE_H_S);
    usage_acct_t a = {.tool = tool, .five = {.have = true, .pct = p5, .starts_at = start, .resets_at = start + USAGE_FIVE_H_S},
                      .week = {.have = true, .pct = p7, .resets_at = T0 + 5 * 86400 + 9 * 3600}};
    snprintf(a.id, sizeof a.id, "%s", id);
    snprintf(a.name, sizeof a.name, "%s", name);
    return a;
}

static void publish(const usage_block_t *b)
{
    s_blk = *b;
    s_gen++;
    usage_view_poll();
}

static void frame(lv_display_t *d, double t)
{
    usage_view_frame(t);
    refresh(d);
}

static void tick_pos(float frac, int *x, int *y)
{
    float a = frac * 6.2831853f;
    *x = (int)lroundf(236 + 222 * sinf(a));
    *y = (int)lroundf(233 - 222 * cosf(a));
}

static int lit_dots(int n)
{
    int lit = 0;
    for (int k = 0; k < n; k++) {
        float x, y;
        usage_dot_xy(k, n, &x, &y);
        lit += s_fb[(int)y * W + (int)x] != 0;
    }
    return lit;
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
    usage_view_create(scr);
    s_clk = (usage_clock_t){.have_srv = true, .srv_offset = (double)(T0 + 10000) - s_now, .has_utc_offset = true, .utc_offset = 3600};

    usage_block_t b = {.n = 3};
    b.a[0] = acct("claude:1", USAGE_TOOL_CLAUDE, "Work", 62, 0.48, 41);
    b.a[1] = acct("claude:2", USAGE_TOOL_CLAUDE, "Personal", 93, 0.62, 71);
    b.a[2] = acct("codex:3", USAGE_TOOL_CODEX, "Pro", 18, 0.3, 9);
    publish(&b);
    refresh(d);
    CHECK(!usage_view_open() && usage_view_hint(), "closed with the hint on (93%%)");

    double t = 10;
    CHECK(usage_view_input(&(usage_in_t){.taps = 1}, t) && usage_view_open(), "a tap opens");
    frame(d, t);
    printf("usage view: open: %d areas, %ld px\n", s_areas, s_px);
    save("usage-open-highest-forecast");
    CHECK(near_rgb(rgb_at(236 + 222, 233), USAGE_RED, 12), "Personal at 93%%: red ring at 3 o'clock");
    int tx, ty;
    tick_pos(0.62f, &tx, &ty);
    CHECK(near_rgb(rgb_at(tx, ty), USAGE_TEXT, 16), "the white pace tick at 62%%: %06x", (unsigned)rgb_at(tx, ty));
    CHECK(count_rgb(236 - 36, 82, 236 + 36, 130, 0xD77757, 20) > 300, "the Clawd mark");
    CHECK(lit_dots(3) == 3 && near_rgb(rgb_at(236, (int)(233 + 182)), USAGE_DOT_ON, 12), "three dots, the middle one lit");
    CHECK(count_rgb(110, 290, 362, 318, USAGE_RED, 40) > 80, "the forecast line in red");

    frame(d, t + 0.5);
    CHECK(s_areas == 0, "then still: %d areas", s_areas);
    s_now += 60;
    frame(d, t + 1);
    printf("usage view: a minute later: %d areas, %ld px\n", s_areas, s_px);
    CHECK(s_areas <= 3 && s_px < 40000, "a minute: the lines and the tick only: %d areas, %ld px", s_areas, s_px);

    CHECK(usage_view_input(&(usage_in_t){.hswipes = 1}, t + 2), "a swipe steps");
    frame(d, t + 2);
    printf("usage view: step: %d areas, %ld px\n", s_areas, s_px);
    save("usage-codex");
    CHECK(near_rgb(rgb_at(236 + 222, 233), USAGE_TRACK, 12) && near_rgb(rgb_at(236 + 30, 233 - 222), USAGE_GREEN, 12),
          "Pro at 18%%: green from 12 o'clock, track at 3");
    CHECK(count_rgb(236 - 24, 82, 236 + 24, 130, 0xFFFFFF, 20) > 200, "the OpenAI mark in white");
    float dx, dy;
    usage_dot_xy(2, 3, &dx, &dy);
    CHECK(near_rgb(rgb_at((int)dx, (int)dy), USAGE_DOT_ON, 12) && lit_dots(3) == 3, "the third dot lit");

    usage_view_input(&(usage_in_t){.detents = 1}, t + 3);
    frame(d, t + 3);
    save("usage-wrap-work");
    CHECK(near_rgb(rgb_at(236 + 222, 233), USAGE_GREEN, 12), "a turn wraps to Work (62%%, green)");

    b.a[0].five.pct = 100;
    publish(&b);
    frame(d, t + 4);
    save("usage-limited");
    CHECK(near_rgb(rgb_at(236 - 222, 233), USAGE_RED, 12), "limited: a full red ring");

    b.a[0].five.pct = 41;
    b.a[0].n_models = 2;
    b.a[0].models[0] = (usage_model_t){"Opus", 38};
    b.a[0].models[1] = (usage_model_t){"Sonnet", 12};
    snprintf(b.a[0].name, sizeof b.a[0].name, "Max");
    publish(&b);
    frame(d, t + 5);
    save("usage-models");

    b.a[0].n_models = 0;
    b.a[0].stale = true;
    b.a[0].updated_at = T0 + 10000 - 12 * 60;
    publish(&b);
    frame(d, t + 6);
    save("usage-stale");
    CHECK(near_rgb(rgb_at(236 + 222, 233 - 1), USAGE_GREY, 12) && count_rgb(236 - 36, 82, 236 + 36, 130, USAGE_GREY, 20) > 300,
          "stale: grey ring and mark");
    tick_pos(0.48f, &tx, &ty);
    CHECK(!near_rgb(rgb_at(tx, ty), USAGE_TEXT, 30), "stale: no pace tick");

    usage_view_input(&(usage_in_t){.pushes = 1}, t + 7);
    CHECK(!usage_view_open(), "a push goes back");
    frame(d, t + 7);
    CHECK(s_fb[233 * W + 236 + 222] == 0, "the face is gone");

    usage_block_t one = {.n = 1};
    one.a[0] = acct("claude:9", USAGE_TOOL_CLAUDE, "Max", 55, 0.6, 30);
    publish(&one);
    s_clk.has_utc_offset = false;
    s_gen++;
    usage_view_poll();
    CHECK(!usage_view_hint(), "55%%: no hint");
    usage_view_input(&(usage_in_t){.taps = 1}, 100);
    frame(d, 100);
    save("usage-single-no-dots");
    CHECK(lit_dots(1) == 0, "one account: no dots");
    usage_view_frame(119.9);
    CHECK(usage_view_open(), "still open at 19.9 s");
    usage_view_frame(120);
    CHECK(!usage_view_open(), "back to the bot after 20 s");

    usage_block_t other = {.n = 1};
    other.a[0] = acct("gem:1", USAGE_TOOL_OTHER, "Other", 75, 0.5, 20);
    publish(&other);
    usage_view_input(&(usage_in_t){.taps = 1}, 200);
    frame(d, 200);
    save("usage-other-tool");
    CHECK(count_rgb(236 - 20, 88, 236 + 20, 128, USAGE_GREY, 30) > 100, "another tool: a neutral grey mark");
    usage_block_t none = {0};
    publish(&none);
    CHECK(!usage_view_open(), "no accounts left: back to the bot");

    usage_face_t f;
    usage_face(&b.a[1], (double)(T0 + 10000), true, 3600, &f);
    static uint16_t rb[W * H];
    struct timespec a0, a1;
    clock_gettime(CLOCK_MONOTONIC, &a0);
    for (int i = 0; i < 10; i++) usage_rings_render(rb, W, H, &f, 3, 1);
    clock_gettime(CLOCK_MONOTONIC, &a1);
    printf("usage view: rings bake %.2f ms on the host\n", ((a1.tv_sec - a0.tv_sec) * 1e3 + (a1.tv_nsec - a0.tv_nsec) / 1e6) / 10);

    if (failures) {
        printf("usage view: %d failure(s)\n", failures);
        return 1;
    }
    printf("usage view: all tests passed\n");
    return 0;
}
