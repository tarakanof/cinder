#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bot_behavior.h"
#include "bot_raster.h"
#include "bot_shape.h"
#include "arc_text.h"
#include "tool_marks.h"
#include "ring_glint.h"
#include "glint_chase.h"
#include "label_wipe.h"
#include "orbit_table.h"
#include "rim_job.h"
#include <stdlib.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int count_blinks(bot_t *b, double t0, double seconds)
{
    int n = 0;
    double prev = 0;
    for (double t = t0; t < t0 + seconds; t += 1.0 / 60) {
        bot_pose_t p = bot_pose(b, t);
        if (p.lid_l >= 0.9 && prev < 0.9) n++;
        prev = p.lid_l;
    }
    return n;
}

static void test_blink_rate(void)
{
    bot_t b;
    bot_init(&b, 42, 0);
    int idle = count_blinks(&b, 0, 240);
    CHECK(idle >= 40 && idle <= 100, "idle blinks over 4 min = %d, want 40..100 (Swift test range)", idle);

    bot_t w;
    bot_init(&w, 42, 0);
    bot_set_mood(&w, BOT_WORKING, 0);
    int working = count_blinks(&w, 0, 240);
    CHECK(working < idle, "working blinks %d should be fewer than idle %d", working, idle);
}

static void test_mood_swaps_eyes_behind_blink(void)
{
    bot_t b;
    bot_init(&b, 7, 0);
    bot_pose(&b, 0.5);
    bot_set_mood(&b, BOT_DONE, 1.0);
    bot_pose_t p = bot_pose(&b, 1.0);
    CHECK(p.eyes == BOT_EYES_DASH, "eyes must not change before the lids close");
    bool swapped_while_shut = false;
    for (double t = 1.0; t < 2.0; t += 1.0 / 120) {
        p = bot_pose(&b, t);
        if (p.eyes == BOT_EYES_HAPPY) {
            swapped_while_shut = fmax(p.lid_l, p.lid_r) >= 0.85;
            break;
        }
    }
    CHECK(swapped_while_shut, "eyes should swap to happy while the lids are (nearly) shut");
    p = bot_pose(&b, 3.0);
    CHECK(p.eyes == BOT_EYES_HAPPY, "done mood shows happy eyes after the morph");
    CHECK(p.badge > 0.9, "done shows the badge, got %.2f", p.badge);
}

static void test_happy_eyes_smooth(void)
{
    const double R = 196;
    for (double lid = 0; lid <= 1.0001; lid += 0.25) {
        bot_pose_t p = {0};
        p.eyes = BOT_EYES_HAPPY;
        p.lid_l = p.lid_r = lid;
        bot_stroke_t s[2];
        bot_eye_strokes(&p, 1.15, s);
        for (int e = 0; e < 2; e++) {
            double sag = 0;
            for (int i = 1; i + 1 < s[e].n; i++) {
                double ax = s[e].x[i] - s[e].x[i - 1], ay = s[e].y[i] - s[e].y[i - 1];
                double bx = s[e].x[i + 1] - s[e].x[i], by = s[e].y[i + 1] - s[e].y[i];
                double turn = fabs(atan2(ax * by - ay * bx, ax * bx + ay * by));
                sag = fmax(sag, turn * fmax(hypot(ax, ay), hypot(bx, by)) * R / 8);
            }
            CHECK(sag < 0.25, "lid %.2f eye %d: happy arc strays %.2f px from a smooth curve", lid, e, sag);
        }
    }
}

static void test_sleepy_after_idle(void)
{
    bot_t b;
    bot_init(&b, 1, 0);
    bot_pose_t p = bot_pose(&b, BOT_SLEEP_AFTER_S - 1);
    CHECK(p.mood == BOT_IDLE, "still idle before the timeout");
    p = bot_pose(&b, BOT_SLEEP_AFTER_S + 0.1);
    CHECK(p.mood == BOT_SLEEPY, "sleepy after %.0f s idle", BOT_SLEEP_AFTER_S);
    CHECK(!bot_set_mood(&b, BOT_IDLE, BOT_SLEEP_AFTER_S + 1), "idle request keeps a sleepy bot asleep");
    CHECK(bot_set_mood(&b, BOT_WAITING, BOT_SLEEP_AFTER_S + 2), "a real state wakes it");

    bot_init(&b, 1, 0);
    bot_set_sleep_after(&b, 30);
    CHECK(bot_pose(&b, 29).mood == BOT_IDLE && bot_pose(&b, 30.1).mood == BOT_SLEEPY, "sleepy after 30 s");
    bot_init(&b, 1, 0);
    bot_set_sleep_after(&b, 0);
    CHECK(bot_pose(&b, 86400).mood == BOT_IDLE, "never sleepy");
}

static void test_error_triangle_geometry(void)
{
    double xs[BOT_RING_POINTS], ys[BOT_RING_POINTS];
    bot_body_ring(0, xs, ys);
    for (int i = 0; i < BOT_RING_POINTS; i++) {
        CHECK(fabs(hypot(xs[i], ys[i]) - 1) < 1e-9, "k=0 ring must be the unit circle (i=%d)", i);
    }
    bot_body_ring(1, xs, ys);
    double apex = hypot(xs[BOT_RING_POINTS / 4], ys[BOT_RING_POINTS / 4]);
    double side = hypot(xs[3 * BOT_RING_POINTS / 4], ys[3 * BOT_RING_POINTS / 4]);
    CHECK(apex > 1.05 && apex < 1.11, "apex radius %.3f, want about 1.1 (sharp vertex)", apex);
    CHECK(side > 0.62 && side < 0.70, "mid-side radius %.3f, want about 0.66 (side bows out from 0.55)", side);
}

static void test_eye_strokes_finite(void)
{
    bot_t b;
    bot_init(&b, 5, 0);
    bot_mood_t moods[] = {BOT_WORKING, BOT_WAITING, BOT_DONE, BOT_ERROR, BOT_IDLE};
    double t = 0;
    for (int m = 0; m < 5; m++) {
        bot_set_mood(&b, moods[m], t);
        for (int i = 0; i < 240; i++, t += 1.0 / 60) {
            bot_pose_t p = bot_pose(&b, t);
            bot_stroke_t s[2];
            bot_eye_strokes(&p, 1.15, s);
            for (int e = 0; e < 2; e++) {
                CHECK(s[e].n >= 2 && s[e].n <= BOT_STROKE_MAX, "stroke point count %d", s[e].n);
                CHECK(s[e].width > 0 && s[e].width < 0.6, "stroke width %.3f", s[e].width);
                for (int k = 0; k < s[e].n; k++) {
                    CHECK(isfinite(s[e].x[k]) && fabs(s[e].x[k]) < 1.2 && fabs(s[e].y[k]) < 1.2,
                          "eye point out of the body at mood %d", moods[m]);
                }
            }
        }
    }
}

static int compare_raster(const float *xs, const float *ys, int npts, float hw, int alpha_mode)
{
    static uint16_t a[BOT_RASTER_MAX_W * 200], b[BOT_RASTER_MAX_W * 200];
    static uint8_t aa[BOT_RASTER_MAX_W * 200], ab[BOT_RASTER_MAX_W * 200];
    int segs[BOT_RASTER_MAX_SEGS];
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < npts; i++) {
        minx = fminf(minx, xs[i]); maxx = fmaxf(maxx, xs[i]); miny = fminf(miny, ys[i]); maxy = fmaxf(maxy, ys[i]);
    }
    int ox = (int)floorf(minx - hw - 2), oy = (int)floorf(miny - hw - 2);
    int w = (int)ceilf(maxx + hw + 2) - ox, h = (int)ceilf(maxy + hw + 2) - oy;
    if (w > BOT_RASTER_MAX_W) w = BOT_RASTER_MAX_W;
    if (h > 200) h = 200;
    for (int i = 0; i + 1 < npts; i++) segs[i] = i;
    static bot_raster_scratch_t scratch;
    bot_raster_stroke(&scratch, a, alpha_mode ? aa : NULL, w, h, ox, oy, xs, ys, npts - 1, segs, hw, 0xF4F4F2, 1.0f);
    bot_raster_stroke_ref(b, alpha_mode ? ab : NULL, w, h, ox, oy, xs, ys, npts - 1, segs, hw, 0xF4F4F2, 1.0f);
    int bad = 0;
    for (int i = 0; i < w * h; i++) {
        int d = alpha_mode ? abs((int)aa[i] - (int)ab[i]) : (a[i] != b[i]);
        if (alpha_mode ? d > 2 : d) bad++;
    }
    return bad;
}

static void test_raster_matches_reference(void)
{
    int bad = 0, cases = 0;
    bot_t b;
    bot_init(&b, 11, 0);
    bot_mood_t moods[] = {BOT_IDLE, BOT_WORKING, BOT_WAITING, BOT_DONE, BOT_ERROR};
    double t = 0;
    for (int m = 0; m < 5; m++) {
        bot_set_mood(&b, moods[m], t);
        for (int f = 0; f < 120; f++, t += 1.0 / 30) {
            bot_pose_t p = bot_pose(&b, t);
            bot_stroke_t st[2];
            bot_eye_strokes(&p, 1.15, st);
            for (int e = 0; e < 2; e++) {
                float xs[BOT_STROKE_MAX], ys[BOT_STROKE_MAX];
                for (int i = 0; i < st[e].n; i++) { xs[i] = (float)(236 + 196 * st[e].x[i]); ys[i] = (float)(233 - 196 * st[e].y[i]); }
                bad += compare_raster(xs, ys, st[e].n, (float)(st[e].width * 196 / 2), 1);
                cases++;
            }
        }
    }
    srand(3);
    for (int k = 0; k < 300; k++) {
        float xs[2] = {50 + rand() % 100, 50 + rand() % 100}, ys[2] = {50 + rand() % 80, 50 + rand() % 80};
        if (k % 10 == 0) xs[1] = xs[0];
        if (k % 10 == 1) ys[1] = ys[0];
        bad += compare_raster(xs, ys, 2, 1.0f + (float)(rand() % 200) / 10.0f, k & 1);
        cases++;
    }
    CHECK(bad == 0, "span rasteriser differs from the reference on %d pixels (%d cases)", bad, cases);
}

static void test_ring_glint(void)
{
    const ring_glint_t g = {.cx = 236, .cy = 233, .r = 195.72f, .hw = 6, .tail_deg = 40};
    static uint8_t a[240 * 240];
    float heads[] = {-90, -40, 0, 45, 90, 135, 180, 225, 269.9f};
    int max_w = 0, max_h = 0;
    for (unsigned k = 0; k < sizeof heads / sizeof heads[0]; k++) {
        float h = heads[k];
        ring_glint_box_t b;
        ring_glint_box(&g, h, &b);
        CHECK(b.x % 2 == 0 && b.y % 2 == 0 && b.w % 2 == 0 && b.h % 2 == 0, "even box at %g", h);
        CHECK(b.w <= 240 && b.h <= 240, "box %dx%d fits the test buffer", b.w, b.h);
        if (b.w > max_w) max_w = b.w;
        if (b.h > max_h) max_h = b.h;
        ring_glint_raster(&g, h, &b, a);
        int edge = 0;
        for (int x = 0; x < b.w; x++) edge += a[x] + a[(b.h - 1) * b.w + x];
        for (int y = 0; y < b.h; y++) edge += a[y * b.w] + a[y * b.w + b.w - 1];
        CHECK(edge == 0, "box edge empty at %g", h);
#define AT(deg, rr) ({ float _t = (deg) * (float)M_PI / 180; \
        int _x = (int)floorf(g.cx + (rr) * cosf(_t)) - b.x, _y = (int)floorf(g.cy + (rr) * sinf(_t)) - b.y; \
        (_x >= 0 && _y >= 0 && _x < b.w && _y < b.h) ? a[_y * b.w + _x] : 0; })
        int head = AT(h - 1, g.r), mid = AT(h - 20, g.r), end = AT(h - 39.5f, g.r);
        CHECK(head > 200, "head bright at %g: %d", h, head);
        CHECK(mid > 30 && mid < head, "tail fades at %g: mid %d head %d", h, mid, head);
        CHECK(end < 10, "tail ends at %g: %d", h, end);
        CHECK(AT(h + 8, g.r) == 0, "nothing well ahead of the head at %g", h);
        CHECK(AT(h - 20, g.r + g.hw + 2) == 0 && AT(h - 20, g.r - g.hw - 2) == 0, "off the band at %g", h);
#undef AT
        int worst = 0;
        for (int y = 0; y < b.h; y++) {
            for (int x = 0; x < b.w; x++) {
                float dx = (float)(b.x + x) + 0.5f - g.cx, dy = (float)(b.y + y) + 0.5f - g.cy;
                float d = sqrtf(dx * dx + dy * dy), behind = h - atan2f(dy, dx) * 180 / (float)M_PI;
                behind -= 360.0f * floorf((behind + 180.0f) / 360.0f);
                float cov = 0;
                if (behind >= 0 && behind <= g.tail_deg) {
                    float rad = g.hw + 0.5f - fabsf(d - g.r), u = 1 - behind / g.tail_deg;
                    cov = (rad < 0 ? 0 : rad > 1 ? 1 : rad) * u;
                }
                float hx = g.cx + g.r * cosf(h * (float)M_PI / 180), hy = g.cy + g.r * sinf(h * (float)M_PI / 180);
                float ex = dx + g.cx - hx, ey = dy + g.cy - hy, c2 = g.hw + 0.5f - sqrtf(ex * ex + ey * ey);
                c2 = c2 < 0 ? 0 : c2 > 1 ? 1 : c2;
                if (c2 > cov) cov = c2;
                int diff = abs((int)a[y * b.w + x] - (int)lroundf(cov * 255));
                if (diff > worst) worst = diff;
            }
        }
        CHECK(worst <= 2, "raster vs reference at %g: worst %d", h, worst);
    }
    printf("glint: largest box %d x %d px\n", max_w, max_h);

    long merged = 0, split = 0;
    for (int k = 0; k < 45; k++) {
        float h0 = (float)(k * 8 - 90), h1 = h0 + 8;
        ring_glint_box_t b0, b1, s3[3];
        ring_glint_box(&g, h0, &b0);
        ring_glint_box(&g, h1, &b1);
        int x0 = b0.x < b1.x ? b0.x : b1.x, y0 = b0.y < b1.y ? b0.y : b1.y;
        int x1 = b0.x + b0.w > b1.x + b1.w ? b0.x + b0.w : b1.x + b1.w;
        int y1 = b0.y + b0.h > b1.y + b1.h ? b0.y + b0.h : b1.y + b1.h;
        merged += (long)(x1 - x0) * (y1 - y0);
        for (int i = 0; i < 3; i++) {
            ring_glint_span_box(&g, h1 - 16.0f * i, 16.0f, &s3[i]);
            split += (long)s3[i].w * s3[i].h;
        }
        float heads[2] = {h0, h1};
        ring_glint_box_t bb[2] = {b0, b1};
        int missed = 0;
        for (int j = 0; j < 2; j++) {
            ring_glint_raster(&g, heads[j], &bb[j], a);
            for (int y = 0; y < bb[j].h; y++)
                for (int x = 0; x < bb[j].w; x++) {
                    if (!a[y * bb[j].w + x]) continue;
                    int px = bb[j].x + x, py = bb[j].y + y, in = 0;
                    for (int i = 0; i < 3; i++)
                        in |= px >= s3[i].x && px < s3[i].x + s3[i].w && py >= s3[i].y && py < s3[i].y + s3[i].h;
                    missed += !in;
                }
        }
        CHECK(missed == 0, "step %d: %d covered px outside the pieces", k, missed);
    }
    printf("glint step: merged box avg %ld px, 3 pieces avg %ld px\n", merged / 45, split / 45);
    CHECK(split * 10 < merged * 7, "pieces at least 30%% smaller than the merged box");
    CHECK(max_w <= 160 && max_h <= 160, "glint buffer 160x160 holds every box");

    CHECK(ring_glint_color(0x2EE85E, 0.7f) == 0xC0F8CF, "mix: %06X", (unsigned)ring_glint_color(0x2EE85E, 0.7f));
    CHECK(ring_glint_color(0x000000, 1.0f) == 0xFFFFFF && ring_glint_color(0x123456, 0) == 0x123456, "mix ends");
}

static void test_arc_text(void)
{
    float w2[] = {20, 20};
    arc_item_t it[16];
    arc_text_layout(236, 233, 172, w2, 2, it);
    CHECK(it[0].x < 236 && it[1].x > 236, "left to right: %g %g", it[0].x, it[1].x);
    CHECK(fabsf((236 - it[0].x) - (it[1].x - 236)) < 1e-3f && fabsf(it[0].y - it[1].y) < 1e-3f, "symmetric");
    CHECK(it[0].rot_deg > 0 && it[1].rot_deg < 0 && fabsf(it[0].rot_deg + it[1].rot_deg) < 1e-3f,
          "left item turned clockwise, right counter-clockwise: %g %g", it[0].rot_deg, it[1].rot_deg);
    float w1[] = {30};
    arc_text_layout(236, 233, 172, w1, 1, it);
    CHECK(fabsf(it[0].x - 236) < 1e-3f && fabsf(it[0].y - 405) < 1e-3f && fabsf(it[0].rot_deg) < 1e-3f, "centre");
    float w3[] = {10, 30, 20};
    arc_text_layout(236, 233, 172, w3, 3, it);
    float d01 = hypotf(it[1].x - it[0].x, it[1].y - it[0].y), d12 = hypotf(it[2].x - it[1].x, it[2].y - it[1].y);
    CHECK(fabsf(d01 - 20) < 0.1f && fabsf(d12 - 25) < 0.1f, "spacing %g %g", d01, d12);
    for (int i = 0; i < 3; i++) {
        float rr = hypotf(it[i].x - 236, it[i].y - 233);
        CHECK(fabsf(rr - 172) < 1e-2f, "on the circle: %g", rr);
    }

    uint8_t body[8], feat[8];
    int bink = 0, fink = 0;
    CHECK(arc_tool_icon(3, body, feat), "t3 has a pixel icon");
    for (int r = 0; r < 8; r++) {
        bink += body[r] != 0;
        fink += feat[r] != 0;
    }
    CHECK(bink && fink, "t3: body and feature drawn");
    CHECK(!arc_tool_icon(0, body, feat), "no tool, no icon");

    const uint8_t *marks[2];
    uint32_t colors[2];
    for (int tool = 1; tool <= 2; tool++) {
        uint32_t rgb = 0;
        int w = 0, h = 0, ink = 0;
        const uint8_t *m = tool_mark(tool, &rgb, &w, &h);
        marks[tool - 1] = m;
        colors[tool - 1] = rgb;
        CHECK(rgb != 0, "mark %d is not black", tool);
        CHECK(m && w > 0 && h > 0 && w <= TOOL_MARK_MAX_W && h <= TOOL_MARK_H, "mark %d fits the icon box: %dx%d", tool, w, h);
        for (int i = 0; m && i < w * h; i++) ink += m[i] != 0;
        CHECK(ink > 0 && ink < w * h, "mark %d has ink and holes: %d", tool, ink);
    }
    CHECK(marks[0] != marks[1] && colors[0] != colors[1], "claude and codex marks differ");
    uint32_t rgb;
    int w, h;
    CHECK(!tool_mark(3, &rgb, &w, &h) && !tool_mark(0, &rgb, &w, &h), "t3 and unknown have no mark");
}

static void test_glint_chase(void)
{
    int chases = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        glint_chase_t c;
        gc_init(&c, seed * 2654435761u);
        gc_out_t o;
        double t = 0, first = -1, start = -1, end = -1, back = -1;
        int sx = 0, sy = 0;
        for (; t < 1000; t += 0.05) {
            gc_tick(&c, t, true, 3.0, &o);
            if (o.chasing && start < 0) start = t;
            if (start >= 0 && !o.chasing && end < 0) end = t;
            if (end >= 0 && o.label_opa >= 1 && back < 0) { back = t; sx = o.shift_x; sy = o.shift_y; }
            if (first < 0 && o.chasing) first = t;
            if (o.chasing) CHECK(o.label_opa == 1, "label stays for the eyes to wipe");
            if (o.chasing && t - c.start_at > GC_ORBIT_IN_S + 0.06 && c.end_at - t > GC_ORBIT_OUT_S + 0.06)
                CHECK(o.orbit == 1, "eyes at the ring mid-chase: %g", o.orbit);
            if (!o.chasing) CHECK(o.orbit == 0, "no orbit outside a chase");
            if (end >= 0 && back < 0 && o.label_opa > 0 && t - end < GC_BACK_AFTER_S - 0.06) CHECK(0, "label away after the chase");
        }
        CHECK(first >= GC_WAIT_MIN_S - 0.1 && first <= GC_WAIT_MAX_S + 0.1, "first chase after 5-15 min: %g", first);
        double len = end - start;
        CHECK(len >= GC_LAPS_MIN * 3.0 - 0.1 && len <= GC_LAPS_MAX * 3.0 + 0.1, "laps: %g s", len);
        CHECK(back - end >= GC_BACK_AFTER_S - 0.1 && back - end <= GC_BACK_AFTER_S + GC_FADE_IN_S + 0.1,
              "label back a few s after: %g", back - end);
        CHECK((sx || sy) && abs(sx) <= GC_SHIFT_PX && abs(sy) <= GC_SHIFT_PX, "shifted %d,%d", sx, sy);
        chases++;
    }
    glint_chase_t c;
    gc_init(&c, 7);
    gc_out_t o;
    double t = 0;
    for (; t < GC_WAIT_MAX_S + 1; t += 0.1) {
        gc_tick(&c, t, true, 3.0, &o);
        if (o.chasing) break;
    }
    CHECK(o.chasing, "chasing");
    gc_tick(&c, t + 0.1, false, 3.0, &o);
    CHECK(!o.chasing && o.label_opa == 1, "inactive: no chase, label shown");
    gc_tick(&c, t + 0.2, true, 3.0, &o);
    CHECK(!o.chasing && c.work_s < 1, "wait restarts");
    gc_init(&c, 9);
    for (t = 0; t < 2000; t += 0.5) gc_tick(&c, t, false, 3.0, &o);
    gc_tick(&c, 2000.5, true, 3.0, &o);
    CHECK(!o.chasing, "idle time does not count toward a chase");
    gc_init(&c, 11);
    gc_tick(&c, 0, true, 3.0, &o);
    CHECK(gc_start_now(&c, GC_STYLE_HALF, 0), "start now");
    gc_tick(&c, 0.1, true, 3.0, &o);
    CHECK(o.chasing && o.style == GC_STYLE_HALF && !gc_start_now(&c, GC_STYLE_FULL, 0), "chasing at once; busy after");
    gc_init(&c, 12);
    gc_tick(&c, 0, true, 3.0, &o);
    CHECK(gc_start_now(&c, GC_STYLE_FULL, 10), "start now, 10 laps");
    gc_tick(&c, 0.1, true, 3.0, &o);
    CHECK(o.chasing && c.laps == 10 && fabs(c.end_at - c.start_at - 30) < 1e-9, "forced laps: %d", c.laps);
    CHECK(o.orbit == 0 || o.orbit < 0.05, "orbit starts at the usual radius: %g", o.orbit);
    gc_tick(&c, 31, true, 3.0, &o);
    gc_tick(&c, 40, true, 3.0, &o);
    CHECK(gc_start_now(&c, GC_STYLE_FULL, 0), "idle again");
    gc_tick(&c, 40.1, true, 3.0, &o);
    CHECK(c.laps >= GC_LAPS_MIN && c.laps <= GC_LAPS_MAX, "forced laps used once: %d", c.laps);
    CHECK(gc_orbit(0, 10) == 0 && gc_orbit(GC_ORBIT_IN_S, 10) == 1 && gc_orbit(5, 0) == 0 &&
              fabs(gc_orbit(GC_ORBIT_IN_S / 2, 10) - 0.5) < 1e-9 && gc_orbit(5, GC_ORBIT_OUT_S / 4) < 0.25,
          "orbit ramp: smoothstep in and out");

    gc_half_t h;
    gc_half_reset(&h);
    int follow = 0, blinks = 0, snaps = 0, passes_blinks[4] = {0};
    double maxtarget = 0;
    for (double psi = -90; psi < 4 * 360 - 90; psi += 2) {
        gc_half_out_t ho;
        gc_half_step(&h, psi, 15, &ho);
        double q = fmod(psi + 720, 360);
        if (ho.follow) {
            follow++;
            CHECK(q < 180, "follows only the lower half: %g", q);
            if (ho.target_deg > maxtarget) maxtarget = ho.target_deg;
        }
        if (ho.blink) { blinks++; CHECK(q >= 180, "blinks up top"); if (h.pass < 4) passes_blinks[h.pass]++; }
        if (ho.snap) { snaps++; CHECK(q >= 345, "snap just before 3 o'clock: %g", q); }
    }
    CHECK(follow > 0 && maxtarget <= 180, "target capped at 9 o'clock: %g", maxtarget);
    CHECK(snaps == 4 && blinks >= 8, "one snap per lap: %d, blinks %d", snaps, blinks);
    CHECK(passes_blinks[1] == 3 && passes_blinks[2] == 2, "3 then 2 blinks: %d %d", passes_blinks[1], passes_blinks[2]);
    int halves = 0, autos = 0;
    for (uint32_t seed = 1; seed <= 400; seed++) {
        glint_chase_t ca;
        gc_init(&ca, seed * 2246822519u);
        gc_out_t oa;
        for (double ta = 0; ta < GC_WAIT_MAX_S + 1; ta += 0.5) {
            gc_tick(&ca, ta, true, 3.0, &oa);
            if (oa.chasing) { autos++; halves += oa.style == GC_STYLE_HALF; break; }
        }
    }
    CHECK(autos == 400 && halves > 140 && halves < 260, "automatic styles 50/50: %d half of %d", halves, autos);
    printf("glint chase: %d seeds ok\n", chases);
}

static void test_track_smooth(void)
{
    bot_t b;
    bot_init(&b, 42, 0);
    bot_set_mood(&b, BOT_WORKING, 0);
    double t = 10, prevx = 0, prevy = 0, maxstep = 0, maxlag = 0;
    bool first = true;
    for (int i = 0; i < 6 * 60; i++, t += 1.0 / 60) {
        double a = 2 * M_PI * (t - 10) / 3;
        bot_track(&b, cos(a) * 0.85, -sin(a) * 0.85, t);
        bot_pose_t p = bot_pose(&b, t);
        if (t > 10.5) {
            CHECK(fabs(p.offset_x) < 1e-3 && fabs(p.offset_y) < 1e-3, "no lean");
            double step = hypot(p.gaze_x - prevx, p.gaze_y - prevy);
            if (!first && step > maxstep) maxstep = step;
            double lag = hypot(p.gaze_x - cos(a) * 0.85, p.gaze_y + sin(a) * 0.85);
            if (lag > maxlag) maxlag = lag;
            first = false;
        }
        prevx = p.gaze_x;
        prevy = p.gaze_y;
    }
    printf("track: max gaze step per 60 fps frame %.3f, max lag %.2f\n", maxstep, maxlag);
    CHECK(maxstep < 0.05, "smooth: %g", maxstep);
    CHECK(maxlag < 0.5, "keeps up: %g", maxlag);
    bot_track_end(&b, t);
    bot_pose_t p = bot_pose(&b, t + 0.01);
    CHECK(fabs(p.gaze_x - prevx) < 0.02 && fabs(p.gaze_y - prevy) < 0.02, "end keeps the gaze");
}

static void eye_centres(const bot_pose_t *p, double r_px, double pts[4][2])
{
    bot_stroke_t s[2];
    bot_eye_strokes(p, 1.15, s);
    for (int e = 0; e < 2; e++) {
        pts[2 * e][0] = s[e].x[0] * r_px;
        pts[2 * e][1] = s[e].y[0] * r_px;
        pts[2 * e + 1][0] = s[e].x[s[e].n - 1] * r_px;
        pts[2 * e + 1][1] = s[e].y[s[e].n - 1] * r_px;
    }
}

static double chase_step_px(double fps, double orbit_on)
{
    const double R = 195.72, outer = 0.92, gaze0 = 0.6;
    bot_t b;
    bot_init(&b, 5, 0);
    bot_set_mood(&b, BOT_WORKING, 0);
    bot_pose_t ref = {.mood = BOT_WORKING, .eyes = BOT_EYES_DASH, .scale_x = 1, .scale_y = 1};
    double t = 10, last = -1, prev[4][2], maxstep = 0;
    for (int i = 0; i < 9 * 60; i++, t += 0.016) {
        double deg = fmod((t - 10) / 3.0, 1.0) * 360 - 90 + 15;
        double m = gaze0 + (bot_orbit_gaze(&ref, 1.15, deg, outer) - gaze0) * orbit_on, a = deg * M_PI / 180;
        bot_track(&b, cos(a) * m, -sin(a) * m, t);
        bot_pose_t p = bot_pose(&b, t);
        p.orbit = orbit_on;
        p.lid_l = p.lid_r = 0;
        if (last >= 0 && t - last < 0.75 / fps) continue;
        double pts[4][2];
        eye_centres(&p, R, pts);
        if (last >= 0 && t > 13)
            for (int k = 0; k < 4; k++) maxstep = fmax(maxstep, hypot(pts[k][0] - prev[k][0], pts[k][1] - prev[k][1]));
        memcpy(prev, pts, sizeof prev);
        last = t;
    }
    return maxstep;
}

static void test_chase_orbit(void)
{
    const double R = 195.72;
    bot_pose_t ref = {.mood = BOT_WORKING, .eyes = BOT_EYES_DASH, .scale_x = 1, .scale_y = 1};
    double rmin = 1e9, rmax = 0;
    for (int deg = 0; deg < 360; deg += 5) {
        double m = bot_orbit_gaze(&ref, 1.15, deg, 0.92), a = deg * M_PI / 180;
        bot_pose_t p = ref;
        p.orbit = 1;
        p.gaze_x = cos(a) * m;
        p.gaze_y = -sin(a) * m;
        bot_stroke_t s[2];
        bot_eye_strokes(&p, 1.15, s);
        CHECK(fabs(bot_eyes_extent(s) - 0.92) < 1e-4, "outer edge at 0.92 r (deg %d): %g", deg, bot_eyes_extent(s));
        double c = hypot((s[0].x[0] + s[0].x[1] + s[1].x[0] + s[1].x[1]) / 4, (s[0].y[0] + s[0].y[1] + s[1].y[0] + s[1].y[1]) / 4) * R;
        rmin = fmin(rmin, c);
        rmax = fmax(rmax, c);
        p.orbit = 0;
        bot_eye_strokes(&p, 1.15, s);
        CHECK(bot_eyes_extent(s) < 0.9, "orbit 0 keeps the usual reach limit (deg %d): %g", deg, bot_eyes_extent(s));
    }
    CHECK(rmin > 95 && rmax < 145 && rmax - rmin > 15, "eye pair centre %.0f-%.0f px", rmin, rmax);
    double s60 = chase_step_px(60, 1), s32 = chase_step_px(32, 1), today = chase_step_px(32, 0);
    printf("chase: eye pair centre %.0f-%.0f px from the body centre; max eye step today %.1f px, ring at 32 fps %.1f px, "
           "ring at 60 fps %.1f px\n", rmin, rmax, today, s32, s60);
    CHECK(s60 < 6 && s32 > s60 * 1.6 && today < s32, "steps: %g %g %g", s60, s32, today);
}

static void test_label_wipe(void)
{
    CHECK(lw_bin(10, 0) == 0 && lw_bin(0, 10) == 90 && lw_bin(-10, 0) == 180 && lw_bin(0, -10) == 270 &&
              lw_bin(10, -0.01) == 359,
          "bins: 0 = 3 o'clock, clockwise (y down)");
    label_wipe_t w;
    lw_clear(&w);
    uint16_t added[LW_BINS];
    CHECK(lw_add(&w, 80.5, 99.5, added, LW_BINS) == 20 && added[0] == 80 && added[19] == 99 && w.count == 20, "add 80..99");
    CHECK(lw_add(&w, 90, 105, added, LW_BINS) == 6 && added[0] == 100, "only new bins: %d", added[0]);
    CHECK(lw_add(&w, 350, 365, added, LW_BINS) == 16 && lw_has(&w, 359) && lw_has(&w, 5) && !lw_has(&w, 6), "wraps at 0");
    CHECK(lw_add(&w, 0, 720, added, 4) == LW_BINS - 42 && w.count == LW_BINS, "all, cap respected");

    double xa[2] = {236 + 100, 236 + 100}, ya[2] = {233 - 20, 233 + 20};
    lw_stroke_t st[1] = {{xa, ya, 2, 10}};
    double from, to, outer;
    CHECK(lw_span(st, 1, 236, 233, &from, &to, &outer), "span");
    double half = atan2(20, 100) * 180 / M_PI + asin(10 / hypot(100, 20)) * 180 / M_PI;
    CHECK(fabs(from + half) < 1e-6 && fabs(to - half) < 1e-6 && fabs(outer - hypot(100, 20) - 10) < 1e-9,
          "span across 3 o'clock: %g..%g", from, to);
    double xb[2] = {236, 236}, yb[2] = {233 + 140, 233 + 140};
    double xc[2] = {236 - 40, 236 - 40}, yc[2] = {233 + 130, 233 + 130};
    lw_stroke_t two[2] = {{xb, yb, 2, 20}, {xc, yc, 2, 20}};
    CHECK(lw_span(two, 2, 236, 233, &from, &to, &outer) && from > 80 && from < 90 && to > 100 && to < 120,
          "two eyes at 6 o'clock: %g..%g", from, to);
    double xd[1] = {236}, yd[1] = {233};
    lw_stroke_t centre[1] = {{xd, yd, 1, 5}};
    CHECK(!lw_span(centre, 1, 236, 233, &from, &to, &outer), "a point on the centre has no span");
}

static void test_orbit_table(void)
{
    static bot_orbit_table_t t;
    bot_orbit_table_init(&t, 1.15, 0.92);
    bot_pose_t ref = {.mood = BOT_WORKING, .eyes = BOT_EYES_ROUND, .scale_x = 1, .scale_y = 1};
    CHECK(bot_orbit_table_get(&t, &ref, 37.3) == bot_orbit_gaze(&ref, 1.15, 37.3, 0.92), "solves before the row is ready");
    for (int k = 0; k < BOT_ORBIT_KINDS; k++) bot_orbit_table_fill_part(&t, (bot_eyes_t)k, 0, BOT_ORBIT_TAB_N);
    double worst = 0;
    for (int k = 0; k < BOT_ORBIT_KINDS; k++) {
        ref.eyes = (bot_eyes_t)k;
        for (int i = 0; i < 36000; i += 7) {
            double deg = i / 100.0, e = fabs(bot_orbit_table_get(&t, &ref, deg) - bot_orbit_gaze(&ref, 1.15, deg, 0.92));
            if (e > worst) worst = e;
        }
    }
    CHECK(worst < 1e-3, "table within 1e-3 of the solver: %g", worst);
    ref.eyes = BOT_EYES_DASH;
    double g0 = bot_orbit_table_get(&t, &ref, 0);
    CHECK(fabs(bot_orbit_table_get(&t, &ref, 360) - g0) < 1e-9 && fabs(bot_orbit_table_get(&t, &ref, 720) - g0) < 1e-9 &&
              fabs(bot_orbit_table_get(&t, &ref, -360) - g0) < 1e-9,
          "wraps at multiples of 360");
    CHECK(fabs(bot_orbit_table_get(&t, &ref, -0.1) - bot_orbit_table_get(&t, &ref, 359.9)) < 1e-9 &&
              fabs(bot_orbit_table_get(&t, &ref, 359.9) - bot_orbit_gaze(&ref, 1.15, 359.9, 0.92)) < 1e-3,
          "-0.1 = 359.9, across the seam");
    bot_pose_t tri = ref;
    tri.triangle = 0.5;
    CHECK(bot_orbit_table_get(&t, &tri, 10) == bot_orbit_gaze(&tri, 1.15, 10, 0.92), "solves for a non-reference pose");

    static bot_orbit_table_t c;
    bot_orbit_table_init(&c, 1.15, 0.92);
    int i = 0, parts = 0;
    bool early = false;
    while (i < BOT_ORBIT_TAB_N) {
        i = bot_orbit_table_fill_part(&c, BOT_EYES_DASH, i, 7);
        parts++;
        if (i < BOT_ORBIT_TAB_N && atomic_load(&c.ready[BOT_EYES_DASH])) early = true;
    }
    CHECK(!early && atomic_load(&c.ready[BOT_EYES_DASH]) && parts == (BOT_ORBIT_TAB_N + 6) / 7, "chunked: published after the last part");
    CHECK(memcmp(c.gaze[BOT_EYES_DASH], t.gaze[BOT_EYES_DASH], sizeof c.gaze[0]) == 0, "chunked fill = whole fill");
    CHECK(bot_orbit_table_fill_part(&c, BOT_EYES_DASH, BOT_ORBIT_TAB_N, 7) == BOT_ORBIT_TAB_N &&
              bot_orbit_table_fill_part(&c, BOT_EYES_DASH, 0, 0) == BOT_ORBIT_TAB_N &&
              bot_orbit_table_fill_part(&c, (bot_eyes_t)99, 0, 7) == BOT_ORBIT_TAB_N,
          "bad ranges: done");
}

static void test_pose_same(void)
{
    const double R = 196, px = 1.0 / R;
    const bot_pose_t base = {.mood = BOT_WORKING, .eyes = BOT_EYES_DASH, .scale_x = 1, .scale_y = 1};
    CHECK(bot_pose_same(&base, &base, R), "same pose: no redraw");
    bot_pose_t p = base;
    p.gaze_x += 0.2 * px;
    p.scale_y += 0.1 * px;
    CHECK(bot_pose_same(&base, &p, R), "under half a pixel: no redraw");
    double *fields[] = {&p.gaze_x, &p.gaze_y, &p.triangle, &p.slump, &p.badge,
                        &p.scale_x, &p.scale_y, &p.offset_x, &p.offset_y, &p.hop_dy, &p.orbit};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        p = base;
        *fields[i] += 0.4 * px;
        CHECK(!bot_pose_same(&base, &p, R), "field %zu moved 0.4 px: redraw", i);
    }
    struct { double *f; double reach; const char *name; } scaled[] = {
        {&p.lid_l, BOT_LID_REACH, "lid_l"}, {&p.lid_r, BOT_LID_REACH, "lid_r"}, {&p.breath, BOT_BREATH_REACH, "breath"}};
    for (size_t i = 0; i < sizeof scaled / sizeof scaled[0]; i++) {
        p = base;
        *scaled[i].f += 0.4 * px / scaled[i].reach;
        CHECK(!bot_pose_same(&base, &p, R), "%s moved the eye 0.4 px: redraw", scaled[i].name);
        p = base;
        *scaled[i].f += 0.2 * px / scaled[i].reach;
        CHECK(bot_pose_same(&base, &p, R), "%s moved the eye 0.2 px: no redraw", scaled[i].name);
    }
    p = base;
    p.lid_l += 0.4 * px;
    CHECK(bot_pose_same(&base, &p, R), "a lid step of 0.4 px in lid units moves the eye 0.08 px: no redraw");
    bot_pose_t a = base, z = base;
    a.asleep = z.asleep = true;
    z.breath += 0.4 * px / BOT_BREATH_BOB;
    CHECK(!bot_pose_same(&a, &z, R), "asleep: a 0.4 px bob redraws");
    z = a;
    z.breath += 0.2 * px / BOT_BREATH_BOB;
    CHECK(bot_pose_same(&a, &z, R), "asleep: a 0.2 px bob does not");
    CHECK(!bot_pose_same(&base, &a, R), "falling asleep: redraw");
    p = base;
    p.scale_x += 0.2 * px;
    CHECK(!bot_pose_same(&base, &p, R), "scale moved 0.2 px: redraw");
    p = base;
    p.mood = BOT_DONE;
    CHECK(!bot_pose_same(&base, &p, R), "mood change: redraw");
    p = base;
    p.eyes = BOT_EYES_HAPPY;
    CHECK(!bot_pose_same(&base, &p, R), "eye shape change: redraw");
}

static int count_hops(bot_t *b, double t0, double seconds, bool *popped)
{
    int n = 0;
    bool was = false;
    *popped = false;
    for (double t = t0; t < t0 + seconds; t += 1.0 / 60) {
        bot_pose(b, t);
        if (b->hopping && !was) n++;
        was = b->hopping;
        *popped |= b->popping;
    }
    return n;
}

static void test_calm(void)
{
    CHECK(!bot_calm(false, true) && !bot_calm(true, false) && bot_calm(true, true), "calm = quiet and the setting");
    CHECK(bot_anim_allowed(BOT_ANIM_WAIT_HOP, false) && bot_anim_allowed(BOT_ANIM_MOOD_POP, false) &&
              bot_anim_allowed(BOT_ANIM_GLINT_CHASE, false) && bot_anim_allowed(BOT_ANIM_USER_INPUT, false),
          "not calm: all allowed");
    CHECK(!bot_anim_allowed(BOT_ANIM_WAIT_HOP, true) && !bot_anim_allowed(BOT_ANIM_MOOD_POP, true) &&
              !bot_anim_allowed(BOT_ANIM_GLINT_CHASE, true) && bot_anim_allowed(BOT_ANIM_USER_INPUT, true),
          "calm: only user input");
    CHECK(bot_chase_on(true, false, false) && !bot_chase_on(true, true, false) && bot_chase_on(true, true, true) &&
              !bot_chase_on(false, false, true),
          "calm blocks a new chase, lets a running one finish, no glint no chase");

    bot_t b;
    bool popped;
    bot_init(&b, 7, 0);
    bot_set_mood(&b, BOT_WAITING, 1);
    CHECK(count_hops(&b, 1, 20, &popped) >= 2 && popped, "waiting hops and pops");

    bot_init(&b, 7, 0);
    bot_set_calm(&b, true, 0.5);
    bot_set_mood(&b, BOT_WAITING, 1);
    CHECK(count_hops(&b, 1, 20, &popped) == 0 && !popped, "calm waiting: no hop, no pop");
    bot_set_mood(&b, BOT_ERROR, 21);
    CHECK(count_hops(&b, 21, 5, &popped) == 0 && !popped, "calm error: no pop");
    bot_set_mood(&b, BOT_WAITING, 26);
    bot_push(&b, 27);
    CHECK(count_hops(&b, 27, 0.5, &popped) == 1, "calm: a push still hops");
    bot_set_calm(&b, false, 30);
    CHECK(count_hops(&b, 30, 20, &popped) >= 2, "quiet off while waiting: hops resume");
    bot_set_calm(&b, true, 50);
    CHECK(count_hops(&b, 52, 20, &popped) == 0, "quiet on while waiting: hops stop");
    bot_set_calm(&b, false, 72);
    bot_set_mood(&b, BOT_DONE, 73);
    CHECK(count_hops(&b, 73, 1, &popped) == 0 && popped, "quiet off: mood change pops again");

    bot_init(&b, 7, 0);
    bot_set_calm(&b, true, 0.5);
    CHECK(bot_set_mood_user(&b, BOT_DONE, 1) && b.popping, "calm: a demo mood (user input) still pops");
    CHECK(!bot_set_mood_user(&b, BOT_DONE, 2), "same mood: nothing");
}

static int rim_run(rim_job_t *j, int hold_at, bool *started)
{
    unsigned gen = 0;
    int shape, mood;
    *started = rim_job_take(j, &gen, &shape, &mood);
    int n = 0;
    for (int k = 0; *started && k < 10; k++) {
        if (k == hold_at) rim_job_hold(j);
        if (!rim_job_live(j, gen)) break;
        n++;
    }
    return n;
}

static void test_rim_job(void)
{
    rim_job_t j = {0};
    bool started;
    rim_job_new(&j, 0, 0);
    CHECK(rim_run(&j, -1, &started) == 10 && started, "a job rasters every variant");
    rim_job_new(&j, 0, 0);
    unsigned before = rim_job_gen(&j);
    CHECK(rim_run(&j, 2, &started) == 2, "a hold stops the job at the next variant");
    CHECK(rim_job_gen(&j) != before, "a hold makes the dropped job's variants stale");
    CHECK(rim_run(&j, -1, &started) == 0 && !started, "no job starts while held");
    rim_job_new(&j, 0, 0);
    CHECK(rim_run(&j, -1, &started) == 0 && !started, "a new outline while held waits");
    CHECK(rim_job_release(&j), "the release asks for the job again");
    CHECK(!rim_job_release(&j), "a second release asks nothing");
    CHECK(rim_run(&j, -1, &started) == 10 && started, "after the release the job runs in full");
    CHECK(!rim_job_release(&j), "release without a hold asks nothing");
}

static int s_race_at, s_race_fired, s_race_done;
static bool s_race_took;
static unsigned s_race_gen, s_race_base_gen, *s_take_gen;
static int s_race_shape, s_race_mood, *s_take_shape, *s_take_mood;

void rj_race_point(rim_job_t *j, int at);
void rj_race_point(rim_job_t *j, int at)
{
    if (at != s_race_at) return;
    s_race_at = 0;
    s_race_fired = at;
    if (at <= 3) {
        s_race_done = (atomic_load(&j->shape) == 1) + (atomic_load(&j->mood) == BOT_ERROR) +
                      (atomic_load(&j->gen) != s_race_base_gen);
        s_race_took = rim_job_take(j, &s_race_gen, &s_race_shape, &s_race_mood);
    } else {
        s_race_done = (*s_take_gen != ~0u) + (*s_take_shape != -1) + (*s_take_mood != -1);
        rim_job_new(j, 1, BOT_ERROR);
    }
}

static void test_rim_job_order(void)
{
    for (int at = 1; at <= 3; at++) {
        rim_job_t j = {0};
        rim_job_new(&j, 0, BOT_IDLE);
        s_race_base_gen = rim_job_gen(&j);
        s_race_at = at;
        s_race_fired = 0;
        rim_job_new(&j, 1, BOT_ERROR);
        CHECK(s_race_fired == at && s_race_took, "a pending notify takes a job at rim_job_new's race point %d", at);
        CHECK(s_race_done == at, "rim_job_new's race point %d follows its atomic step %d (%d done)", at, at, s_race_done);
        CHECK(s_race_gen != rim_job_gen(&j) || (s_race_shape == 1 && s_race_mood == BOT_ERROR),
              "a job taken at rim_job_new's race point %d is stale or for the new outline (shape %d, mood %d)", at,
              s_race_shape, s_race_mood);
        unsigned gen;
        int shape, mood;
        CHECK(rim_job_take(&j, &gen, &shape, &mood) && gen == rim_job_gen(&j) && shape == 1 && mood == BOT_ERROR,
              "after race point %d the next take gets the new outline under the current generation", at);
    }
    for (int at = 4; at <= 7; at++) {
        rim_job_t j = {0};
        rim_job_new(&j, 0, BOT_IDLE);
        unsigned gen = ~0u;
        int shape = -1, mood = -1;
        s_take_gen = &gen;
        s_take_shape = &shape;
        s_take_mood = &mood;
        s_race_at = at;
        s_race_fired = 0;
        bool took = rim_job_take(&j, &gen, &shape, &mood);
        CHECK(s_race_fired == at && took, "rim_job_new runs at rim_job_take's race point %d", at);
        CHECK(s_race_done == at - 4, "rim_job_take's race point %d follows its load %d (%d done)", at, at - 4, s_race_done);
        CHECK(gen != rim_job_gen(&j) || (shape == 1 && mood == BOT_ERROR),
              "a job taken around a new outline at race point %d is stale or for the new outline (shape %d, mood %d)", at,
              shape, mood);
    }
}

static void test_chase_hold(void)
{
    glint_chase_t c;
    gc_init(&c, 7);
    gc_out_t o;
    c.hold = true;
    double t = 0;
    for (; t < GC_WAIT_MAX_S + 60; t += 0.1) {
        gc_tick(&c, t, true, 3.0, &o);
        CHECK(!o.chasing, "held: no chase at %g", t);
        if (o.chasing) break;
    }
    CHECK(c.work_s >= c.wait_s, "the working time still counts: %g of %g", c.work_s, c.wait_s);
    c.hold = false;
    gc_tick(&c, t + 0.1, true, 3.0, &o);
    CHECK(o.chasing, "released: the chase starts at once");
    gc_init(&c, 8);
    gc_tick(&c, 0, true, 3.0, &o);
    c.hold = true;
    CHECK(gc_start_now(&c, GC_STYLE_FULL, 0), "a forced chase is accepted");
    gc_tick(&c, 0.1, true, 3.0, &o);
    CHECK(!o.chasing, "and waits for the hold");
    c.hold = false;
    gc_tick(&c, 0.2, true, 3.0, &o);
    CHECK(o.chasing, "then runs");
}

static double lid_at(bot_t *b, double t)
{
    bot_pose_t p = bot_pose(b, t);
    return p.lid_l;
}

static void test_act(void)
{
    bot_t b;
    bot_init(&b, 21, 0);
    for (double t = 0; t < 3; t += 0.016) bot_pose(&b, t);
    b.next_blink_at = 1e9;
    double t = 3;
    while (b.blinking) bot_pose(&b, t += 0.016);
    bot_act_begin(&b, t);
    bot_act_lid(&b, 0.45, 0.25, t);
    CHECK(lid_at(&b, t + 0.1) > 0.05 && lid_at(&b, t + 0.1) < 0.45, "the lid floor eases in");
    CHECK(fabs(lid_at(&b, t + 0.3) - 0.45) < 1e-9, "then holds at 0.45: %g", lid_at(&b, t + 0.3));
    bot_act_lid(&b, 0, 0.3, t + 1);
    CHECK(fabs(lid_at(&b, t + 1.4)) < 1e-9, "and eases out");

    t += 2;
    for (double u = t; u < t + 3 && b.animating; u += 0.016) bot_pose(&b, u);
    bot_act_lid(&b, 0.2, 1, t + 3);
    bot_pose(&b, t + 3.5);
    CHECK(b.animating, "a lid easing counts as animating");
    bot_act_lid(&b, 0, 0.1, t + 3.6);
    bot_pose(&b, t + 3.8);
    t += 4;
    bot_act_eyes(&b, BOT_EYES_ROUND, t);
    CHECK(b.blinking, "a costume blinks");
    double bs = b.blink_start;
    bot_act_eyes(&b, BOT_EYES_HAPPY, t + 0.01);
    CHECK(b.blink_start == bs, "a second costume change rides the same blink");
    bot_act_eyes(&b, BOT_EYES_ROUND, t + 0.01);
    CHECK(bot_pose(&b, t + 0.02).eyes == BOT_EYES_DASH, "the old eyes until the lids close");
    CHECK(bot_pose(&b, t + 0.09).eyes == BOT_EYES_ROUND, "round once they close");
    for (double u = t + 0.1; u < t + 1; u += 0.016) bot_pose(&b, u);
    bot_set_mood(&b, BOT_DONE, t + 1);
    for (double u = t + 1; u < t + 2; u += 0.016) bot_pose(&b, u);
    CHECK(bot_pose(&b, t + 2).eyes == BOT_EYES_ROUND, "a mood change keeps the costume");
    bot_act_end(&b, true, 0.3, t + 2);
    CHECK(!b.acting && b.act_eyes == -1 && b.blinking, "the end blinks the costume away");
    for (double u = t + 2; u < t + 2.5; u += 0.016) bot_pose(&b, u);
    CHECK(bot_pose(&b, t + 2.5).eyes == BOT_EYES_HAPPY, "the mood's eyes again");
    CHECK(b.gaze_to_x == 0 && fabs(b.gaze_to_y - 0.1) < 1e-9, "back at the viewer");

    bot_init(&b, 22, 0);
    for (t = 0; t < 2; t += 0.016) bot_pose(&b, t);
    while (b.blinking) bot_pose(&b, t += 0.016);
    bot_act_eyes(&b, -1, t);
    CHECK(!b.blinking, "no costume change: no blink");
    for (int k = 0; k < 40; k++) {
        bot_t c = b;
        c.rng += (uint64_t)k * 977;
        bot_act_blink(&c, 2.2, t);
        CHECK(!c.double_blink_pending, "a slow blink is single (seed %d)", k);
    }
    bot_act_blink(&b, 2.2, t);
    CHECK(b.blinking && b.blink_speed == 2.2, "a slow blink");
    bot_pose(&b, t + 0.55);
    CHECK(b.blinking, "still closed-ish at 0.55 s");
    bot_pose(&b, t + 0.6);
    CHECK(!b.blinking, "over after 0.26 x 2.2 s");
    double sp = b.blink_speed;
    b.blinking = true;
    bot_act_blink(&b, 3, t + 1);
    CHECK(b.blink_speed == sp, "no new blink over a running one");
    b.blinking = false;

    bot_init(&b, 23, 0);
    for (t = 0; t < 2; t += 0.016) bot_pose(&b, t);
    bot_pose_t p0 = bot_pose(&b, t);
    CHECK(!b.hopping && p0.scale_x == 1, "on the ground");
    bot_act_hop(&b, t);
    bot_pose_t up = bot_pose(&b, t + 0.4);
    CHECK(up.offset_y - p0.offset_y > 0.15, "a hop lifts: %g", up.offset_y - p0.offset_y);
    bot_pose_t down = bot_pose(&b, t + BOT_HOP_DURATION_S + 0.05);
    CHECK(!b.hopping && down.scale_x == 1 && down.scale_y == 1, "and lands");

    bot_init(&b, 24, 0);
    for (t = 0; t < 2; t += 0.016) bot_pose(&b, t);
    bot_act_begin(&b, t);
    CHECK(b.gaze_to_x == 0 && fabs(b.gaze_to_y - 0.1) < 1e-9 && b.gaze_dur == 0.42, "begin glides to the viewer");
    bot_pose_t settled = bot_pose(&b, t + 0.5);
    CHECK(fabs(settled.offset_x) < 1e-9 && fabs(settled.offset_y - 0.003) < 1e-9, "and leans as for the viewer within the pre-roll: %g %g",
          settled.offset_x, settled.offset_y);
    bot_act_gaze(&b, 0.3, 0.2, true, t + 1);
    CHECK(b.gaze_to_x == 0.3 && b.gaze_to_y == 0.2 && b.gaze_dur == 0.42, "a glide: 0.42 s");
    bot_act_gaze(&b, -0.3, 0.1, false, t + 2);
    CHECK(b.gaze_dur < 0.1, "a saccade: fast (%g)", b.gaze_dur);
    bot_pose_t held = bot_pose(&b, t + 2.6);
    CHECK(held.offset_x == settled.offset_x && held.offset_y == settled.offset_y, "acting gazes keep the body lean: %g %g", held.offset_x,
          held.offset_y);
    double lx = b.gaze_to_x, ly = b.gaze_to_y;
    bot_look(&b, 0, 0.85, t + 3);
    CHECK(b.gaze_to_x == lx && b.gaze_to_y == ly, "a knob turn does not move the eyes while acting");
    bot_act_end(&b, true, 0.3, t + 4);
    bot_look(&b, 0, 0.85, t + 5);
    CHECK(b.gaze_to_y == 0.85, "it does again after the act");

    bot_init(&b, 25, 0);
    for (t = 0; t < 2; t += 0.016) bot_pose(&b, t);
    bot_act_begin(&b, t);
    bot_act_lid(&b, 0.4, 0.2, t);
    bot_act_eyes(&b, BOT_EYES_ROUND, t);
    bot_push(&b, t + 0.1);
    CHECK(!b.hopping && !b.popping, "a push does not hop the outline while acting");
    bot_set_mood(&b, BOT_WORKING, t + 1);
    double wx = b.gaze_to_x, wy = b.gaze_to_y, ws = b.gaze_start;
    bot_act_drop(&b, t + 1);
    CHECK(!b.acting && b.gaze_to_x == wx && b.gaze_to_y == wy && b.gaze_start == ws, "dropping the act keeps the new mood's gaze");
    CHECK(b.act_eyes == -1 && b.act_lid.to == 0, "but ends the costume and the lid floor");
    for (double u = t + 1; u < t + 2; u += 0.016) bot_pose(&b, u);
    CHECK(bot_pose(&b, t + 2).eyes == BOT_EYES_DASH && fabs(bot_pose(&b, t + 2).lid_l) < 1e-9, "working eyes, open");
    bot_push(&b, t + 2);
    CHECK(b.hopping, "a push hops again after the act");
    bot_act_drop(&b, t + 3);
    CHECK(!b.acting, "a second drop: nothing");
}

static void test_demo_acts(void)
{
    bot_t b;
    bot_init(&b, 7, 0);
    bot_set_calm(&b, true, 0.5);
    bot_act_begin(&b, 1);
    bot_hop(&b, 2);
    bot_pose(&b, 2.5);
    CHECK(b.hopping && !b.popping, "a demo hop runs while calm and acting, alone");
    bot_pose(&b, 2 + BOT_HOP_DURATION_S + 0.01);
    CHECK(!b.hopping, "the hop ends after BOT_HOP_DURATION_S");
    bot_pop(&b, 4);
    bot_pose(&b, 4.2);
    CHECK(b.popping && !b.hopping, "a demo pop, alone");
    bot_pose(&b, 4.41);
    CHECK(!b.popping, "the pop ends after 0.4 s");

    glint_chase_t c;
    gc_out_t o;
    gc_init(&c, 5);
    gc_tick(&c, 0, true, 3.0, &o);
    gc_tick(&c, 100, true, 3.0, &o);
    double work = c.work_s, wait = c.wait_s;
    CHECK(work > 0 && work < wait, "working time counts: %g of %g", work, wait);
    CHECK(gc_start_now(&c, GC_STYLE_HALF, 0), "a demo chase");
    gc_tick(&c, 100.1, true, 3.0, &o);
    CHECK(o.chasing, "chasing");
    gc_resume(&c, work, wait);
    gc_tick(&c, 100.2, true, 3.0, &o);
    CHECK(!o.chasing && c.phase == GC_IDLE && fabs(c.work_s - work - 0.1) < 1e-9 && c.wait_s == wait,
          "resume: no chase, the working time and wait as before: %g", c.work_s);
    CHECK(gc_start_now(&c, GC_STYLE_FULL, 0), "idle after resume");

    gc_init(&c, 21);
    gc_tick(&c, 0, true, 3.0, &o);
    CHECK(gc_start_now(&c, GC_STYLE_FULL, 4), "a real chase");
    gc_tick(&c, 1, true, 3.0, &o);
    gc_tick(&c, 5, true, 3.0, &o);
    CHECK(o.chasing && c.phase == GC_CHASE, "chasing at 5");
    glint_chase_t saved = c;
    double left = c.end_at - 5, in = 5 - c.start_at;
    int sx = c.shift_x, sy = c.shift_y;
    uint32_t rng = c.rng;
    gc_resume(&c, c.work_s, c.wait_s);
    CHECK(gc_start_now(&c, GC_STYLE_HALF, 2), "the demo's chase");
    for (double t = 5; t < 30; t += 0.5) gc_tick(&c, t, true, 3.0, &o);
    CHECK(c.shift_x != sx || c.shift_y != sy || c.rng != rng, "the demo chase moved the label or the rng");
    gc_restore(&c, &saved, 5, 40);
    CHECK(c.phase == GC_CHASE && c.shift_x == sx && c.shift_y == sy && c.rng == rng, "restored: phase, shift, rng");
    CHECK(fabs(c.end_at - 40 - left) < 1e-9 && fabs(40 - c.start_at - in) < 1e-9, "the real chase goes on where it was");
    gc_tick(&c, 40.1, true, 3.0, &o);
    CHECK(o.chasing && o.style == GC_STYLE_FULL, "still the real chase after the scene");

    gc_init(&c, 23);
    gc_tick(&c, 0, true, 3.0, &o);
    CHECK(gc_start_now(&c, GC_STYLE_FULL, 4), "a real chase before a non-chase scene");
    gc_tick(&c, 1, true, 3.0, &o);
    gc_tick(&c, 4, true, 3.0, &o);
    saved = c;
    left = c.end_at - 4;
    gc_resume(&c, c.work_s, c.wait_s);
    c.hold = true;
    bool orbited = false;
    for (double t = 4; t < 20; t += 0.25) {
        gc_tick(&c, t, true, 3.0, &o);
        orbited = orbited || o.chasing;
    }
    CHECK(!orbited, "the real chase pauses during a non-chase scene");
    gc_restore(&c, &saved, 4, 20);
    gc_tick(&c, 20.1, true, 3.0, &o);
    CHECK(o.chasing && fabs(c.end_at - 20 - left) < 1e-9, "and resumes after it where it was");

    gc_init(&c, 22);
    gc_tick(&c, 0, true, 3.0, &o);
    gc_tick(&c, 50, true, 3.0, &o);
    saved = c;
    double w = c.work_s;
    c.hold = true;
    for (double t = 50; t < 50 + GC_WAIT_MAX_S + 10; t += 1) gc_tick(&c, t, true, 3.0, &o);
    CHECK(c.phase == GC_IDLE && !o.chasing, "a held scene in a working mood starts no chase");
    gc_restore(&c, &saved, 50, 60 + GC_WAIT_MAX_S);
    gc_tick(&c, 60 + GC_WAIT_MAX_S + 0.5, true, 3.0, &o);
    CHECK(fabs(c.work_s - w - 0.5) < 1e-9 && !c.hold, "the scene's working time is not counted: %g", c.work_s);

    bot_init(&b, 3, 0);
    bot_set_mood(&b, BOT_SLEEPY, 1);
    CHECK(!bot_set_mood(&b, BOT_IDLE, 2) && b.mood == BOT_SLEEPY, "idle never wakes a sleepy bot");
    bot_wake(&b, 3);
    CHECK(b.mood == BOT_IDLE, "bot_wake: idle again");
    bot_set_mood(&b, BOT_WORKING, 4);
    bot_wake(&b, 5);
    CHECK(b.mood == BOT_WORKING, "bot_wake leaves other moods");
}

typedef struct {
    int micros, saccades, gaps, toward_n, bad_amp, bad_dur, early, late, moved_saccade, moved_fix, moved_lean, blinked;
    double amp_lo, amp_hi, gap_lo, gap_hi, toward_sum, drift_max;
    double gap[8192];
} micro_stats_t;

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double micro_median(micro_stats_t *s)
{
    if (!s->gaps) return 0;
    qsort(s->gap, (size_t)s->gaps, sizeof s->gap[0], cmp_double);
    return s->gap[s->gaps / 2];
}

static void micro_run(bot_t *b, double t0, double secs, double amp_lo, double amp_hi, micro_stats_t *s)
{
    memset(s, 0, sizeof *s);
    s->amp_lo = 1e9;
    s->gap_lo = 1e9;
    double fix_x = b->gaze_to_x, fix_y = b->gaze_to_y, fix_from = b->gaze_start + b->gaze_dur, last_micro = -1;
    const double dt = 1.0 / 120;
    for (double t = t0; t < t0 + secs; t += dt) {
        bot_t was = *b;
        bot_pose_t p = bot_pose(b, t);
        if (b->gaze_start == was.gaze_start) {
            if (t > fix_from && b->mood == was.mood) s->drift_max = fmax(s->drift_max, hypot(p.gaze_x - fix_x, p.gaze_y - fix_y));
            continue;
        }
        if (t >= was.next_saccade_at) {
            s->saccades++;
            fix_x = b->gaze_to_x;
            fix_y = b->gaze_to_y;
            fix_from = b->gaze_start + b->gaze_dur;
            last_micro = -1;
            continue;
        }
        s->micros++;
        s->moved_saccade += b->next_saccade_at != was.next_saccade_at;
        s->moved_fix += b->fix_x != was.fix_x || b->fix_y != was.fix_y;
        double dx = b->gaze_to_x - b->gaze_from_x, dy = b->gaze_to_y - b->gaze_from_y, amp = hypot(dx, dy);
        s->amp_lo = fmin(s->amp_lo, amp);
        s->amp_hi = fmax(s->amp_hi, amp);
        s->bad_amp += amp < amp_lo - 1e-9 || amp > amp_hi + 1e-9;
        s->bad_dur += b->gaze_dur != 0.025;
        s->early += t - fix_from < 0.6 - dt;
        s->late += t + 0.05 >= b->next_saccade_at;
        s->moved_lean += memcmp(&b->lean_x, &was.lean_x, sizeof b->lean_x) || memcmp(&b->lean_y, &was.lean_y, sizeof b->lean_y);
        s->blinked += b->blinking && !was.blinking && was.next_blink_at > t;
        double fx = fix_x - b->gaze_from_x, fy = fix_y - b->gaze_from_y, fd = hypot(fx, fy);
        if (fd > 0.015) {
            s->toward_sum += (dx * fx + dy * fy) / (amp * fd);
            s->toward_n++;
        }
        if (last_micro >= 0 && s->gaps < (int)(sizeof s->gap / sizeof s->gap[0])) {
            double g = t - last_micro;
            s->gap[s->gaps++] = g;
            s->gap_lo = fmin(s->gap_lo, g);
            s->gap_hi = fmax(s->gap_hi, g);
        }
        last_micro = t;
    }
}

static void test_micro_saccades(void)
{
    micro_stats_t *s = malloc(sizeof *s);
    const double dt = 1.0 / 120;
    int idle_micros = 0;
    for (int seed = 1; seed <= 4; seed++) {
        bot_t b;
        bot_init(&b, 0xC0FFEE + (uint64_t)seed, 0);
        bot_set_sleep_after(&b, 0);
        micro_run(&b, 0, 280, 0.012, 0.025, s);
        idle_micros += s->micros;
        CHECK(s->micros >= 280 / 6, "idle seed %d: %d micro-saccades in 280 s", seed, s->micros);
        CHECK(!s->bad_amp, "idle seed %d: amplitude %.4f..%.4f outside 0.012..0.025", seed, s->amp_lo, s->amp_hi);
        CHECK(!s->bad_dur, "idle seed %d: %d micro-saccades not 25 ms", seed, s->bad_dur);
        CHECK(!s->early, "idle seed %d: %d micro-saccades in the first 0.6 s of a fixation", seed, s->early);
        CHECK(!s->late, "idle seed %d: %d micro-saccades within 50 ms of the next saccade", seed, s->late);
        CHECK(!s->moved_lean, "idle seed %d: %d micro-saccades moved the lean", seed, s->moved_lean);
        CHECK(!s->moved_saccade && !s->moved_fix, "idle seed %d: micro-saccades moved the saccade deadline %d times, the fixation target %d",
              seed, s->moved_saccade, s->moved_fix);
        CHECK(s->saccades >= 280 / 8, "idle seed %d: %d saccades in 280 s", seed, s->saccades);
        CHECK(!s->blinked, "idle seed %d: %d micro-saccades started a blink", seed, s->blinked);
        CHECK(s->gap_lo >= 0.3 - dt && s->gap_hi <= 2.5 + dt, "idle seed %d: gaps %.3f..%.3f outside 0.3..2.5 s", seed, s->gap_lo, s->gap_hi);
        CHECK(s->toward_n > 10 && s->toward_sum / s->toward_n > 0.6, "idle seed %d: biased back to the target, mean cos %.2f over %d", seed,
              s->toward_n ? s->toward_sum / s->toward_n : 0, s->toward_n);
        CHECK(s->drift_max < 0.06, "idle seed %d: the gaze drifts %.3f from its fixation target", seed, s->drift_max);
        double med = micro_median(s);
        CHECK(med > 0.6 && med < 1.05, "idle seed %d: median gap %.2f s, want about 0.8", seed, med);
    }
    printf("micro-saccades: idle %d in 4 x 280 s\n", idle_micros);

    bot_t b;
    bot_init(&b, 0xBEEF, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    micro_run(&b, 0, BOT_ASLEEP_AFTER_S - 10, 0.006, 0.0125, s);
    CHECK(s->micros >= 100, "sleepy: %d micro-saccades before asleep", s->micros);
    CHECK(!s->bad_amp, "sleepy: half amplitude, got %.4f..%.4f", s->amp_lo, s->amp_hi);
    CHECK(s->gap_lo >= 0.8 - dt && s->gap_hi <= 5 + dt, "sleepy: gaps %.3f..%.3f outside 0.8..5 s", s->gap_lo, s->gap_hi);
    double med = micro_median(s);
    CHECK(med > 1.6 && med < 2.5, "sleepy: median gap %.2f s, want about 2", med);
    CHECK(!s->early && !s->late && !s->moved_lean && !s->moved_saccade && !s->moved_fix,
          "sleepy: timing, lean, deadline, target (%d early, %d late, %d lean, %d deadline, %d target)", s->early, s->late, s->moved_lean,
          s->moved_saccade, s->moved_fix);

    for (int mood = BOT_WORKING; mood <= BOT_DONE; mood++) {
        bot_init(&b, 0xABC + (uint64_t)mood, 0);
        bot_set_mood(&b, (bot_mood_t)mood, 0);
        micro_run(&b, 0, 300, 0.012, 0.025, s);
        CHECK(s->micros > 0 && !s->bad_amp && !s->early && !s->late && !s->moved_lean && !s->moved_saccade && !s->moved_fix,
              "mood %d: %d micro-saccades, %d off size, %d early, %d late, %d lean, %d deadline, %d target", mood, s->micros, s->bad_amp,
              s->early, s->late, s->moved_lean, s->moved_saccade, s->moved_fix);
    }
    free(s);

    bot_init(&b, 77, 0);
    double t = 0;
    for (; t < 5; t += dt) bot_pose(&b, t);
    for (double end = t + 6; t < end; t += dt) {
        double a = 2 * M_PI * t / 3;
        bot_track(&b, cos(a) * 0.85, -sin(a) * 0.85, t);
        double gs = b.gaze_start;
        bot_pose(&b, t);
        CHECK(b.gaze_start == gs, "no micro-saccade while tracking a chase (t %.2f)", t);
        if (b.gaze_start != gs) break;
    }
    bot_track_end(&b, t);
    bot_look_still(&b, 0.6, 0, t);
    bot_pose_t held = bot_pose(&b, t + 0.2);
    for (double end = t + 6; t < end; t += dt) {
        bot_hold_gaze(&b, t + 0.5);
        bot_pose_t p = bot_pose(&b, t + 0.2);
        CHECK(p.gaze_x == held.gaze_x && p.gaze_y == held.gaze_y, "no micro-saccade while a half chase holds the gaze (t %.2f)", t);
        if (p.gaze_x != held.gaze_x) break;
    }

    double toward_old = 0;
    int released = 0;
    for (int seed = 0; seed < 40; seed++) {
        bot_init(&b, 300 + (uint64_t)seed, 0);
        for (t = 0; t < 2; t += dt) bot_pose(&b, t);
        bot_look(&b, -0.8, -0.5, t);
        for (double end = t + 0.5; t < end; t += dt) bot_pose(&b, t);
        for (double end = t + 3; t < end; t += dt) {
            bot_track(&b, 0.85, 0, t);
            bot_pose(&b, t);
        }
        bot_track_end(&b, t);
        double rs = b.gaze_start, rx = b.gaze_to_x, ry = b.gaze_to_y;
        bool quiet = true;
        for (double end = t + 0.6 - dt / 2; t < end; t += dt) {
            bot_pose(&b, t);
            quiet = quiet && b.gaze_start == rs && b.gaze_dur == 0 && b.gaze_to_x == rx && b.gaze_to_y == ry;
        }
        CHECK(quiet, "seed %d: no micro-saccade on the chase's release frame or in the 0.6 s after it", seed);
        for (double end = t + 0.5; t < end && b.gaze_start == rs; t += dt) bot_pose(&b, t);
        if (b.gaze_start == rs || hypot(b.gaze_to_x - rx, b.gaze_to_y - ry) > 0.03) continue;
        double dx = b.gaze_to_x - b.gaze_from_x, dy = b.gaze_to_y - b.gaze_from_y, ox = -0.8 - b.gaze_from_x, oy = -0.5 - b.gaze_from_y;
        toward_old += (dx * ox + dy * oy) / (hypot(dx, dy) * hypot(ox, oy));
        released++;
    }
    CHECK(released >= 30, "a micro-saccade follows the release hold: %d of 40", released);
    CHECK(released && toward_old / released < 0.4, "the first micro-saccade after a chase ignores the pre-chase target: mean cos %.2f over %d",
          released ? toward_old / released : 0, released);

    bot_init(&b, 78, 0);
    for (t = 0; t < 3; t += dt) bot_pose(&b, t);
    bot_act_begin(&b, t);
    double gs = b.gaze_start;
    for (double end = t + 6; t < end; t += dt) bot_pose(&b, t);
    CHECK(b.gaze_start == gs, "no micro-saccade while acting");
}

static void test_outline_dy(void)
{
    const double R = 196, S = 0.45;
    const struct { double px; int want; } cases[] = {
        {0, 0}, {0.6, 0}, {-0.6, 0}, {0.99, 0}, {-0.99, 0}, {1.2, 2}, {-1.2, -2}, {2.9, 2}, {-2.9, -2}, {3.1, 4}, {-3.1, -4}, {-10.4, -10},
    };
    bot_pose_t p = {.scale_x = 1, .scale_y = 1};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        p.hop_dy = -cases[i].px / (R * S);
        p.offset_y = p.hop_dy + 0.03;
        int dy = bot_rim_dy(&p, R, S);
        CHECK(dy == cases[i].want, "outline dy for %.2f px = %d, want %d", cases[i].px, dy, cases[i].want);
    }
    for (double x = -14; x <= 14; x += 0.013) {
        p.hop_dy = -x / (R * S);
        int a = bot_rim_dy(&p, R, S);
        p.hop_dy = x / (R * S);
        int b = bot_rim_dy(&p, R, S);
        CHECK(a == -b && a % 2 == 0 && fabs(a - x) <= 1 + 1e-9, "outline dy %.3f px: %d and %d for the mirror", x, a, b);
        if (a != -b) break;
    }

    double lean_px = 0;
    int moved = 0;
    for (int mood = BOT_IDLE; mood <= BOT_DONE; mood++) {
        bot_t bt;
        bot_init(&bt, 900 + (uint64_t)mood, 0);
        bot_set_mood(&bt, (bot_mood_t)mood, 0);
        for (double t = 0; t < 120; t += 1.0 / 60) {
            if (fmod(t, 7) < 1.0 / 60) bot_look(&bt, 0, 0.85, t);
            bot_pose_t q = bot_pose(&bt, t);
            if (!bt.hopping) {
                lean_px = fmax(lean_px, fabs(q.offset_y) * R * S);
                bool still = q.hop_dy == 0 && bot_rim_dy(&q, R, S) == 0;
                CHECK(still, "mood %d at %.2f: the outline moves %d px off a hop", mood, t, bot_rim_dy(&q, R, S));
                if (!still) break;
            } else {
                moved += bot_rim_dy(&q, R, S) != 0;
            }
        }
    }
    CHECK(lean_px >= 1.5, "the glance lean is still there for the eyes: %.2f px", lean_px);
    CHECK(moved > 0, "a hop still moves the outline");
}

static void test_badged_rest(void)
{
    int rest = 0, viewer = 0, other = 0;
    for (int seed = 0; seed < 6; seed++) {
        bot_t b;
        bot_init(&b, 4000 + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_DONE, 0);
        for (double t = 0; t < 300; t += 1.0 / 60) {
            double nsa = b.next_saccade_at;
            bot_pose(&b, t);
            if (b.next_saccade_at == nsa || t < 0.1) continue;
            double x = b.gaze_to_x, y = b.gaze_to_y;
            if (fabs(x - 0.5) <= 0.08 + 1e-9 && fabs(y - 0.6) <= 0.06 + 1e-9) rest++;
            else if (fabs(x) <= 0.06 + 1e-9 && fabs(y - 0.1) <= 0.06 + 1e-9) viewer++;
            else other++;
        }
    }
    CHECK(rest > 50 && viewer > 50 && other == 0, "done rests at (0.5, 0.6): %d rest, %d viewer, %d elsewhere", rest, viewer, other);
    CHECK(bot_label_mark(BOT_WORKING) && bot_label_mark(BOT_WAITING) && !bot_label_mark(BOT_ERROR),
          "the label keeps its tool mark except in error");
}

typedef struct {
    int cycles;
    double lo, hi, mean, b_lo, b_hi;
} breath_stats_t;

static void breath_run(bot_t *b, double t0, double secs, breath_stats_t *s)
{
    const double dt = 1.0 / 120;
    double prev = 0, last_up = -1, sum = 0;
    int ups = 0;
    *s = (breath_stats_t){.lo = 1e9, .b_lo = 1e9, .b_hi = -1e9};
    for (double t = t0; t < t0 + secs; t += dt) {
        bot_pose_t p = bot_pose(b, t);
        s->b_lo = fmin(s->b_lo, p.breath);
        s->b_hi = fmax(s->b_hi, p.breath);
        if (t > t0 && prev < 0 && p.breath >= 0) {
            if (ups >= 2) {
                double per = t - last_up;
                s->lo = fmin(s->lo, per);
                s->hi = fmax(s->hi, per);
                sum += per;
                s->cycles++;
            }
            last_up = t;
            ups++;
        }
        prev = p.breath;
    }
    s->mean = s->cycles ? sum / s->cycles : 0;
}

static void test_breath(void)
{
    static const struct { bot_mood_t m; double per; const char *name; } M[] = {
        {BOT_IDLE, 4.2, "idle"}, {BOT_WORKING, 3.2, "working"}, {BOT_WAITING, 2.6, "waiting"},
        {BOT_DONE, 3.6, "done"}, {BOT_SLEEPY, 5.5, "sleepy"}, {BOT_ERROR, 3.0, "error"}};
    const double dt = 1.0 / 120;
    for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) {
        bot_t b;
        bot_init(&b, 0x5EED + i, 0);
        bot_set_sleep_after(&b, 0);
        if (M[i].m != BOT_IDLE) bot_set_mood(&b, M[i].m, 0);
        breath_stats_t s;
        breath_run(&b, 0, BOT_ASLEEP_AFTER_S - 20, &s);
        CHECK(s.cycles > 80, "%s: %d breaths", M[i].name, s.cycles);
        CHECK(s.lo >= 0.85 * M[i].per - 2 * dt && s.hi <= 1.15 * M[i].per + 2 * dt, "%s: periods %.2f..%.2f outside %.2f +-15 %%",
              M[i].name, s.lo, s.hi, M[i].per);
        CHECK(fabs(s.mean / M[i].per - 1) < 0.03, "%s: mean period %.3f, want %.1f", M[i].name, s.mean, M[i].per);
        CHECK(s.hi - s.lo > 0.15 * M[i].per, "%s: jittered per cycle, periods %.2f..%.2f", M[i].name, s.lo, s.hi);
        CHECK(s.b_lo >= -1 && s.b_hi <= 1 && s.b_lo < -0.99 && s.b_hi > 0.99, "%s: breath %.3f..%.3f", M[i].name, s.b_lo, s.b_hi);
    }
    bot_t b;
    bot_init(&b, 0xA51EE9, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    bot_pose(&b, BOT_ASLEEP_AFTER_S + 1);
    breath_stats_t s;
    breath_run(&b, BOT_ASLEEP_AFTER_S + 20, 1200, &s);
    CHECK(b.asleep && s.cycles > 150, "asleep: %d breaths", s.cycles);
    CHECK(s.lo >= 0.85 * 6 - 2 * dt && s.hi <= 1.15 * 6 + 2 * dt && fabs(s.mean / 6 - 1) < 0.03,
          "asleep: a bob every 6 s, periods %.2f..%.2f mean %.2f", s.lo, s.hi, s.mean);

    bot_init(&b, 9, 0);
    bot_set_sleep_after(&b, 0);
    bool scaled = false, breathed = false;
    for (double t = 1; t < 120; t += 1.0 / 60) {
        bot_pose_t p = bot_pose(&b, t);
        scaled |= p.scale_x != 1 || p.scale_y != 1;
        breathed |= fabs(p.breath) > 0.9;
    }
    CHECK(breathed && !scaled, "breath never goes into scale_x/y (outline variants)");
}

static void eye_geom(const bot_pose_t *p, double *cy, double *h)
{
    bot_stroke_t s[2];
    bot_eye_strokes(p, 1.15, s);
    double lo = 1e9, hi = -1e9, sum = 0;
    for (int i = 0; i < s[0].n; i++) {
        lo = fmin(lo, s[0].y[i]);
        hi = fmax(hi, s[0].y[i]);
        sum += s[0].y[i];
    }
    *cy = sum / s[0].n;
    *h = hi - lo + s[0].width;
}

static void test_breath_eyes(void)
{
    static const bot_eyes_t E[] = {BOT_EYES_DASH, BOT_EYES_ROUND, BOT_EYES_ANGRY};
    for (size_t i = 0; i < sizeof E / sizeof E[0]; i++) {
        bot_pose_t p = {.eyes = E[i], .gaze_y = 0.1, .scale_x = 1, .scale_y = 1};
        double c0, h0, c1, h1, c2, h2;
        eye_geom(&p, &c0, &h0);
        p.breath = 1;
        eye_geom(&p, &c1, &h1);
        p.breath = -1;
        eye_geom(&p, &c2, &h2);
        CHECK(fabs(c1 - c0 - 0.006) < 1e-9 && fabs(c2 - c0 + 0.006) < 1e-9, "eyes %d: centre +-0.006, got %+.5f %+.5f", E[i], c1 - c0,
              c2 - c0);
        bot_stroke_t a[2], b1[2];
        p.breath = 0;
        bot_eye_strokes(&p, 1.15, a);
        p.breath = 1;
        bot_eye_strokes(&p, 1.15, b1);
        double ha = hypot(a[0].x[1] - a[0].x[0], a[0].y[1] - a[0].y[0]) + a[0].width;
        double hb = hypot(b1[0].x[1] - b1[0].x[0], b1[0].y[1] - b1[0].y[0]) + b1[0].width;
        CHECK(fabs(hb / ha - 1.025) < 1e-9, "eyes %d: height x1.025 on the inhale, got x%.5f", E[i], hb / ha);
    }
    bot_pose_t p = {.eyes = BOT_EYES_HAPPY, .scale_x = 1, .scale_y = 1};
    double c0, h0, c1, h1;
    eye_geom(&p, &c0, &h0);
    p.breath = 1;
    eye_geom(&p, &c1, &h1);
    CHECK(c1 > c0 && h1 > h0, "happy eyes breathe too: centre %+.5f height %+.5f", c1 - c0, h1 - h0);

    bot_pose_t z = {.eyes = BOT_EYES_DASH, .lid_l = 1, .lid_r = 1, .gaze_y = -0.35, .scale_x = 1, .scale_y = 1, .asleep = true};
    eye_geom(&z, &c0, &h0);
    z.breath = 1;
    eye_geom(&z, &c1, &h1);
    CHECK(fabs(c1 - c0 - BOT_BREATH_BOB) < 1e-9 && fabs(h1 - h0) < 1e-12, "asleep: a %.2f px bob, height unchanged (%+.6f)",
          (c1 - c0) * 195.72, h1 - h0);
    CHECK(BOT_BREATH_BOB * 195.72 > 1.45 && BOT_BREATH_BOB * 195.72 < 1.55, "the bob is 1.5 px");
}

static double redraws_per_s(bot_t *b, double t0, double secs, bool no_breath)
{
    bot_pose_t last = {0};
    int n = 0;
    bool drawn = false;
    for (double t = t0; t < t0 + secs; t += 0.016) {
        bot_pose_t p = bot_pose(b, t);
        if (no_breath) p.breath = 0;
        if (!drawn || !bot_pose_same(&p, &last, 195.72)) {
            n += drawn;
            last = p;
            drawn = true;
        }
    }
    return n / secs;
}

static void test_breath_redraws(void)
{
    bot_t b;
    double with[3], without[3];
    static const bot_mood_t M[] = {BOT_IDLE, BOT_WORKING, BOT_DONE};
    for (int i = 0; i < 3; i++) {
        bot_init(&b, 77, 0);
        bot_set_sleep_after(&b, 0);
        if (M[i] != BOT_IDLE) bot_set_mood(&b, M[i], 0);
        with[i] = redraws_per_s(&b, 2, 280, false);
        bot_init(&b, 77, 0);
        bot_set_sleep_after(&b, 0);
        if (M[i] != BOT_IDLE) bot_set_mood(&b, M[i], 0);
        without[i] = redraws_per_s(&b, 2, 280, true);
        CHECK(with[i] - without[i] > 1.0 && with[i] - without[i] < 3.0, "mood %d: breath adds %.2f redraws/s, want about 2", M[i],
              with[i] - without[i]);
    }
    bot_init(&b, 77, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    double sleepy = redraws_per_s(&b, 2, BOT_ASLEEP_AFTER_S - 4, false);
    double asleep = redraws_per_s(&b, BOT_ASLEEP_AFTER_S + 2, 1200, false);
    printf("breath redraws/s: idle %.1f (%.1f without), working %.1f (%.1f), done %.1f (%.1f), sleepy %.1f, asleep %.1f\n", with[0],
           without[0], with[1], without[1], with[2], without[2], sleepy, asleep);
    CHECK(sleepy < 16, "sleepy: %.1f redraws/s, the breathing lid must not redraw every frame", sleepy);
    CHECK(asleep > 1 && asleep < 2.5, "asleep: %.2f redraws/s, the bob alone", asleep);
}

static void test_sleepy_lid(void)
{
    bot_t b;
    bot_init(&b, 31, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    double lo = 1, hi = 0, worst = 0;
    int n = 0;
    for (double t = 2; t < 300; t += 1.0 / 60) {
        bot_pose_t p = bot_pose(&b, t);
        if (b.blinking || b.acting) continue;
        double want = 0.80 + 0.12 * p.breath;
        worst = fmax(worst, fmax(fabs(p.lid_l - want), fabs(p.lid_r - want)));
        lo = fmin(lo, p.lid_l);
        hi = fmax(hi, p.lid_l);
        n++;
    }
    CHECK(n > 1000 && worst < 1e-9, "sleepy lid = 0.80 + 0.12 breath, off by %g", worst);
    CHECK(lo < 0.69 && hi > 0.91, "sleepy lid breathes %.3f..%.3f", lo, hi);
    bot_set_mood(&b, BOT_WORKING, 300);
    bot_pose_t p = bot_pose(&b, 302);
    CHECK(p.lid_l < 0.01 && p.lid_r < 0.01, "awake: no lid floor, got %.3f", p.lid_l);
}

static void test_heavy_blinks(void)
{
    bot_t b;
    bot_init(&b, 0xB11C, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    const double dt = 1.0 / 240;
    int closes = 0, bad_hold = 0, overshoots = 0, singles = 0, starts = 0;
    double prev = 0, shut_at = -1, last_start = -1, gaps[256];
    int ngaps = 0;
    bool was = false, dipped = false;
    double t_end = BOT_ASLEEP_AFTER_S - 5;
    for (double t = 3; t < t_end; t += dt) {
        bot_t before = b;
        bot_pose_t p = bot_pose(&b, t);
        if (b.blinking && (!was || b.blink_start != before.blink_start)) {
            starts++;
            CHECK(b.blink_heavy, "sleepy blinks are heavy");
            CHECK(b.blink_hold >= 0.4 && b.blink_hold <= 1.2, "hold %.2f outside 0.4..1.2 s", b.blink_hold);
            if (last_start >= 0 && ngaps < 256) gaps[ngaps++] = b.blink_start - last_start;
            last_start = b.blink_start;
            dipped = false;
        }
        if (p.lid_l >= 1 - 1e-12 && prev < 1 - 1e-12) shut_at = t;
        if (p.lid_l < 1 - 1e-12 && prev >= 1 - 1e-12 && shut_at >= 0) {
            double hold = t - shut_at;
            closes++;
            bad_hold += hold < 0.4 - 2 * dt || hold > 1.2 + 2 * dt;
            shut_at = -1;
        }
        if (b.blinking && t - b.blink_start > 0.55 + b.blink_hold - 0.25 && p.lid_l < 0.80 + 0.12 * p.breath - 0.005) dipped = true;
        if (was && !b.blinking) {
            overshoots += dipped;
            singles++;
        }
        was = b.blinking;
        prev = p.lid_l;
    }
    qsort(gaps, (size_t)ngaps, sizeof gaps[0], cmp_double);
    double med = ngaps ? gaps[ngaps / 2] : 0, per_min = starts / ((t_end - 3) / 60);
    printf("heavy blinks: %.1f/min, median gap %.1f s, %d closes\n", per_min, med, closes);
    CHECK(per_min > 5 && per_min < 12, "sleepy: %.1f heavy blinks/min, want about 60/7", per_min);
    CHECK(med > 5.5 && med < 9, "sleepy: median gap %.2f s, want about 7", med);
    CHECK(ngaps && gaps[0] >= 1.2 - dt && gaps[ngaps - 1] <= 15 + 0.5, "sleepy: gaps %.2f..%.2f s", gaps[0], gaps[ngaps - 1]);
    CHECK(closes >= starts - 1 && !bad_hold, "closed 0.4..1.2 s: %d of %d blinks off", bad_hold, closes);
    CHECK(singles > 0 && overshoots > singles / 2, "reopens with EASE_OUT_BACK past the lid: %d of %d", overshoots, singles);
}

static int rim_tone_changes(bot_t *b, double t0, double secs, int *tone)
{
    int n = 0;
    for (double t = t0; t < t0 + secs; t += 0.05) {
        bot_pose_t p = bot_pose(b, t);
        int k = bot_rim_tone(p.mood, p.asleep);
        if (k != *tone) n++;
        *tone = k;
    }
    return n;
}

static void test_asleep(void)
{
    bot_t b;
    bot_init(&b, 0xA5, 0);
    bot_pose(&b, BOT_SLEEP_AFTER_S + 0.1);
    double s0 = b.mood_since;
    CHECK(b.mood == BOT_SLEEPY, "sleepy after idle");
    bot_pose_t p = bot_pose(&b, s0 + BOT_ASLEEP_AFTER_S - 0.1);
    CHECK(!p.asleep && !b.asleep, "awake sleepy before 10 min");
    p = bot_pose(&b, s0 + BOT_ASLEEP_AFTER_S + 0.01);
    CHECK(p.asleep && p.mood == BOT_SLEEPY, "asleep after 10 min sleepy");
    double ta = s0 + BOT_ASLEEP_AFTER_S + 1.3;
    p = bot_pose(&b, ta);
    CHECK(p.lid_l == 1 && p.lid_r == 1, "lids closed 1.2 s in: %.3f %.3f", p.lid_l, p.lid_r);
    double gx = p.gaze_x, gy = p.gaze_y;
    CHECK(fabs(gx) < 1e-9 && fabs(gy + 0.35) < 1e-9, "asleep gaze (0, -0.35): %.3f %.3f", gx, gy);
    bool moved = false, opened = false, blinked = false;
    for (double t = ta; t < ta + 3600; t += 1.0 / 30) {
        p = bot_pose(&b, t);
        moved |= p.gaze_x != gx || p.gaze_y != gy;
        opened |= p.lid_l != 1 || p.lid_r != 1;
        blinked |= b.blinking;
    }
    CHECK(!moved && !opened && !blinked && b.asleep, "asleep an hour: no saccade, micro-saccade or blink, lids shut");

    bot_init(&b, 0xA6, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    int tone = bot_rim_tone(BOT_SLEEPY, false);
    CHECK(rim_tone_changes(&b, 0, 2 * 3600, &tone) == 1 && tone == bot_rim_tone(BOT_SLEEPY, true),
          "sleepy for 2 h: the outline re-renders once, at 25 %%");
    CHECK(bot_rim_alpha(bot_rim_tone(BOT_SLEEPY, true)) == 0.25f && bot_rim_alpha(bot_rim_tone(BOT_SLEEPY, false)) == 0.55f &&
              bot_rim_alpha(bot_rim_tone(BOT_IDLE, false)) == 0.55f,
          "ring 25 %% asleep, 55 %% idle or sleepy");
    for (int m = BOT_WORKING; m <= BOT_DONE; m++) {
        CHECK(bot_rim_alpha(bot_rim_tone((bot_mood_t)m, false)) == 1.0f, "mood %d: full ring", m);
        CHECK((int)bot_rim_mood(bot_rim_tone((bot_mood_t)m, true)) == m && (int)bot_rim_mood(bot_rim_tone((bot_mood_t)m, false)) == m,
              "mood %d: the tone keeps the mood", m);
    }
}

static void asleep_bot(bot_t *b, uint64_t seed)
{
    bot_init(b, seed, 0);
    bot_set_mood(b, BOT_SLEEPY, 0);
    bot_pose(b, BOT_ASLEEP_AFTER_S + 5);
}

static void test_wake_from_asleep(void)
{
    const double ta = BOT_ASLEEP_AFTER_S + 5;
    bot_t b;
    asleep_bot(&b, 1);
    CHECK(b.asleep, "asleep");
    CHECK(!bot_set_mood(&b, BOT_IDLE, ta + 1) && b.asleep, "an idle mood keeps it asleep, as it keeps sleepy");
    CHECK(bot_set_mood(&b, BOT_WORKING, ta + 2), "a real mood wakes it");
    bot_pose_t p = bot_pose(&b, ta + 2);
    CHECK(!p.asleep && p.mood == BOT_WORKING, "awake and working");
    double lo = 1;
    for (double t = ta + 2; t < ta + 2.05; t += 1.0 / 120) lo = fmin(lo, bot_pose(&b, t).lid_l);
    CHECK(lo > 0.95, "the waking blink starts shut: no open frame first (%.3f)", lo);
    p = bot_pose(&b, ta + 4);
    CHECK(p.lid_l < 0.05, "eyes open 2 s later: %.3f", p.lid_l);

    asleep_bot(&b, 2);
    bot_wake(&b, ta + 1);
    p = bot_pose(&b, ta + 3);
    CHECK(b.mood == BOT_IDLE && !p.asleep, "bot_wake: idle, awake");

    for (int how = 0; how < 3; how++) {
        asleep_bot(&b, 3 + how);
        double t = ta + 1;
        if (how == 0) bot_look(&b, 0.5, 0.2, t);
        else if (how == 1) bot_push(&b, t);
        else bot_act_begin(&b, t);
        p = bot_pose(&b, t);
        CHECK(!p.asleep && p.mood == BOT_SLEEPY, "input %d: awake sleepy", how);
        if (how == 2) bot_act_end(&b, true, 0.3, t + 1);
        bool reopened = false, moved = false;
        double g0x = p.gaze_x, g0y = p.gaze_y;
        for (double u = t; u < t + 30; u += 1.0 / 60) {
            p = bot_pose(&b, u);
            if (u > t + 1 && !b.blinking && fabs(p.lid_l - (0.80 + 0.12 * p.breath)) < 1e-9) reopened = true;
            moved |= u > t + 3 && (p.gaze_x != g0x || p.gaze_y != g0y);
        }
        CHECK(reopened && moved, "input %d: breathing lid and saccades again (%d %d)", how, reopened, moved);
        p = bot_pose(&b, t + BOT_ASLEEP_AFTER_S - 1);
        CHECK(!p.asleep, "input %d: not asleep again before 10 min", how);
        p = bot_pose(&b, t + BOT_ASLEEP_AFTER_S + 1);
        CHECK(p.asleep, "input %d: asleep again 10 min later", how);
    }

    bot_init(&b, 8, 0);
    bot_sleep(&b, 1);
    p = bot_pose(&b, 1.01);
    CHECK(p.mood == BOT_SLEEPY && p.asleep, "bot_sleep: sleepy and asleep at once");
    p = bot_pose(&b, 2.3);
    CHECK(p.lid_l == 1, "bot_sleep: lids shut within 1.2 s");
    bot_sleep(&b, 3);
    CHECK(b.asleep && b.sleep_lid.start == 1, "bot_sleep again changes nothing");
    bot_stir(&b, 4);
    CHECK(!b.asleep && b.mood == BOT_SLEEPY, "bot_stir: awake sleepy");
    bot_stir(&b, 5);
    CHECK(b.mood == BOT_SLEEPY && !b.asleep, "bot_stir on an awake bot changes nothing");
}

int main(void)
{
    test_micro_saccades();
    test_outline_dy();
    test_badged_rest();
    bot_shape_init();
    test_demo_acts();
    test_chase_hold();
    test_act();
    test_rim_job();
    test_rim_job_order();
    test_orbit_table();
    test_chase_orbit();
    test_label_wipe();
    test_track_smooth();
    test_pose_same();
    test_breath();
    test_breath_eyes();
    test_breath_redraws();
    test_sleepy_lid();
    test_heavy_blinks();
    test_asleep();
    test_wake_from_asleep();
    test_calm();
    test_glint_chase();
    test_ring_glint();
    test_arc_text();
    test_raster_matches_reference();
    test_blink_rate();
    test_mood_swaps_eyes_behind_blink();
    test_happy_eyes_smooth();
    test_sleepy_after_idle();
    test_error_triangle_geometry();
    test_eye_strokes_finite();
    printf(failures ? "%d FAILED\n" : "all bot tests passed\n", failures);
    return failures ? 1 : 0;
}
