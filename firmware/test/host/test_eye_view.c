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
    bot_view_create(scr);

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
    printf(failures ? "%d FAILED\n" : "eye view: all tests passed\n", failures);
    return failures ? 1 : 0;
}
