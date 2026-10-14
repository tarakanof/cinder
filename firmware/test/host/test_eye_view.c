#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "bot_raster.h"
#include "bot_shape.h"
#include "bot_view.h"
#include "lvgl.h"

#define W 472
#define H 466
#define EYE_RGB 0xF4F4F2

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
    }
    printf(failures ? "%d FAILED\n" : "eye view: all tests passed\n", failures);
    return failures ? 1 : 0;
}
