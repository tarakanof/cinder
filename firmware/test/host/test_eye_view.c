#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "bot_raster.h"
#include "bot_shape.h"
#include "bot_view.h"
#include "lvgl.h"
#include "offline_glyph.h"
#include "moment_view.h"
#include "tool_marks.h"
#include <unistd.h>

#define W 472
#define H 466
#define EYE_RGB 0xF4F4F2
#define OFFLINE_RGB 0x5A5A5A
#define RIM_HW 4.5

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t s_fb[W * H];
static float s_want[W * H];
static uint8_t s_ref[W * H];
static uint16_t s_tmp[W * H];

int64_t esp_timer_get_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
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

static void expect(const bot_pose_t *p)
{
    double r = bot_view_radius_px();
    bot_stroke_t st[2];
    bot_eye_strokes(p, BOT_VIEW_EYE_SCALE, st);
    for (int i = 0; i < W * H; i++) s_want[i] = 0;
    for (int e = 0; e < 2; e++) {
        float xs[BOT_STROKE_MAX], ys[BOT_STROKE_MAX];
        double dys[BOT_STROKE_MAX];
        int segs[BOT_STROKE_MAX];
        for (int i = 0; i < st[e].n; i++) {
            xs[i] = (float)(236 + r * (p->offset_x + p->scale_x * st[e].x[i]));
            dys[i] = 233 - r * (p->offset_y * 0.45 - (1 - p->scale_y) + p->scale_y * st[e].y[i]);
            ys[i] = (float)dys[i];
            segs[i] = i;
        }
        double wd = st[e].width * r * (p->scale_x + p->scale_y) / 2, line = bot_lid_line_y(dys, st[e].n, wd, st[e].lid, st[e].keep);
        bot_raster_stroke_ref(s_tmp, s_ref, W, H, 0, 0, xs, ys, st[e].n - 1, segs, (float)wd / 2, EYE_RGB, 1.0f);
        for (int y = 0; y < H; y++) {
            double keep = y + 1 <= line ? 0 : (y >= line ? 1 : y + 1 - line);
            for (int x = 0; x < W; x++) s_want[y * W + x] += (float)(s_ref[y * W + x] / 255.0 * keep);
        }
    }
}

static void compare(const char *name, int *stale, int *off, int *lit)
{
    const int r5 = 0xF4 >> 3, g6 = 0xF4 >> 2, b5 = 0xF2 >> 3;
    *stale = *off = *lit = 0;
    for (int i = 0; i < W * H; i++) {
        uint16_t c = s_fb[i];
        int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
        float a = s_want[i];
        if (a <= 0) {
            *stale += c != 0;
            continue;
        }
        *lit += c != 0;
        if (abs(r - (int)lroundf(r5 * a)) > 2 || abs(g - (int)lroundf(g6 * a)) > 3 || abs(b - (int)lroundf(b5 * a)) > 2) (*off)++;
    }
    printf("eye view %s: %d lit px, %d off, %d stale\n", name, *lit, *off, *stale);
}

static double seg_dist(double px, double py, double ax, double ay, double bx, double by)
{
    double vx = bx - ax, vy = by - ay, t = ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypot(px - ax - t * vx, py - ay - t * vy);
}

static void test_offline(lv_display_t *d, int eye_bottom)
{
    static uint8_t g[OFFLINE_GLYPH_W * OFFLINE_GLYPH_H];
    offline_glyph_raster(g);
    int gx = 236 - OFFLINE_GLYPH_W / 2, gy = 233 + BOT_VIEW_OFFLINE_DY - OFFLINE_GLYPH_H / 2;
    bot_pose_t p = {.mood = BOT_IDLE, .face = BOT_IDLE, .eyes = BOT_EYES_DASH, .scale_x = 1, .scale_y = 1};
    bot_view_update(&p, 100);
    lv_refr_now(d);
    expect(&p);
    bot_view_set_offline(true);
    lv_refr_now(d);
    const int r5 = OFFLINE_RGB >> 19, g6 = (OFFLINE_RGB >> 10) & 63, b5 = (OFFLINE_RGB >> 3) & 31;
    int off = 0, stray = 0, lit = 0, top = H, bottom = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            if (s_want[i] > 0) continue;
            bool in = x >= gx && x < gx + OFFLINE_GLYPH_W && y >= gy && y < gy + OFFLINE_GLYPH_H;
            float a = in ? g[(y - gy) * OFFLINE_GLYPH_W + (x - gx)] / 255.0f : 0;
            uint16_t c = s_fb[i];
            if (!in) {
                stray += c != 0;
                continue;
            }
            if (a > 0.5f) {
                lit++;
                if (y < top) top = y;
                if (y > bottom) bottom = y;
            }
            int r = c >> 11, gg = (c >> 5) & 63, b = c & 31;
            if (abs(r - (int)lroundf(r5 * a)) > 1 || abs(gg - (int)lroundf(g6 * a)) > 2 || abs(b - (int)lroundf(b5 * a)) > 1) off++;
        }
    printf("offline glyph on screen: %d lit px, rows %d..%d, %d off, %d stray; lowest eye row %d\n", lit, top, bottom, off, stray, eye_bottom);
    CHECK(lit > 600 && !off && !stray, "the glyph is drawn once, in grey, where it belongs: %d lit, %d off, %d stray", lit, off, stray);
    CHECK(top - eye_bottom >= 30, "clear of the eyes in every pose above: glyph top %d, eyes end %d", top, eye_bottom);
    double bx[BOT_RING_POINTS], by[BOT_RING_POINTS], r = bot_view_radius_px(), gap = 1e9;
    bot_body_ring(0, bx, by);
    for (int y = 0; y < OFFLINE_GLYPH_H; y++)
        for (int x = 0; x < OFFLINE_GLYPH_W; x++) {
            if (!g[y * OFFLINE_GLYPH_W + x]) continue;
            for (int k = 0; k < BOT_RING_POINTS; k++) {
                int n = (k + 1) % BOT_RING_POINTS;
                double dd = seg_dist(gx + x + 0.5, gy + y + 0.5, 236 + r * bx[k], 233 - r * by[k], 236 + r * bx[n], 233 - r * by[n]);
                if (dd < gap) gap = dd;
            }
        }
    printf("offline glyph: %.1f px from the idle outline's centre line\n", gap);
    CHECK(gap - RIM_HW >= 16, "clear of the 9 px outline and the glint: %.1f px", gap - RIM_HW);
    bot_view_set_offline(false);
    lv_refr_now(d);
    int left = 0;
    for (int y = gy; y < gy + OFFLINE_GLYPH_H; y++)
        for (int x = gx; x < gx + OFFLINE_GLYPH_W; x++) left += s_fb[y * W + x] != 0 && s_want[y * W + x] <= 0;
    CHECK(!left, "online again: the glyph is gone (%d px)", left);
}

#define HOST_RGB 0x00FF00
#define CANVAS_X0 (W / 2 - BOT_VIEW_LABEL_W / 2)
#define CANVAS_X1 (CANVAS_X0 + BOT_VIEW_LABEL_W - 1)
#define CANVAS_Y0 BOT_VIEW_LABEL_Y
#define CANVAS_Y1 (BOT_VIEW_LABEL_Y + BOT_VIEW_LABEL_H - 1)

static const uint32_t MOOD_RGB[] = {[BOT_IDLE] = 0x888888, [BOT_SLEEPY] = 0x888888, [BOT_WORKING] = 0x2EE85E,
                                     [BOT_WAITING] = 0xFFC14D, [BOT_ERROR] = 0xFF3A3A, [BOT_DONE] = 0x4FA9FF};
#define N_MOODS ((int)(sizeof MOOD_RGB / sizeof MOOD_RGB[0]))

static double hue(int r, int g, int b, int *chroma)
{
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    *chroma = mx - mn;
    if (!*chroma) return 0;
    double h = mx == r ? fmod((double)(g - b) / *chroma, 6) : mx == g ? (double)(b - r) / *chroma + 2 : (double)(r - g) / *chroma + 4;
    return h < 0 ? h * 60 + 360 : h * 60;
}

static int pixel_mood(uint16_t v)
{
    int c, best = -1;
    double h = hue((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3, &c), gap = 25;
    if (c < 48) return -1;
    for (int m = 0; m < N_MOODS; m++) {
        int mc;
        double mh = hue((int)(MOOD_RGB[m] >> 16), (int)(MOOD_RGB[m] >> 8 & 0xFF), (int)(MOOD_RGB[m] & 0xFF), &mc);
        double d = fabs(h - mh);
        if (d > 180) d = 360 - d;
        if (mc >= 48 && d < gap) gap = d, best = m;
    }
    return best < 0 ? N_MOODS : best;
}

static bool rim_in(bot_mood_t m)
{
    int n[N_MOODS + 1] = {0}, grey = 0;
    for (int k = 0; k < W * 150; k++) {
        if (!s_fb[k]) continue;
        int pm = pixel_mood(s_fb[k]);
        if (pm < 0) grey++;
        else n[pm]++;
    }
    int hued = 0;
    for (int k = 0; k <= N_MOODS; k++) hued += n[k];
    int c, want = (hue((int)(MOOD_RGB[m] >> 16), (int)(MOOD_RGB[m] >> 8 & 0xFF), (int)(MOOD_RGB[m] & 0xFF), &c), c < 48) ? -1 : (int)m;
    return want < 0 ? grey > 500 && !hued : n[want] > 500 && n[want] == hued;
}

static bool shown_rim(lv_display_t *d, bot_mood_t m)
{
    bot_pose_t p = {.mood = m, .face = m, .eyes = BOT_EYES_DASH, .scale_x = 1, .scale_y = 1};
    for (int i = 0; i < 2000; i++) {
        bot_view_rim_feed(m, false, true);
        bot_view_update(&p, 100 + i * 0.01);
        bot_view_rim_pose(&p, 100 + i * 0.01);
        lv_refr_now(d);
        if (rim_in(m)) return true;
        usleep(5000);
    }
    return false;
}

static void test_rim_hue(lv_display_t *d)
{
    static const bot_mood_t M[] = {BOT_WORKING, BOT_WAITING, BOT_ERROR, BOT_DONE, BOT_IDLE, BOT_WORKING};
    for (size_t k = 0; k < sizeof M / sizeof M[0]; k++) {
        bool in = shown_rim(d, M[k]);
        CHECK(in, "rim %zu: no outline in mood %d's hue within 10 s", k, (int)M[k]);
        int also = -1;
        for (int m = 0; in && m < N_MOODS; m++)
            if (m != (int)M[k] && MOOD_RGB[m] != MOOD_RGB[M[k]] && rim_in((bot_mood_t)m)) also = m;
        CHECK(also < 0, "rim %zu: the mood %d outline also reads as mood %d", k, (int)M[k], also);
    }
    printf("rim hue: working, waiting, error, done and idle outlines each read as their own mood only\n");
}

static bool shown_label(lv_display_t *d, const ember_host_info_t *h, bot_mood_t m, bot_eyes_t eyes)
{
    bot_pose_t p = {.mood = m, .face = m, .eyes = eyes, .gaze_y = 0.6, .scale_x = 1, .scale_y = 1};
    bot_view_set_host(h);
    for (int i = 0; i < 2000; i++) {
        bot_view_rim_feed(m, false, true);
        bot_view_update(&p, 200 + i * 0.01);
        bot_view_rim_pose(&p, 200 + i * 0.01);
        lv_refr_now(d);
        bool rim = rim_in(m);
        for (int k = 0; rim && k < W * H; k++)
            if (s_fb[k] == 0x07E0 && k / W > 233 + 70) return true;
        usleep(5000);
    }
    return false;
}

static void test_label(lv_display_t *d)
{
    struct {
        int tool;
        const char *lead;
        int hosts, waiting, done;
        bot_mood_t mood;
        bot_eyes_t eyes;
    } c[] = {
        {EMBER_TOOL_CLAUDE, "macbook-pro", 2, 3, 0, BOT_WAITING, BOT_EYES_ROUND},
        {EMBER_TOOL_CODEX, "macbook-pro", 2, 3, 0, BOT_WAITING, BOT_EYES_ROUND},
        {EMBER_TOOL_CLAUDE, "m4", 1, 3, 0, BOT_WAITING, BOT_EYES_ROUND},
        {EMBER_TOOL_CLAUDE, "macbook-pro", 2, 0, 3, BOT_DONE, BOT_EYES_HAPPY},
        {EMBER_TOOL_CODEX, "m4", 1, 0, 3, BOT_DONE, BOT_EYES_HAPPY},
        {EMBER_TOOL_CLAUDE, "macbook-pro", 2, 3, 0, BOT_WAITING, BOT_EYES_ROUND},
    };
    for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
        ember_host_info_t h = {.color = HOST_RGB, .tool = (uint8_t)c[k].tool};
        ember_host_lead_info("", c[k].lead, c[k].hosts, "learn-language", c[k].waiting, c[k].done, &h);
        bool sub = h.sub[0] != 0;
        CHECK(!strcmp(h.text, "learn-la... +2") && sub == (c[k].hosts > 1), "label %zu: '%s' over '%s'", k, h.text, h.sub);
        if (!shown_label(d, &h, c[k].mood, c[k].eyes)) {
            CHECK(0, "label %zu: no label on the %s outline within 10 s", k, c[k].mood == BOT_DONE ? "done" : "waiting");
            continue;
        }
        static int ix[W * H], iy[W * H], gx[W * H], gy[W * H];
        int ni = 0, ng = 0, main_px = 0, edge = 0, mx0 = W, mx1 = -1, my0 = H, my1 = -1;
        for (int y = 233 + 70; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint16_t v = s_fb[y * W + x];
                double r = hypot(x + 0.5 - 236, y + 0.5 - 233);
                if (!v) continue;
                int rr = v >> 11, gg = (v >> 5) & 63, bb = v & 31;
                bool green = !rr && !bb && gg >= 4, icon = (rr || bb) && r < 188;
                if (!green && !icon) continue;
                edge += x == CANVAS_X0 || x == CANVAS_X1 || y == CANVAS_Y0 || y == CANVAS_Y1;
                if (icon) ix[ni] = x, iy[ni++] = y;
                else if (r < 151) gx[ng] = x, gy[ng++] = y;
                else {
                    main_px++;
                    mx0 = x < mx0 ? x : mx0, mx1 = x > mx1 ? x : mx1, my0 = y < my0 ? y : my0, my1 = y > my1 ? y : my1;
                }
            }
        uint32_t rgb;
        int mw, mh, mask = 0, faint = 0;
        const uint8_t *m = tool_mark(c[k].tool, &rgb, &mw, &mh);
        for (int i = 0; m && i < mw * mh; i++) mask += m[i] >= 32, faint += m[i] > 0;
        double gap = 1e9;
        for (int i = 0; i < ni; i++)
            for (int j = 0; j < ng; j++) gap = fmin(gap, hypot(ix[i] - gx[j], iy[i] - gy[j]));
        printf("label %s %s, host line '%s': %d main px in x %d..%d y %d..%d, %d host-line px, icon %d px (mask %d-%d), %d at the canvas edge",
               c[k].mood == BOT_DONE ? "done" : "waiting", h.text, h.sub, main_px, mx0, mx1, my0, my1, ng, ni, mask, faint, edge);
        if (sub) printf(", icon to host line %.1f px", gap);
        printf("\n");
        CHECK(main_px >= 1200, "label %zu: the whole project is drawn (%d px)", k, main_px);
        CHECK(abs(mx0 - 154) <= 1 && abs(mx1 - 320) <= 1 && abs(my0 - 368) <= 1 && abs(my1 - 408) <= 1,
              "label %zu: the project sits on its arc, x 154..320 y 368..408 (x %d..%d y %d..%d)", k, mx0, mx1, my0, my1);
        CHECK(sub ? ng > 300 : ng == 0, "label %zu: the host line %s (%d px)", k, sub ? "is drawn" : "is absent", ng);
        CHECK(!edge, "label %zu: nothing clipped at the canvas edge (%d px)", k, edge);
        CHECK(ni >= mask && ni <= faint, "label %zu: the whole icon (%d px, mask %d-%d)", k, ni, mask, faint);
        CHECK(!sub || gap >= 4, "label %zu: icon at least 4 px from the host line (%.1f)", k, gap);
    }
}

static int hint_px(void)
{
    int n = 0;
    for (int y = 30; y < 160; y++)
        for (int x = 40; x < 200; x++) {
            uint16_t v = s_fb[y * W + x];
            int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
            n += r >= 200 && g <= 100 && b <= 100;
        }
    return n;
}

static void save_ppm(const char *dir, const char *name)
{
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t v = s_fb[i];
        fputc((v >> 11) * 255 / 31, f);
        fputc(((v >> 5) & 63) * 255 / 63, f);
        fputc((v & 31) * 255 / 31, f);
    }
    fclose(f);
}

static int hint_frames(lv_display_t *d, bot_mood_t m, double t0, int want)
{
    bot_pose_t p = {.mood = m, .face = m, .eyes = BOT_EYES_DASH, .gaze_y = 0.2, .scale_x = 1, .scale_y = 1};
    int n = 0;
    for (int i = 0; i < 2000; i++) {
        double t = t0 + i * 0.01;
        bot_view_rim_feed(m, false, true);
        bot_view_update(&p, t);
        bot_view_rim_pose(&p, t);
        bot_view_tick(t, false);
        lv_refr_now(d);
        n = hint_px();
        if (want ? n > want : n < 20) return n;
        usleep(5000);
    }
    return n;
}

static void test_hint(lv_display_t *d, const char *dir)
{
    bot_view_set_host(&(ember_host_info_t){.color = -1});
    CHECK(shown_rim(d, BOT_WORKING), "the working outline before the hint");
    bot_view_set_hint(true);
    int on = hint_frames(d, BOT_WORKING, 400, 900);
    CHECK(on > 900 && on < 2000, "the limit hint: a short red arc on the working rim: %d px", on);
    save_ppm(dir, "bot-hint");
    bot_view_set_hint(false);
    int off = hint_frames(d, BOT_WORKING, 430, 0);
    CHECK(off < 20, "hint off: the rim back to green: %d red px", off);
    bot_view_set_hint(true);
    on = hint_frames(d, BOT_IDLE, 460, 900);
    CHECK(on > 900, "on the idle rim too: %d px", on);
    bot_view_set_hint(false);
    hint_frames(d, BOT_IDLE, 490, 0);
    printf("eye view: limit hint %d px on the rim, gone when off\n", on);
}

static lv_obj_t *s_page;
static uint16_t s_base[W * H], s_inc[W * H];

static void orbit_frame(lv_display_t *d, const bot_pose_t *p, double t)
{
    bot_view_rim_feed(p->mood, false, true);
    bot_view_update(p, t);
    bot_view_rim_pose(p, t);
    bot_view_tick(t, false);
    lv_refr_now(d);
}

static void page_shift(int dx, int dy)
{
    lv_obj_set_style_translate_x(s_page, dx, 0);
    lv_obj_set_style_translate_y(s_page, dy, 0);
}

static int redraw_diff(lv_display_t *d, int *x0, int *y0)
{
    memcpy(s_inc, s_fb, sizeof s_fb);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(d);
    int n = 0;
    *x0 = *y0 = -1;
    for (int i = 0; i < W * H; i++)
        if (s_inc[i] != s_fb[i]) {
            if (!n) *x0 = i % W, *y0 = i / W;
            n++;
        }
    return n;
}

static int moved_diff(int dx, int dy, int *edge)
{
    int n = 0;
    *edge = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int sx = x - dx, sy = y - dy;
            if (sx < 0 || sy < 0 || sx >= W || sy >= H) *edge += s_fb[y * W + x] != 0;
            else n += s_fb[y * W + x] != s_base[sy * W + sx];
        }
    return n;
}

static int glint_px(const uint16_t *fb)
{
    int n = 0;
    for (int i = 0; i < W * H; i++) {
        uint16_t v = fb[i];
        n += (v >> 11) >= 24 && ((v >> 5) & 63) >= 60 && (v & 31) >= 20 && s_want[i] <= 0;
    }
    return n;
}

static int icon_px(void)
{
    int n = 0;
    for (int i = 0; i < W * H; i++) n += s_fb[i] == 0xD3AA;
    return n;
}

static bool hit_screen(int x, int y)
{
    lv_point_t pt = {x, y};
    return lv_indev_search_obj(lv_screen_active(), &pt) == lv_screen_active();
}

static void test_orbit(lv_display_t *d)
{
    ember_host_info_t h = {.color = HOST_RGB, .tool = EMBER_TOOL_CLAUDE};
    ember_host_lead_info("", "m4", 1, "learn-language", 3, 0, &h);
    CHECK(shown_label(d, &h, BOT_WORKING, BOT_EYES_DASH), "orbit: the working outline with its label");
    bot_view_set_hint(true);
    bot_pose_t p = {.mood = BOT_WORKING, .face = BOT_WORKING, .eyes = BOT_EYES_DASH, .gaze_y = 0.6, .scale_x = 1, .scale_y = 1};
    const double t0 = 600.02;
    page_shift(0, 0);
    for (int i = 0; i < 400 && hint_px() < 800; i++) {
        orbit_frame(d, &p, t0);
        usleep(5000);
    }
    expect(&p);
    int x0, y0, n = redraw_diff(d, &x0, &y0);
    memcpy(s_base, s_fb, sizeof s_fb);
    int glint = glint_px(s_base), hint = hint_px(), icon = icon_px();
    printf("orbit base: glint %d px, hint %d px, Clawd %d px\n", glint, hint, icon);
    CHECK(!n && glint > 200 && hint > 800 && icon > 300, "orbit base: glint %d, hint %d, icon %d px, %d px differ from a full redraw at %d,%d",
          glint, hint, icon, n, x0, y0);

    static const int OFF[][2] = {{2, 0}, {4, 0}, {4, 4}, {-2, 4}, {-4, -2}, {-4, -4}, {0, -4}, {2, 2}};
    for (size_t k = 0; k < sizeof OFF / sizeof OFF[0]; k++) {
        int dx = OFF[k][0], dy = OFF[k][1], edge;
        page_shift(dx, dy);
        orbit_frame(d, &p, t0);
        n = redraw_diff(d, &x0, &y0);
        int moved = moved_diff(dx, dy, &edge);
        printf("orbit %+d,%+d: %d px off the moved base, %d lit in the uncovered edge, %d px differ from a full redraw\n", dx, dy, moved,
               edge, n);
        CHECK(!moved && !edge, "orbit %+d,%+d: ring, eyes, label, icon, glint and hint move together (%d px off, %d edge)", dx, dy, moved,
              edge);
        CHECK(!n, "orbit %+d,%+d: no ghost after the step (%d px, first at %d,%d)", dx, dy, n, x0, y0);
        CHECK(hit_screen(236, 233) && hit_screen(1, 1) && hit_screen(470, 464) && hit_screen(236 + dx, 233 + 140 + dy),
              "orbit %+d,%+d: taps still reach the screen (the bot page's tap)", dx, dy);
    }

    page_shift(4, -4);
    orbit_frame(d, &p, t0);
    int ghost = 0;
    for (int i = 1; i <= 12; i++) {
        orbit_frame(d, &p, t0 + i * (1.0 / 15));
        n = redraw_diff(d, &x0, &y0);
        ghost += n;
    }
    CHECK(!ghost, "orbit +4,-4: 12 glint steps leave no ghost (%d px)", ghost);
    bot_view_set_hint(false);
    orbit_frame(d, &p, t0 + 1);
    n = redraw_diff(d, &x0, &y0);
    CHECK(!n && hint_px() < 20, "orbit +4,-4: the hint leaves cleanly (%d px differ, %d red)", n, hint_px());
    bot_view_set_hint(true);
    orbit_frame(d, &p, t0 + 1.1);
    n = redraw_diff(d, &x0, &y0);
    CHECK(!n && hint_px() > 800, "orbit +4,-4: the hint returns cleanly (%d px differ, %d red)", n, hint_px());

    lv_area_t a;
    int before = icon_px();
    CHECK(bot_view_icon_off(true, &a), "orbit: the label has an icon to step aside");
    int outside = 0;
    for (int i = 0; i < W * H; i++)
        if (s_fb[i] == 0xD3AA) outside += i % W < a.x1 || i % W > a.x2 || i / W < a.y1 || i / W > a.y2;
    bot_view_icon_redraw(&a);
    lv_refr_now(d);
    int left = icon_px();
    n = redraw_diff(d, &x0, &y0);
    printf("orbit +4,-4: icon box %d,%d..%d,%d, %d icon px outside it, %d left after off\n", (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2,
           outside, left);
    CHECK(before > 300 && !outside && !left && !n, "orbit +4,-4: the icon's box is its shifted place (%d outside, %d left, %d differ)",
          outside, left, n);
    bot_view_icon_off(false, &a);
    bot_view_icon_redraw(&a);
    lv_refr_now(d);
    CHECK(icon_px() == before, "orbit +4,-4: the icon comes back (%d of %d px)", icon_px(), before);

    bot_pose_t low = p;
    low.gaze_y = -0.95;
    low.orbit = 0.9;
    bot_view_chase(true, true);
    int label0 = 0, label1 = 0;
    for (int i = 0; i < W * H; i++) label0 += s_fb[i] == 0x07E0;
    ghost = 0;
    for (int i = 0; i < 8; i++) {
        low.gaze_x = -0.6 + i * 0.15;
        orbit_frame(d, &low, t0 + 2 + i * 0.02);
        ghost += redraw_diff(d, &x0, &y0);
    }
    for (int i = 0; i < W * H; i++) label1 += s_fb[i] == 0x07E0;
    printf("orbit +4,-4: chase wipe took the label %d -> %d px, %d px differ from full redraws\n", label0, label1, ghost);
    CHECK(label1 < label0 && !ghost, "orbit +4,-4: the chase wipe redraws where the label is (%d -> %d px, %d ghost)", label0, label1,
          ghost);
    bot_view_chase(false, false);

    wxm_moment_t m = {.kind = WXM_GLANCE, .quiet = true, .sun = true, .text = "Sunny 21", .rgb = 0xFFFFFF};
    double tm = t0 + 4;
    CHECK(moment_view_start(&m, tm), "orbit: a quiet weather moment starts");
    double tau = -1;
    for (int i = 0; i < 2000 && tau < 0; i++) {
        tau = moment_view_poll(tm);
        if (tau < 0) usleep(2000);
    }
    CHECK(tau >= 0, "orbit: the moment baked and plays");
    int frames = 0, lit_max = 0, icon_min = before;
    ghost = 0;
    for (int k = 0; k <= WXM_FRAMES; k++) {
        double t = tm + (k + 0.5) / WXM_FPS;
        orbit_frame(d, &p, t);
        moment_view_frame(t);
        lv_refr_now(d);
        int lit = 0;
        for (int y = WXM_BOX_Y - 4; y < WXM_BOX_Y + WXM_BOX_H + 4; y++)
            for (int x = WXM_BOX_X; x < WXM_BOX_X + WXM_BOX_W + 8 && x < W; x++) lit += s_fb[y * W + x] != 0;
        lit_max = lit > lit_max ? lit : lit_max;
        icon_min = icon_px() < icon_min ? icon_px() : icon_min;
        int g = redraw_diff(d, &x0, &y0);
        if (g && !ghost) printf("orbit +4,-4: moment frame %d: %d px differ, first at %d,%d\n", k, g, x0, y0);
        ghost += g;
        frames++;
    }
    printf("orbit +4,-4: quiet moment %d frames, up to %d px in the sprite box, %d px differ from full redraws\n", frames, lit_max, ghost);
    CHECK(lit_max > 500 && !ghost && !moment_view_busy() && icon_px() == before,
          "orbit +4,-4: the moment sprite, text and icon join redraw where they are (%d frames, %d px, %d ghost, icon %d of %d)", frames,
          lit_max, ghost, icon_px(), before);
    CHECK(!icon_min, "orbit +4,-4: the icon steps aside during the moment (at least %d of %d px left)", icon_min, before);

    bot_view_set_hint(false);
    page_shift(0, 0);
    orbit_frame(d, &p, t0 + 3);
}

int main(int argc, char **argv)
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
    s_page = lv_obj_create(scr);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_page, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    bot_view_create(s_page);
    moment_view_create(s_page);

    struct {
        const char *name;
        bot_eyes_t eyes;
        double gx, gy, lid, blink;
        bool hide;
    } f[] = {
        {"open", BOT_EYES_DASH, 0.3, 0.4, 0, 0, false},
        {"heavy lid (trimmed)", BOT_EYES_DASH, 0.3, 0.4, 0.8, 0, false},
        {"blink shut, the closed-eye line", BOT_EYES_DASH, 0.3, 0.4, 1, 1, false},
        {"swap blink shut (hidden)", BOT_EYES_DASH, 0.3, 0.4, 1, 1, true},
        {"reopened, looking down-left", BOT_EYES_DASH, -0.4, -0.3, 0, 0, false},
        {"half blink", BOT_EYES_DASH, -0.4, -0.3, 0.5, 0.5, false},
        {"round open", BOT_EYES_ROUND, 0.1, 0.1, 0, 0, false},
        {"round heavy lid", BOT_EYES_ROUND, 0.1, 0.1, 0.7, 0, false},
        {"angry half lid", BOT_EYES_ANGRY, -0.2, -0.4, 0.5, 0.5, false},
        {"angry swap blink shut (hidden)", BOT_EYES_ANGRY, -0.2, -0.4, 1, 1, true},
        {"happy open", BOT_EYES_HAPPY, 0.4, 0.5, 0, 0, false},
        {"happy blink (flattened, not clipped)", BOT_EYES_HAPPY, 0.4, 0.5, 0.7, 0.7, true},
        {"happy swap blink shut (hidden)", BOT_EYES_HAPPY, 0.4, 0.5, 0.95, 0.95, true},
        {"reopened", BOT_EYES_DASH, 0.3, 0.4, 0, 0, false},
    };
    int eye_bottom = -1;
    for (size_t k = 0; k < sizeof f / sizeof f[0]; k++) {
        bot_pose_t p = {.mood = BOT_IDLE, .face = BOT_IDLE, .eyes = f[k].eyes, .gaze_x = f[k].gx, .gaze_y = f[k].gy,
                        .lid_l = f[k].lid, .lid_r = f[k].lid, .blink_l = f[k].blink, .blink_r = f[k].blink, .hide = f[k].hide,
                        .scale_x = 1, .scale_y = 1};
        bot_view_update(&p, (double)k);
        lv_refr_now(d);
        expect(&p);
        int stale, off, lit;
        compare(f[k].name, &stale, &off, &lit);
        CHECK(!stale, "%s: %d px still lit where no eye is (the vacated area must clear)", f[k].name, stale);
        bool gone = f[k].hide && f[k].blink >= BOT_BLINK_HIDE;
        CHECK(!off && (gone ? lit == 0 : lit > 100), "%s: %d px off the expected eye, %d lit", f[k].name, off, lit);
        for (int i = W * H - 1; i >= 0; i--)
            if (s_want[i] > 0) {
                if (i / W > eye_bottom) eye_bottom = i / W;
                break;
            }
    }
    test_offline(d, eye_bottom);
    test_rim_hue(d);
    test_label(d);
    test_hint(d, argc > 1 ? argv[1] : NULL);
    test_orbit(d);
    printf(failures ? "%d FAILED\n" : "eye view: all tests passed\n", failures);
    return failures ? 1 : 0;
}
