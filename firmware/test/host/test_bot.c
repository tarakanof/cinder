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
    double *fields[] = {&p.gaze_x, &p.gaze_y, &p.lid_l, &p.lid_r, &p.triangle, &p.slump, &p.badge,
                        &p.scale_x, &p.scale_y, &p.offset_x, &p.offset_y, &p.orbit};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        p = base;
        *fields[i] += 0.4 * px;
        CHECK(!bot_pose_same(&base, &p, R), "field %zu moved 0.4 px: redraw", i);
    }
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

int main(void)
{
    test_orbit_table();
    test_chase_orbit();
    test_label_wipe();
    test_track_smooth();
    test_pose_same();
    test_glint_chase();
    test_ring_glint();
    test_arc_text();
    test_raster_matches_reference();
    test_blink_rate();
    test_mood_swaps_eyes_behind_blink();
    test_sleepy_after_idle();
    test_error_triangle_geometry();
    test_eye_strokes_finite();
    printf(failures ? "%d FAILED\n" : "all bot tests passed\n", failures);
    return failures ? 1 : 0;
}
