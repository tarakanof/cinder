#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "lvgl.h"
#include "page_canvas.h"
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
static uint16_t s_canvas[W * H];
static int s_areas;
static long s_px;
static lv_area_t s_inv[LV_INV_BUF_SIZE];
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
        s_inv[s_areas++] = d->inv_areas[i];
        s_px += (long)lv_area_get_width(&d->inv_areas[i]) * lv_area_get_height(&d->inv_areas[i]);
    }
}

static void refresh(lv_display_t *d)
{
    s_areas = 0;
    s_px = 0;
    lv_refr_now(d);
}

static bool redrawn(const lv_area_t *a)
{
    for (int i = 0; i < s_areas; i++)
        if (s_inv[i].x1 <= a->x2 && a->x1 <= s_inv[i].x2 && s_inv[i].y1 <= a->y2 && a->y1 <= s_inv[i].y2) return true;
    return false;
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

static int label_objs(lv_obj_t *scr, lv_area_t *out, const char **txt, int max, int *line_h, lv_obj_t **objs)
{
    lv_obj_t *root = lv_obj_get_child(scr, -1);
    int n = 0;
    for (uint32_t i = 0; root && i < lv_obj_get_child_count(root) && n < max; i++) {
        lv_obj_t *o = lv_obj_get_child(root, (int32_t)i);
        if (!lv_obj_check_type(o, &lv_label_class) || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_obj_get_coords(o, &out[n]);
        if (line_h) line_h[n] = lv_obj_get_style_text_font(o, LV_PART_MAIN)->line_height;
        if (objs) objs[n] = o;
        txt[n++] = lv_label_get_text(o);
    }
    return n;
}

static int labels(lv_obj_t *scr, lv_area_t *out, const char **txt, int max, int *line_h) { return label_objs(scr, out, txt, max, line_h, NULL); }

static const char *label_with(lv_obj_t *scr, const char *prefix)
{
    lv_area_t a[8];
    const char *t[8];
    int n = labels(scr, a, t, 8, NULL);
    for (int i = 0; i < n; i++)
        if (strncmp(t[i], prefix, strlen(prefix)) == 0) return t[i];
    return NULL;
}

static int label_role(lv_obj_t *o)
{
    lv_obj_t *root = lv_obj_get_parent(o);
    int role = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *c = lv_obj_get_child(root, (int32_t)i);
        if (c == o) return role;
        role += lv_obj_check_type(c, &lv_label_class);
    }
    return -1;
}

static const char *role_name(int role)
{
    static const char *const NAMES[] = {"name", "value", "%", "reset", "week"};
    return role >= 0 && role < (int)(sizeof NAMES / sizeof NAMES[0]) ? NAMES[role] : "unknown";
}

static int label_text_w(const char *s, const void *ctx)
{
    const lv_obj_t *o = ctx;
    lv_point_t p;
    lv_text_get_size(&p, s, lv_obj_get_style_text_font(o, LV_PART_MAIN), lv_obj_get_style_text_letter_space(o, LV_PART_MAIN), 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    return p.x;
}

static const char *week_src(lv_obj_t *o, const usage_face_t *f)
{
    return usage_week_text(f, lv_obj_get_content_width(o), label_text_w, o);
}

static void check_labels(lv_obj_t *scr, const char *state, const usage_acct_t *acc, bool name_dots)
{
    usage_face_t f;
    usage_face(acc, s_now + s_clk.srv_offset, s_clk.has_utc_offset, s_clk.utc_offset, &f);
    lv_area_t a[8];
    const char *t[8];
    int lh[8];
    lv_obj_t *o[8];
    int n = label_objs(scr, a, t, 8, lh, o);
    CHECK(n >= 4, "%s: name, value, %%, reset and week labels: %d", state, n);
    for (int i = 0; i < n; i++) {
        int h = lv_area_get_height(&a[i]);
        CHECK(h <= lh[i], "%s: '%s' stays on one line: %d px for a %d px line", state, t[i], h, lh[i]);
        lv_point_t sz;
        lv_text_get_size(&sz, t[i], lv_obj_get_style_text_font(o[i], LV_PART_MAIN), lv_obj_get_style_text_letter_space(o[i], LV_PART_MAIN), 0,
                         LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int role = label_role(o[i]);
        const char *src = role == 0 ? acc->name : role == 1 ? f.value : role == 2 ? "%" : role == 3 ? f.reset : role == 4 ? week_src(o[i], &f) : NULL;
        CHECK(src != NULL, "%s: '%s' is one of the five labels: role %d", state, t[i], role);
        size_t len = strlen(t[i]);
        bool name_dotted = role == 0 && name_dots && len > 3 && len <= strlen(acc->name) && strcmp(t[i] + len - 3, "...") == 0 &&
                           strncmp(t[i], acc->name, len - 3) == 0;
        CHECK(src && (strcmp(t[i], src) == 0 || name_dotted), "%s: the %s label shows '%s', set from '%s'", state, role_name(role), t[i],
              src ? src : "");
        CHECK(sz.x <= lv_area_get_width(&a[i]),
              "%s: '%s' fits its label: %d px in %d", state, t[i], (int)sz.x, (int)lv_area_get_width(&a[i]));
        float corners[4][2] = {{a[i].x1, a[i].y1}, {a[i].x2, a[i].y1}, {a[i].x1, a[i].y2}, {a[i].x2, a[i].y2}};
        for (int k = 0; h < 60 && k < 4; k++)
            CHECK(hypotf(corners[k][0] - 236, corners[k][1] - 233) < 196, "%s: '%s' inside the inner ring", state, t[i]);
        for (int j = i + 1; j < n; j++) {
            bool apart = a[i].x2 < a[j].x1 || a[j].x2 < a[i].x1 || a[i].y2 < a[j].y1 || a[j].y2 < a[i].y1;
            bool value_pct = (strcmp(t[i], "%") == 0 || strcmp(t[j], "%") == 0) && (lv_area_get_height(&a[i]) > 60 || lv_area_get_height(&a[j]) > 60);
            CHECK(apart || value_pct, "%s: '%s' and '%s' overlap", state, t[i], t[j]);
        }
    }
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
    page_canvas_init(s_canvas, sizeof s_canvas);
    usage_view_create(scr);
    s_clk = (usage_clock_t){.have_srv = true, .srv_offset = (double)(T0 + 10000) - s_now, .has_utc_offset = true, .utc_offset = 3600,
                           .fed_at = T0 + 10000 - 5 * 60};

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
    CHECK(label_with(scr, "Opus 38% \xE2\x80\xA2 Sonnet 12%") != NULL, "the models line with their names");
    check_labels(scr, "models", &b.a[0], false);
    b.a[0].models[0].pct = 100;
    b.a[0].models[1].pct = 100;
    publish(&b);
    frame(d, t + 5.2);
    save("usage-models-100");
    CHECK(label_with(scr, "Op 100% \xE2\x80\xA2 So 100%") != NULL, "100/100: the models by two letters, since the names would truncate");
    check_labels(scr, "models at 100/100", &b.a[0], false);

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

    CHECK(usage_view_input(&(usage_in_t){.taps = 1, .pushes = 1}, t + 8) && usage_view_open(),
          "a push in the opening tap's frame: taken, not the bot's, and the screen stays open");
    usage_view_close();

    usage_block_t twin = {.n = 2};
    twin.a[0] = acct("claude:t1", USAGE_TOOL_CLAUDE, "Twin", 40, 0.5, 20);
    twin.a[1] = acct("claude:t2", USAGE_TOOL_CLAUDE, "Twin", 40, 0.5, 20);
    publish(&twin);
    usage_view_input(&(usage_in_t){.taps = 1}, t + 9);
    frame(d, t + 9);
    usage_view_input(&(usage_in_t){.hswipes = 1}, t + 10);
    frame(d, t + 10);
    printf("usage view: dots-only step: %d areas, %ld px\n", s_areas, s_px);
    CHECK(s_areas >= 1 && s_px < 5000, "same rings, another dot: only the dots redraw: %d areas, %ld px", s_areas, s_px);
    int bx, by, bw, bh;
    usage_dots_box(&bx, &by, &bw, &bh);
    CHECK(s_areas == 1 && s_inv[0].x1 == bx && s_inv[0].y1 == by && s_inv[0].x2 == bx + bw - 1 && s_inv[0].y2 == by + bh - 1,
          "the redrawn area is the dots box on screen: %d areas, %d,%d-%d,%d for %d,%d %dx%d", s_areas, (int)s_inv[0].x1, (int)s_inv[0].y1,
          (int)s_inv[0].x2, (int)s_inv[0].y2, bx, by, bw, bh);
    float d1x, d1y;
    usage_dot_xy(1, 2, &d1x, &d1y);
    CHECK(near_rgb(rgb_at((int)d1x, (int)d1y), USAGE_DOT_ON, 12), "the second dot lit on screen: %06x", (unsigned)rgb_at((int)d1x, (int)d1y));
    usage_view_close();
    frame(d, t + 10);

    usage_block_t mod = {.n = 1};
    mod.a[0] = acct("claude:mod", USAGE_TOOL_CLAUDE, "Models", 41, 0.5, 20);
    mod.a[0].n_models = 2;
    mod.a[0].models[0] = (usage_model_t){"Opus", 38};
    mod.a[0].models[1] = (usage_model_t){"Sonnet", 12};
    publish(&mod);
    usage_view_input(&(usage_in_t){.taps = 1}, t + 11);
    frame(d, t + 11);
    CHECK(label_with(scr, "Opus 38% \xE2\x80\xA2 Sonnet 12%") != NULL, "opened on a models account: the full models line in the first frame");
    usage_view_close();
    frame(d, t + 11);
    usage_view_input(&(usage_in_t){.taps = 1}, t + 12);
    frame(d, t + 12);
    CHECK(label_with(scr, "Models") && label_with(scr, "41") && label_with(scr, "resets in ") &&
              label_with(scr, "Opus 38% \xE2\x80\xA2 Sonnet 12%"),
          "the same account reopened with the same strings: name, value, reset and models lines shown");
    usage_view_close();
    frame(d, t + 12);

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

    usage_block_t lng = {.n = 2};
    lng.a[0] = acct("claude:l", USAGE_TOOL_CLAUDE, "Wwwwwwwwwwwwwwwwwwwwwwww", 78, 0.4, 52);
    lng.a[1] = acct("claude:m", USAGE_TOOL_CLAUDE, "Personal account, home", 10, 0.5, 5);
    publish(&lng);
    s_clk.has_utc_offset = false;
    s_gen++;
    usage_view_poll();
    usage_view_input(&(usage_in_t){.taps = 1}, 300);
    frame(d, 300);
    save("usage-long-name-forecast");
    {
        lv_area_t fa[8];
        const char *ft[8];
        int fn = labels(scr, fa, ft, 8, NULL);
        bool fc = false;
        for (int i = 0; i < fn; i++) fc |= strncmp(ft[i], "limit in ", 9) == 0 && strstr(ft[i], "at this pace") != NULL;
        CHECK(fc, "the relative forecast line");
    }
    check_labels(scr, "long name, relative forecast", &lng.a[0], true);
    {
        lv_area_t na[8];
        const char *nt[8];
        int nn = labels(scr, na, nt, 8, NULL), ni = -1;
        for (int i = 0; i < nn; i++)
            if (strncmp(nt[i], "Wwww", 4) == 0) ni = i;
        CHECK(ni >= 0 && strcmp(nt[ni], lng.a[0].name) != 0, "the 24-char name is dotted");
        s_now += 1;
        frame(d, 300.5);
        CHECK(ni >= 0 && !redrawn(&na[ni]), "a second later the dotted name is not set and redrawn again: %d areas, %ld px", s_areas, s_px);
    }
    lng.a[0].five.pct = 100;
    publish(&lng);
    frame(d, 301);
    check_labels(scr, "limited, relative", &lng.a[0], true);

    s_now += 1;
    usage_view_set_offline(true);
    CHECK(!usage_view_hint(), "offline: no hint");
    frame(d, 302);
    save("usage-offline");
    CHECK(near_rgb(rgb_at(236 + 222, 233 - 1), USAGE_GREY, 12), "offline: the ring goes grey");
    lv_area_t la[8];
    const char *lt[8];
    int nl = labels(scr, la, lt, 8, NULL);
    bool said = false;
    for (int i = 0; i < nl; i++) said |= strncmp(lt[i], "no update for", 13) == 0;
    CHECK(said, "offline: 'no update for ...'");
    usage_view_set_offline(false);
    frame(d, 303);
    CHECK(near_rgb(rgb_at(236 + 222, 233 - 1), USAGE_RED, 12), "online again: red");
    lng.a[0].five.pct = 95;
    publish(&lng);
    CHECK(usage_view_hint(), "95%%: hint");
    s_now += 6 * 3600;
    CHECK(!usage_view_hint(), "its 5h window has reset by the knob's clock: hint off");
    usage_view_close();

    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "closed: the page canvas is back: %d", (int)page_canvas_owner());
    CHECK(page_canvas_take(PAGE_CANVAS_WEATHER) != NULL, "a weather bake still running holds the canvas");
    CHECK(!usage_view_input(&(usage_in_t){.taps = 1}, 400) && !usage_view_open(),
          "the page canvas is held: the tap stays the bot's");
    page_canvas_give(PAGE_CANVAS_WEATHER);

    for (int i = 0; i < W * H; i++) s_canvas[i] = 0xFFFF;
    CHECK(usage_view_input(&(usage_in_t){.taps = 1}, 401) && page_canvas_owner() == PAGE_CANVAS_USAGE, "free again: it opens on the canvas");
    frame(d, 401);
    CHECK(s_fb[2 * W + 2] == 0, "the last owner's pixels are cleared: %04x", s_fb[2 * W + 2]);
    usage_view_close();
    CHECK(page_canvas_owner() == PAGE_CANVAS_FREE, "closing gives it back");

    usage_face_t f;
    usage_face(&b.a[1], (double)(T0 + 10000), true, 3600, &f);
    static uint16_t rb[W * H];
    struct timespec a0, a1, a2;
    clock_gettime(CLOCK_MONOTONIC, &a0);
    for (int i = 0; i < 100; i++) usage_rings_render(rb, W, H, &f, 3, 1);
    clock_gettime(CLOCK_MONOTONIC, &a1);
    for (int i = 0; i < 100; i++) usage_ring5_render(rb, W, H, &f);
    clock_gettime(CLOCK_MONOTONIC, &a2);
    printf("usage view: host bake %.3f ms for the whole face, %.3f ms for the 5h ring alone\n",
           ((a1.tv_sec - a0.tv_sec) * 1e3 + (a1.tv_nsec - a0.tv_nsec) / 1e6) / 100,
           ((a2.tv_sec - a1.tv_sec) * 1e3 + (a2.tv_nsec - a1.tv_nsec) / 1e6) / 100);

    if (failures) {
        printf("usage view: %d failure(s)\n", failures);
        return 1;
    }
    printf("usage view: all tests passed\n");
    return 0;
}
