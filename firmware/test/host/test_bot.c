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
#include "rim_beat.h"
#include "rim_slots.h"
#include <pthread.h>
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

static bool stroke_mirrors(const bot_stroke_t *a, const bot_stroke_t *b)
{
    if (a->n != b->n || fabs(a->width - b->width) > 1e-12) return false;
    bool fwd = true, rev = true;
    for (int i = 0; i < a->n; i++) {
        int j = a->n - 1 - i;
        fwd = fwd && fabs(a->x[i] + b->x[i]) < 1e-12 && fabs(a->y[i] - b->y[i]) < 1e-12;
        rev = rev && fabs(a->x[i] + b->x[j]) < 1e-12 && fabs(a->y[i] - b->y[j]) < 1e-12;
    }
    return fwd || rev;
}

static void test_tilt_mirror(void)
{
    const bot_eyes_t eyes[] = {BOT_EYES_DASH, BOT_EYES_ROUND, BOT_EYES_ANGRY, BOT_EYES_HAPPY};
    int cases = 0, bad[4] = {0};
    for (int k = 0; k < 4; k++)
        for (double gx = 0.02; gx <= 1.0; gx += 0.07)
            for (double gy = -0.7; gy <= 0.81; gy += 0.15)
                for (int v = 0; v < 4; v++) {
                    bot_pose_t r = {.eyes = eyes[k], .gaze_x = gx, .gaze_y = gy, .scale_x = 1, .scale_y = 1,
                                    .lid_l = 0.1 * v, .lid_r = 0.05 * v, .triangle = v == 1, .slump = v == 2 ? 0.6 : 0,
                                    .breath = v == 3 ? -0.7 : 0.3, .orbit = v == 3, .asleep = v == 2};
                    bot_pose_t l = r;
                    l.gaze_x = -gx;
                    l.lid_l = r.lid_r;
                    l.lid_r = r.lid_l;
                    bot_stroke_t sr[2], sl[2];
                    bot_eye_strokes(&r, 1.15, sr);
                    bot_eye_strokes(&l, 1.15, sl);
                    cases++;
                    if (!stroke_mirrors(&sl[0], &sr[1]) || !stroke_mirrors(&sl[1], &sr[0])) bad[k]++;
                }
    CHECK(!bad[0] && !bad[1] && !bad[2] && !bad[3],
          "tilt: a left glance must be the exact mirror of a right glance (dash %d, round %d, angry %d, happy %d of %d)",
          bad[0], bad[1], bad[2], bad[3], cases);
    bot_pose_t p = {.eyes = BOT_EYES_DASH, .gaze_x = -0.8, .gaze_y = 0.2, .scale_x = 1, .scale_y = 1};
    bot_stroke_t s[2];
    bot_eye_strokes(&p, 1.15, s);
    double yl = (s[0].y[0] + s[0].y[1]) / 2, yr = (s[1].y[0] + s[1].y[1]) / 2;
    double top_l = s[0].y[0] > s[0].y[1] ? s[0].x[0] : s[0].x[1], bot_l = s[0].y[0] > s[0].y[1] ? s[0].x[1] : s[0].x[0];
    CHECK(yl > yr + 0.05, "tilt: looking left raises the left (leading) eye, %.3f vs %.3f", yl, yr);
    CHECK(top_l > bot_l, "tilt: looking left leans the dash's top to the right (\"/\"), as looking right gives \"\\\"");
}

static void test_lid_shape(void)
{
    const bot_eyes_t eyes[] = {BOT_EYES_DASH, BOT_EYES_ROUND, BOT_EYES_ANGRY};
    const double widen[] = {0.35, 0.15, 0.35};
    int shrunk = 0, dropped = 0, wide = 0;
    for (int k = 0; k < 3; k++)
        for (double gx = -0.8; gx <= 0.81; gx += 0.4) {
            bot_pose_t p = {.eyes = eyes[k], .gaze_x = gx, .gaze_y = 0.3, .scale_x = 1, .scale_y = 1};
            bot_stroke_t o[2];
            bot_eye_strokes(&p, 1.15, o);
            for (double lid = 0.1; lid <= 1.0001; lid += 0.1) {
                p.lid_l = p.lid_r = lid;
                bot_stroke_t s[2];
                bot_eye_strokes(&p, 1.15, s);
                for (int e = 0; e < 2; e++) {
                    double lo = hypot(o[e].x[1] - o[e].x[0], o[e].y[1] - o[e].y[0]) + o[e].width;
                    double ls = hypot(s[e].x[1] - s[e].x[0], s[e].y[1] - s[e].y[0]) + s[e].width;
                    double ldo = fabs(o[e].y[1] - o[e].y[0]) + o[e].width, lds = fabs(s[e].y[1] - s[e].y[0]) + s[e].width;
                    if (ls < lo - 1e-9 || lds < ldo - 1e-9) shrunk++;
                    double cyo = (o[e].y[0] + o[e].y[1]) / 2, cys = (s[e].y[0] + s[e].y[1]) / 2;
                    if (fabs(cyo - cys - BOT_LID_DROOP * lid) > 1e-9) dropped++;
                    if (fabs(s[e].width - o[e].width * (1 + widen[k] * lid)) > 1e-9) wide++;
                }
            }
        }
    CHECK(!shrunk, "lid: %d eyes flattened with the lid; the lid clips them from the top instead (no round-dot frame)", shrunk);
    CHECK(!dropped, "lid: %d eyes not lowered by %.3f x lid (the blink's droop)", dropped, BOT_LID_DROOP);
    CHECK(!wide, "lid: %d eyes not widened as BotNext (dash, angry x(1 + 0.35 lid), round x(1 + 0.15 lid))", wide);
    bot_pose_t h = {.eyes = BOT_EYES_HAPPY, .scale_x = 1, .scale_y = 1, .lid_l = 0.6, .lid_r = 0.6};
    bot_stroke_t hs[2];
    bot_eye_strokes(&h, 1.15, hs);
    CHECK(hs[0].lid == 0 && hs[1].lid == 0, "lid: happy eyes flatten with the lid; only a blink clips them");
    bot_pose_t d = {.eyes = BOT_EYES_DASH, .scale_x = 1, .scale_y = 1, .lid_l = 1.4, .lid_r = -0.2};
    bot_eye_strokes(&d, 1.15, hs);
    CHECK(hs[0].lid == 1 && hs[1].lid == 0, "lid: the clip fraction is the lid in 0..1");
}

static void test_lid_line(void)
{
    const double ys[2] = {100, 160}, w = 30;
    CHECK(bot_lid_line_y(ys, 2, w, 0, BOT_LID_KEEP) == -INFINITY && bot_lid_line_y(ys, 2, w, -0.1, BOT_LID_KEEP) == -INFINITY, "lid line: none when open");
    CHECK(fabs(bot_lid_line_y(ys, 2, w, 1, BOT_LID_KEEP) - (176 - BOT_LID_KEEP * w)) < 1e-9 && fabs(bot_lid_line_y(ys, 2, w, 3, BOT_LID_KEEP) - (176 - BOT_LID_KEEP * w)) < 1e-9,
          "lid line: shut keeps a %.1f x width strip at the bottom", BOT_LID_KEEP);
    CHECK(bot_lid_line_y(ys, 2, w, 1, 0) == 176, "lid line: a shut blink (keep 0) hides the whole eye");
    CHECK(fabs(bot_lid_line_y(ys, 2, w, 0.5, BOT_LID_KEEP) - (84 + (176 - BOT_LID_KEEP * w - 84) / 2)) < 1e-9, "lid line: halfway at lid 0.5");
    double prev = -INFINITY;
    bool down = true;
    for (double l = 0.05; l <= 1; l += 0.05) {
        double y = bot_lid_line_y(ys, 2, w, l, BOT_LID_KEEP);
        down = down && y > prev && y >= 84;
        prev = y;
    }
    CHECK(down, "lid line: comes down from the top as the lid closes");

    static uint8_t a[160 * 120], r[160 * 120];
    static bot_raster_scratch_t sc;
    const float xs[2] = {60.3f, 95.8f}, fy[2] = {30.4f, 85.1f};
    const int segs[1] = {0};
    int bad = 0, cases = 0;
    for (float clip = -20; clip <= 140; clip += 0.37f) {
        memset(a, 0xAA, sizeof a);
        bot_raster_stroke(&sc, NULL, r, 160, 120, 4, 6, xs, fy, 1, segs, 13.5f, 0xF4F4F2, 1.0f);
        bot_raster_stroke_below(&sc, NULL, a, clip, 160, 120, 4, 6, xs, fy, 1, segs, 13.5f, 0xF4F4F2, 1.0f);
        cases++;
        for (int y = 0; y < 120; y++) {
            double top = 6 + y, keep = top + 1 <= clip ? 0 : (top >= clip ? 1 : top + 1 - clip);
            for (int x = 0; x < 160; x++) {
                int want = keep == 1 ? r[y * 160 + x] : (int)(r[y * 160 + x] * keep + 0.5);
                if (abs(a[y * 160 + x] - want) > 1) { bad++; y = 120; break; }
            }
        }
    }
    CHECK(!bad, "lid raster: %d of %d clip lines differ from the full stroke cut at the line", bad, cases);

    int box_bad = 0, trim_bad = 0;
    for (float clip = -30; clip <= 140; clip += 0.37f) {
        int oy = 6, h = 120;
        bot_raster_lid_box(clip, &oy, &h);
        if (oy % 2 || h % 2 || h < 2 || oy + h != 126 || (clip > 6 && clip < 124 && (oy > clip || oy + 2 <= clip)) ||
            (clip <= 6 && (oy != 6 || h != 120)) || (clip >= 124 && h != 2))
            box_bad++;
        static uint8_t full[160 * 120], cut[160 * 120];
        bot_raster_stroke_below(&sc, NULL, full, clip, 160, 120, 4, 6, xs, fy, 1, segs, 13.5f, 0xF4F4F2, 1.0f);
        bot_raster_stroke_below(&sc, NULL, cut, clip, 160, h, 4, oy, xs, fy, 1, segs, 13.5f, 0xF4F4F2, 1.0f);
        for (int y = 0; y < 120; y++) {
            int ky = y + 6 - oy;
            for (int x = 0; x < 160; x++)
                if (full[y * 160 + x] != (ky >= 0 ? cut[ky * 160 + x] : 0)) { trim_bad++; y = 120; break; }
        }
    }
    CHECK(!box_bad, "lid box: %d trims not from the even row at or above the line, bottom kept, h even and >= 2", box_bad);
    CHECK(!trim_bad, "lid box: %d trimmed boxes lose a visible pixel", trim_bad);
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
                        &p.scale_x, &p.scale_y, &p.offset_x, &p.offset_y, &p.hop_dy, &p.orbit, &p.eye_dy};
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
    p = base;
    p.face = BOT_DONE;
    CHECK(!bot_pose_same(&base, &p, R), "the beat (face change): redraw");
    p = base;
    p.lap = true;
    CHECK(!bot_pose_same(&base, &p, R), "a glint lap starting: redraw");
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
    CHECK(count_hops(&b, 73, 1, &popped) == 1 && popped, "quiet off: mood change pops again, done hops once");
    CHECK(bot_anim_allowed(BOT_ANIM_DONE_REWARD, false) && !bot_anim_allowed(BOT_ANIM_DONE_REWARD, true), "calm: no done reward");

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

static void (*s_rj_hook)(void);

void rj_race_point(rim_job_t *j, int at);
void rj_race_point(rim_job_t *j, int at)
{
    if (s_rj_hook) {
        s_rj_hook();
        return;
    }
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
    CHECK(bot_pose(&b, t + 0.11).eyes == BOT_EYES_ROUND, "round once both lids close (lag 20 ms at most)");
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
        if (t >= was.next_saccade_at || (b->mood == BOT_WAITING && t >= was.next_peek_at)) {
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
    for (double t = 0; t < 3; t += dt) bot_pose(&b, t);
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
    for (double t = ta + 2; t < ta + 2.03; t += 1.0 / 240) {
        bot_pose_t q = bot_pose(&b, t);
        lo = fmin(lo, fmin(q.lid_l, q.lid_r));
    }
    CHECK(lo > 0.99, "the waking blink starts shut in both eyes: no open frame first (%.3f)", lo);
    p = bot_pose(&b, ta + 4);
    CHECK(p.lid_l < 0.05, "eyes open 2 s later: %.3f", p.lid_l);

    asleep_bot(&b, 2);
    bot_wake(&b, ta + 1);
    p = bot_pose(&b, ta + 3);
    CHECK(b.mood == BOT_IDLE && !p.asleep, "bot_wake: idle, awake");

    for (int how = 0; how < 4; how++) {
        asleep_bot(&b, 3 + how);
        double t = ta + 1;
        if (how == 0) bot_look(&b, 0.5, 0.2, t);
        else if (how == 1) bot_push(&b, t);
        else if (how == 2) bot_act_begin(&b, t);
        else bot_track(&b, 0.3, 0.3, t);
        p = bot_pose(&b, t);
        CHECK(!p.asleep && p.mood == BOT_SLEEPY, "input %d: awake sleepy", how);
        CHECK(bot_pose(&b, t + 0.05).lid_l > 0.95, "input %d: the lids open over time, not at once", how);
        if (how == 2) bot_act_end(&b, true, 0.3, t + 1);
        if (how == 3) bot_track_end(&b, t + 1);
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
    CHECK(bot_asleep_in(&b, 3.5) == 0, "asleep: nothing left");
    bot_sleep_in(&b, 100, 4);
    CHECK(!b.asleep && b.mood == BOT_SLEEPY && fabs(bot_asleep_in(&b, 4) - 100) < 1e-9, "bot_sleep_in: awake sleepy, asleep in 100 s");
    CHECK(!bot_pose(&b, 103.9).asleep && bot_pose(&b, 104.1).asleep, "asleep when the time left runs out");
    bot_sleep_in(&b, 0, 200);
    CHECK(b.asleep, "bot_sleep_in 0: asleep");
    bot_sleep_in(&b, INFINITY, 210);
    CHECK(!b.asleep && !bot_pose(&b, 210 + 7200).asleep, "bot_sleep_in INFINITY: paused, awake for 2 h");
    bot_act_begin(&b, 7500);
    bot_act_drop(&b, 7600);
    bot_sleep_in(&b, 5, 7600);
    CHECK(!bot_pose(&b, 7604.9).asleep && bot_pose(&b, 7605.1).asleep, "and back to 5 s left");
    bot_set_mood(&b, BOT_WORKING, 300);
    bot_sleep_in(&b, 0, 301);
    CHECK(!b.asleep && b.mood == BOT_WORKING && bot_asleep_in(&b, 301) == 0, "bot_sleep_in leaves other moods");
}

static void test_asleep_guards(void)
{
    bot_t b;
    bot_init(&b, 41, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    bot_act_begin(&b, BOT_ASLEEP_AFTER_S - 2);
    bool slept = false;
    for (double t = BOT_ASLEEP_AFTER_S - 2; t < 700; t += 1.0 / 30) slept |= bot_pose(&b, t).asleep;
    CHECK(!slept, "never falls asleep while acting");
    CHECK(fabs(bot_asleep_in(&b, 700) - 2) < 1e-9, "an act pauses the countdown: 2 s left, got %g", bot_asleep_in(&b, 700));
    bot_act_end(&b, true, 0.3, 700);
    CHECK(!bot_pose(&b, 701.9).asleep && bot_pose(&b, 702.1).asleep, "the act's end resumes it: asleep 2 s later");

    bot_init(&b, 44, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    double asleep_at = -1;
    for (double t = 0; t < 1800 && asleep_at < 0; t += 1.0 / 30) {
        double k = fmod(t, 60);
        if (k < 1.0 / 30) bot_act_begin(&b, t);
        if (fabs(k - 20) < 1.0 / 60) bot_act_drop(&b, t);
        if (bot_pose(&b, t).asleep) asleep_at = t;
    }
    CHECK(asleep_at > 880 && asleep_at < 960, "a 20 s moment every minute: asleep after 10 min awake between them, at %.0f s", asleep_at);
    bot_init(&b, 45, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    bot_act_begin(&b, 100);
    bot_act_drop(&b, 110);
    bot_act_drop(&b, 400);
    CHECK(!bot_pose(&b, 609.9).asleep && bot_pose(&b, 610.1).asleep, "a second end resumes nothing");

    for (int how = 0; how < 2; how++) {
        bot_init(&b, 46, 0);
        bot_set_mood(&b, BOT_SLEEPY, 0);
        bot_act_begin(&b, BOT_ASLEEP_AFTER_S - 1);
        if (how == 0) bot_push(&b, BOT_ASLEEP_AFTER_S + 3);
        else bot_look(&b, 0.5, 0.2, BOT_ASLEEP_AFTER_S + 3);
        CHECK(!b.hopping && b.gaze_to_x == 0, "input %d mid-act: no hop or look", how);
        bot_act_end(&b, true, 0.3, BOT_ASLEEP_AFTER_S + 16);
        CHECK(fabs(bot_asleep_in(&b, BOT_ASLEEP_AFTER_S + 16) - BOT_ASLEEP_AFTER_S) < 1e-9,
              "input %d mid-act: the full 10 min from the act's end (%.1f s left)", how, bot_asleep_in(&b, BOT_ASLEEP_AFTER_S + 16));
    }
    bot_init(&b, 47, 0);
    bot_set_mood(&b, BOT_WORKING, 0);
    bot_act_begin(&b, 10);
    bot_push(&b, 11);
    bot_act_end(&b, true, 0.3, 12);
    CHECK(b.sleep_left < 0 && bot_asleep_in(&b, 12) == 0, "input mid-act outside sleepy: no countdown");

    bot_init(&b, 48, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    bot_act_begin(&b, BOT_ASLEEP_AFTER_S - 5);
    bot_act_begin(&b, BOT_ASLEEP_AFTER_S - 4);
    bot_act_end(&b, true, 0.3, 700);
    CHECK(!bot_pose(&b, 704.9).asleep && bot_pose(&b, 705.1).asleep, "a nested begin keeps the first time left: asleep 5 s after the end");

    bot_init(&b, 49, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    bot_act_begin(&b, BOT_ASLEEP_AFTER_S - 5);
    bot_set_mood(&b, BOT_WORKING, 700);
    bot_set_mood(&b, BOT_SLEEPY, 701);
    bot_act_end(&b, true, 0.3, 702);
    CHECK(fabs(bot_asleep_in(&b, 702) - (BOT_ASLEEP_AFTER_S - 1)) < 1e-9,
          "sleepy again mid-act: its own 10 min, not the old act's 5 s (%.1f s left)", bot_asleep_in(&b, 702));

    bot_init(&b, 50, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    bot_act_begin(&b, BOT_ASLEEP_AFTER_S - 5);
    bot_sleep_in(&b, 30, 700);
    bot_act_end(&b, true, 0.3, 702);
    CHECK(fabs(bot_asleep_in(&b, 702) - 28) < 1e-9, "bot_sleep_in mid-act wins over the paused time left (%.1f s left)", bot_asleep_in(&b, 702));

    bot_init(&b, 42, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    slept = false;
    for (double t = BOT_ASLEEP_AFTER_S - 2; t < 700; t += 1.0 / 30) {
        bot_track(&b, 0.4, 0.1, t);
        slept |= bot_pose(&b, t).asleep;
    }
    CHECK(!slept, "never falls asleep while tracking");
    bot_track_end(&b, 700);
    CHECK(!bot_pose(&b, 700 + BOT_ASLEEP_AFTER_S - 1).asleep && bot_pose(&b, 700 + BOT_ASLEEP_AFTER_S + 1).asleep,
          "asleep 10 min after the chase");

    for (int m = BOT_IDLE; m <= BOT_DONE; m++) {
        if (m == BOT_SLEEPY) continue;
        bot_init(&b, 50 + (uint64_t)m, 0);
        bot_set_sleep_after(&b, 0);
        if (m != BOT_IDLE) bot_set_mood(&b, (bot_mood_t)m, 0);
        slept = false;
        for (double t = 0; t < 1200; t += 0.25) slept |= bot_pose(&b, t).asleep;
        CHECK(!slept, "mood %d: 20 min and never asleep, only sleepy falls asleep", m);
    }

    bot_init(&b, 43, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    for (double t = 0; t <= 10; t += 1.0 / 60) bot_pose(&b, t);
    bot_act_begin(&b, 10);
    bot_act_eyes(&b, BOT_EYES_ROUND, 10.5);
    CHECK(b.blinking && !b.blink_heavy && b.blink_speed == 2.2, "a costume blink in sleepy is not heavy, 2.2x as in 0.9.68");
    bot_pose(&b, 12);
    bot_act_end(&b, true, 0.3, 12);
    CHECK(!b.blinking || !b.blink_heavy, "the costume blink back is not heavy");
}

static void test_sleepy_transitions(void)
{
    int shut = 0, bad = 0, entered = 0, early = 0, jumps = 0;
    double worst = 1, jump_max = 0;
    for (uint64_t seed = 1; seed <= 24; seed++) {
        bot_t b;
        bot_init(&b, seed * 977, 0);
        bot_set_mood(&b, BOT_SLEEPY, 0);
        double t = 3;
        for (; t < 200; t += 1.0 / 240) {
            bot_pose_t p = bot_pose(&b, t);
            if (b.blinking && b.blink_heavy && p.lid_l >= 1 && p.lid_r >= 1 && t - b.blink_start > 0.35) break;
        }
        if (t >= 200) continue;
        shut++;
        bot_set_mood(&b, BOT_WORKING, t);
        bad += b.blink_heavy;
        for (double u = t; u < t + 0.03; u += 1.0 / 240) {
            bot_pose_t p = bot_pose(&b, u);
            worst = fmin(worst, fmin(p.lid_l, p.lid_r));
            bad += p.lid_l < 0.99 || p.lid_r < 0.99;
        }

        bot_init(&b, seed * 31, 0);
        bot_set_mood(&b, BOT_SLEEPY, 5);
        entered++;
        for (double u = 5; u < 8; u += 1.0 / 120) {
            bot_t was = b;
            bot_pose(&b, u);
            early += u > 5.8 && b.blinking && (!was.blinking || b.blink_start != was.blink_start);
        }

        bot_init(&b, seed * 53, 0);
        bot_set_mood(&b, BOT_SLEEPY, 0);
        double prev = -1;
        for (double u = BOT_ASLEEP_AFTER_S - 1; u < BOT_ASLEEP_AFTER_S + 1; u += 1.0 / 60) {
            bot_pose_t p = bot_pose(&b, u);
            if (prev >= 0 && !b.blinking) {
                double d = fabs(p.lid_l - prev);
                jump_max = fmax(jump_max, d);
                jumps += d > 0.03;
            }
            prev = b.blinking ? -1 : p.lid_l;
        }
    }
    CHECK(shut >= 20 && !bad, "a mood change in a heavy blink's hold keeps both eyes shut: %d of %d off, lid %.3f", bad, shut, worst);
    CHECK(entered && !early, "entering sleepy: no blink 0.8-3 s after the mood blink (%d)", early);
    int soon = 0;
    for (uint64_t seed = 1; seed <= 40; seed++) {
        bot_t b;
        bot_init(&b, seed, 0);
        bot_blink(&b, 1);
        bot_set_mood(&b, BOT_SLEEPY, 1.05);
        soon += b.next_blink_at < 1.05 + 3;
    }
    CHECK(!soon, "entering sleepy mid-blink: the next blink is a sleepy one, 3 s or more away (%d early)", soon);
    CHECK(!jumps, "falling asleep: the lid eases from where it is, %d jumps, max %.3f per frame", jumps, jump_max);
    int half = 0, tried = 0;
    for (uint64_t seed = 1; seed <= 12; seed++) {
        for (int late = 0; late < 2; late++) {
            bot_t b;
            bot_init(&b, seed * 7, 0);
            bot_set_mood(&b, BOT_SLEEPY, 0);
            double t = 3, start = -1;
            for (; t < 200; t += 1.0 / 240) {
                bot_pose(&b, t);
                if (b.blinking && b.blink_heavy) { start = b.blink_start; break; }
            }
            if (start < 0) continue;
            double at = start + b.blink_lag + (late ? 0.2 : 0.1);
            bot_pose(&b, at);
            tried++;
            bot_set_mood(&b, BOT_WORKING, at);
            double shut_start = at - 0.075 * b.blink_speed - b.blink_lag;
            half += late ? fabs(b.blink_start - shut_start) < 1e-9 : b.blink_start > shut_start + 1e-6;
        }
    }
    CHECK(tried >= 20 && half == tried, "a heavy blink past half closed (0.64) hands over shut, one at 0.16 part way: %d of %d", half,
          tried);
    int changes = 0, drops = 0;
    double worst_drop = 0;
    for (uint64_t seed = 1; seed <= 24; seed++) {
        for (int k = 0; k < 8; k++) {
            bot_t b;
            bot_init(&b, seed * 13 + (uint64_t)k, 0);
            bot_set_mood(&b, BOT_SLEEPY, 0);
            double t = 0, at = 3 + 2.37 * k + 0.11 * (double)seed;
            for (; t < at; t += 1.0 / 60) bot_pose(&b, t);
            bot_t c = b;
            bot_pose_t was = bot_pose(&c, t);
            bot_set_mood(&b, k % 2 ? BOT_WORKING : BOT_DONE, t);
            changes++;
            for (double u = t; u < t + 0.1; u += 1.0 / 240) {
                bot_pose_t p = bot_pose(&b, u);
                double d = fmax(was.lid_l - p.lid_l, was.lid_r - p.lid_r);
                if (fmin(p.lid_l, p.lid_r) >= 0.999) break;
                if (d > 0.02) { drops++; worst_drop = fmax(worst_drop, d); }
                was = p;
            }
        }
    }
    CHECK(!drops, "leaving sleepy: the mood blink closes from the lid showing, %d of %d changes drop a lid first (worst %.3f)", drops, changes,
          worst_drop);
    bot_t b;
    bot_init(&b, 3, 0);
    bot_sleep(&b, 1);
    CHECK(bot_pose(&b, 1.02).lid_l < 0.1, "bot_sleep from open eyes: the lid closes from open");
}

static void test_sleepy_still_redraws(void)
{
    bot_t b;
    bot_init(&b, 66, 0);
    bot_set_mood(&b, BOT_SLEEPY, 0);
    bot_pose_t last = {0};
    int n = 0, frames = 0;
    bool drawn = false;
    for (double t = 3; t < 300; t += 0.016) {
        bot_t was = b;
        bot_pose_t p = bot_pose(&b, t);
        bool still = !b.blinking && !was.blinking && p.gaze_x == last.gaze_x && p.gaze_y == last.gaze_y && drawn;
        if (!drawn || !bot_pose_same(&p, &last, 195.72)) {
            n += still;
            last = p;
            drawn = true;
        }
        frames += still;
    }
    double rate = n / (frames * 0.016);
    printf("sleepy: %.1f redraws/s while the gaze and blinks are still\n", rate);
    CHECK(rate > 4 && rate < 12, "sleepy holds: %.1f redraws/s, the breathing lid alone", rate);
}

typedef struct {
    int saccades, forward, regress, regress_chance, returns, pages, turns, turns_up, marks, viewers, other;
    int line_steps[7], page_lines[7], lines_seen[6];
    int bad_step, bad_regress, bad_y, bad_x, bad_page, bad_turn, bad_mark, bad_end, bad_hold;
    int turn_shut, page_jitter, covers, cover_open, cover_lead, cover_moved, cover_shape, delayed;
    double fwd_hold, ret_hold, ret_lo, ret_hi, mark_lo, mark_hi, y_min, gap_lo, gap_hi, gap_sum, lead_sum;
    int gaps;
} read_stats_t;

static void read_run(uint64_t seed, double secs, read_stats_t *s)
{
    const double dt = 1.0 / 120, E = 1e-9;
    bot_t b;
    bot_init(&b, seed, 0);
    bot_set_mood(&b, BOT_WORKING, 0);
    double last_turn = -1;
    int page_returns = 0;
    for (double t = dt; t < secs; t += dt) {
        bot_t w = b;
        bot_pose_t p = bot_pose(&b, t);
        s->y_min = fmin(s->y_min, p.gaze_y);
        if (b.next_saccade_at == w.next_saccade_at) continue;
        s->saccades++;
        double x = b.gaze_to_x, y = b.gaze_to_y, hold = b.next_saccade_at - b.gaze_start - b.gaze_dur;
        double line_y = -0.05 - 0.06 * (b.rd_line < 4 ? b.rd_line : 4);
        if (fabs(x) > 0.7 + E || y < -0.35 - E) s->bad_x++;
        bool ret = w.rd_turn || (b.rd_line == w.rd_line + 1 && !b.rd_turn), turn = b.rd_turn && !w.rd_turn;
        double amp = hypot(x - b.gaze_from_x, y - b.gaze_from_y), lead = b.gaze_start - t;
        if ((ret || turn) && amp > 0.6) {
            bot_t c = b;
            bot_pose_t q = bot_pose(&c, b.gaze_start);
            s->covers++;
            s->lead_sum += lead;
            bool open = !b.blinking;
            for (double u = b.gaze_start; u < b.gaze_start + 0.025; u += 0.001) {
                bot_pose_t o = u == b.gaze_start ? q : bot_pose(&c, u);
                open |= fmin(o.lid_l, o.lid_r) < 0.9 - E;
            }
            s->cover_open += open;
            s->cover_lead += lead < 0 || lead > 0.075 * sqrt(0.9) + 0.02 + E;
            s->cover_moved += fabs(q.gaze_x - b.gaze_from_x) > E || fabs(q.gaze_y - b.gaze_from_y) > E || fabs(q.offset_x - p.offset_x) > E ||
                              fabs(q.offset_y - p.offset_y) > E;
            s->cover_shape += fabs(b.gaze_dur - (0.025 + 0.045 * amp)) > E || (b.gaze_ease != 1 && y - 0.071 * (b.gaze_from_y - y) >= -0.35);
        } else {
            s->delayed += lead != 0;
        }
        if (turn) {
            bot_t c = b;
            double shut = 0;
            for (double u = t; u < t + 0.4; u += dt / 4) {
                bot_pose_t q = bot_pose(&c, u);
                shut = fmax(shut, fmin(q.lid_l, q.lid_r));
            }
            s->turn_shut += shut >= 0.9 - E;
        }
        if ((ret || b.rd_turn) && !w.rd_turn && w.rd_steps != 0 && w.read_x < 0.7) s->bad_end++;
        if (w.rd_turn) {
            s->pages++;
            page_returns = 0;
            if (b.rd_turn || b.rd_line != 0 || fabs(y + 0.05) > 0.02 + E || b.rd_lines < 4 || b.rd_lines > 6) s->bad_page++;
            s->page_jitter += y != -0.05;
        } else if (b.rd_turn) {
            s->turns++;
            s->turns_up += y == 0.3;
            if (fabs(x) > 0.08 + E || (y != 0.3 && y != 0.1) || fabs(hold - 1.2) > E) s->bad_turn++;
            if (page_returns + 1 != w.rd_lines) s->bad_turn++;
            if (w.rd_lines >= 0 && w.rd_lines <= 6) s->page_lines[w.rd_lines]++;
            if (last_turn >= 0) {
                double gap = t - last_turn;
                s->gap_lo = fmin(s->gap_lo, gap);
                s->gap_hi = fmax(s->gap_hi, gap);
                s->gap_sum += gap;
                s->gaps++;
            }
            last_turn = t;
        } else if (ret) {
            s->returns++;
            page_returns++;
            if (fabs(y - line_y) > 0.02 + E) s->bad_y++;
        } else if (b.read_x == w.read_x && b.rd_steps == w.rd_steps && y == -0.35) {
            s->marks++;
            s->mark_lo = fmin(s->mark_lo, hold);
            s->mark_hi = fmax(s->mark_hi, hold);
            if (fabs(x) > 0.04 + E || hold < 0.6 - E || hold > 1.0 + E) s->bad_mark++;
            continue;
        } else if (b.read_x == w.read_x && b.rd_steps == w.rd_steps && y == 0.1) {
            s->viewers++;
            continue;
        } else if (b.rd_steps == w.rd_steps - 1) {
            s->forward++;
            s->regress_chance += w.read_x > -0.35;
            double d = b.read_x - w.read_x;
            if (d > 0.38 + E || (d < 0.18 - E && b.read_x != 0.7) || x != b.read_x) s->bad_step++;
            if (fabs(y - line_y) > 0.02 + E) s->bad_y++;
            s->fwd_hold += hold;
            continue;
        } else if (b.read_x < w.read_x && b.rd_steps == w.rd_steps) {
            s->regress++;
            s->regress_chance++;
            double d = w.read_x - b.read_x;
            if (d < 0.10 - E || d > 0.22 + E || w.read_x <= -0.35 || x != b.read_x) s->bad_regress++;
            if (fabs(y - line_y) > 0.02 + E) s->bad_y++;
            continue;
        } else {
            s->other++;
            continue;
        }
        if (b.rd_turn) continue;
        if (b.rd_steps >= 0 && b.rd_steps <= 6) s->line_steps[b.rd_steps]++;
        s->lines_seen[b.rd_line < 5 ? b.rd_line : 5]++;
        if (b.read_x < -0.68 - E || b.read_x > -0.62 + E || x != b.read_x) s->bad_page++;
        s->ret_hold += hold;
        s->ret_lo = fmin(s->ret_lo, hold);
        s->ret_hi = fmax(s->ret_hi, hold);
        if (hold < 1.3 * 0.35 - E || hold > 1.3 * 2 + E) s->bad_hold++;
    }
}

static void test_reading(void)
{
    read_stats_t *s = calloc(1, sizeof *s);
    s->ret_lo = s->mark_lo = s->gap_lo = s->y_min = 1e9;
    s->ret_hi = s->mark_hi = s->gap_hi = -1e9;
    const int seeds = 8;
    const double secs = 600;
    for (int seed = 0; seed < seeds; seed++) read_run(0x7EAD00 + (uint64_t)seed, secs, s);
    int glance_pool = s->saccades - s->pages;
    double rate = s->saccades / (seeds * secs), mark = (double)s->marks / glance_pool, viewer = (double)s->viewers / glance_pool;
    double regress = s->regress_chance ? (double)s->regress / s->regress_chance : 0;
    double ret_mul = s->returns + s->pages && s->forward ? (s->ret_hold / (s->returns + s->pages)) / (s->fwd_hold / s->forward) : 0;
    double gap = s->gaps ? s->gap_sum / s->gaps : 0;
    printf("reading: %d saccades (%.3f/s), %d forward, %d regressions (%.3f), %d returns, %d page turns (%d up) every %.1f s (%.1f-%.1f), "
           "%d marks (%.3f), %d viewer (%.3f), return hold x%.2f, %d jumps over 0.6 behind a blink (lead %.1f ms), y min %.4f\n",
           s->saccades, rate, s->forward, s->regress, regress, s->returns, s->turns, s->turns_up, gap, s->gap_lo, s->gap_hi, s->marks,
           mark, s->viewers, viewer, ret_mul, s->covers, s->covers ? 1000 * s->lead_sum / s->covers : 0, s->y_min);
    CHECK(!s->other, "reading: %d saccades of no reading kind", s->other);
    for (int k = 3; k <= 6; k++) {
        int n = s->line_steps[k], all = s->line_steps[3] + s->line_steps[4] + s->line_steps[5] + s->line_steps[6];
        CHECK(n > all / 4 - all / 12 && n < all / 4 + all / 12, "reading: lines of %d steps %d of %d, want a quarter", k, n, all);
    }
    CHECK(s->line_steps[0] + s->line_steps[1] + s->line_steps[2] == 0, "reading: lines of 3-6 steps only: %d %d %d", s->line_steps[0],
          s->line_steps[1], s->line_steps[2]);
    CHECK(!s->bad_end, "reading: %d lines ended before their steps ran out or the right margin", s->bad_end);
    CHECK(s->forward > 1500 && !s->bad_step, "reading: %d forward steps, %d outside 0.18-0.38", s->forward, s->bad_step);
    CHECK(s->regress > 50 && !s->bad_regress, "reading: %d regressions, %d outside -0.10..-0.22 or from left of -0.35", s->regress, s->bad_regress);
    CHECK(regress > 0.10 && regress < 0.14, "reading: regression chance %.3f per step, want 0.12", regress);
    CHECK(!s->bad_y, "reading: %d targets off their line's y (-0.05 - 0.06 line, +/-0.02)", s->bad_y);
    for (int l = 0; l <= 5; l++) CHECK(s->lines_seen[l] > 0, "reading: line %d never read", l);
    CHECK(!s->bad_page, "reading: %d return sweeps or new pages off the left margin or line 0", s->bad_page);
    CHECK(s->page_jitter > s->pages / 2, "reading: %d of %d new pages jitter their line 0 y", s->page_jitter, s->pages);
    CHECK(ret_mul > 1.2 && ret_mul < 1.4, "reading: the fixation after a return sweep is x%.2f, want x1.3", ret_mul);
    CHECK(!s->bad_hold && s->ret_hi > 2.0, "reading: return holds %.2f-%.2f, %d outside 1.3 x 0.35-2 s", s->ret_lo, s->ret_hi, s->bad_hold);
    CHECK(s->covers > s->returns * 9 / 10 && !s->cover_open, "reading: %d of %d jumps over 0.6 start with a lid under 0.9", s->cover_open,
          s->covers);
    CHECK(!s->cover_lead && !s->cover_moved, "reading: %d jumps start off the lids' 0.9 close, %d move before it", s->cover_lead,
          s->cover_moved);
    CHECK(!s->cover_shape, "reading: %d jumps over 0.6 change their duration or easing", s->cover_shape);
    CHECK(!s->delayed, "reading: %d forward steps, regressions, glances or small sweeps wait for a blink", s->delayed);
    CHECK(s->turns > 100 && !s->bad_turn, "reading: %d page turns, %d off target, hold or line count", s->turns, s->bad_turn);
    CHECK(s->turn_shut == s->turns, "reading: %d of %d page turns close both lids to 0.9", s->turn_shut, s->turns);
    CHECK(s->turns_up > s->turns * 4 / 10 && s->turns_up < s->turns * 6 / 10, "reading: %d of %d page turns look up, want half",
          s->turns_up, s->turns);
    for (int k = 4; k <= 6; k++) {
        int n = s->page_lines[k];
        CHECK(n > s->turns / 3 - s->turns / 10 && n < s->turns / 3 + s->turns / 10, "reading: pages of %d lines %d of %d, want a third", k, n,
              s->turns);
    }
    CHECK(s->page_lines[0] + s->page_lines[1] + s->page_lines[2] + s->page_lines[3] == 0, "reading: pages of 4-6 lines only");
    CHECK(gap > 20 && gap < 40, "reading: a page turn every %.1f s, want 20-40", gap);
    CHECK(s->pages >= s->turns - seeds && s->pages <= s->turns, "reading: %d new pages after %d turns", s->pages, s->turns);
    CHECK(mark > 0.04 && mark < 0.06 && !s->bad_mark, "reading: %.3f of saccades glance at the mark, %d off (0, -0.35) or 0.6-1.0 s",
          mark, s->bad_mark);
    CHECK(s->mark_lo < 0.65 && s->mark_hi > 0.95, "reading: mark holds %.2f-%.2f, want 0.6-1.0", s->mark_lo, s->mark_hi);
    CHECK(viewer > 0.055 && viewer < 0.085, "reading: %.3f of saccades glance at the viewer, want 0.07", viewer);
    CHECK(!s->bad_x, "reading: %d targets outside |x| <= 0.7, y >= -0.35", s->bad_x);
    CHECK(s->y_min >= -0.35 - 1e-9, "reading: the gaze reached y %.4f, below the mark's -0.35", s->y_min);
    CHECK(fabs(rate - 0.982) < 0.982 * 0.08, "reading: %.3f saccades/s, 0.9.69 made 0.982", rate);
    free(s);
}

static void test_reading_redraws(void)
{
    bot_t b;
    double sum = 0;
    for (int seed = 0; seed < 10; seed++) {
        bot_init(&b, 500 + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WORKING, 0);
        sum += redraws_per_s(&b, 2, 600, false);
    }
    printf("reading: working %.2f redraws/s (0.9.69: 14.65, 0.9.70: 13.51)\n", sum / 10);
    CHECK(sum / 10 < 13.51, "reading: working %.2f redraws/s, 0.9.70 made 13.51", sum / 10);
}

static double eye_step_px(const bot_pose_t *a, const bot_pose_t *b)
{
    const double R = 195.72;
    const bot_pose_t *p[2] = {a, b};
    double pts[2][4][2], step = 0;
    for (int j = 0; j < 2; j++) {
        eye_centres(p[j], 1, pts[j]);
        for (int k = 0; k < 4; k++) {
            pts[j][k][0] = (p[j]->offset_x + p[j]->scale_x * pts[j][k][0]) * R;
            pts[j][k][1] = (p[j]->offset_y * 0.45 - (1 - p[j]->scale_y) + p[j]->scale_y * pts[j][k][1]) * R;
        }
    }
    for (int k = 0; k < 4; k++) step = fmax(step, hypot(pts[0][k][0] - pts[1][k][0], pts[0][k][1] - pts[1][k][1]));
    return step;
}

static void test_reading_open_steps(void)
{
    enum { FWD, RET, PAGE, TURN, GLANCE, KINDS };
    static const char *name[KINDS] = {"steps", "return sweeps", "new pages", "page turns", "glances"};
    double n[KINDS] = {0}, all = 0, mins = 0;
    for (int seed = 0; seed < 10; seed++) {
        bot_t b;
        bot_init(&b, 0x0E7E + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WORKING, 0);
        bot_pose_t last = {0};
        bool drawn = false;
        int kind = FWD;
        double end = -1;
        for (double t = 2; t < 600; t += 0.016) {
            bot_t w = b;
            bot_pose_t p = bot_pose(&b, t);
            if (b.next_saccade_at != w.next_saccade_at) {
                bool glance = b.read_x == w.read_x && b.rd_steps == w.rd_steps && b.rd_line == w.rd_line && !b.rd_turn && !w.rd_turn;
                kind = w.rd_turn ? PAGE : b.rd_turn ? TURN : b.rd_line != w.rd_line ? RET : glance ? GLANCE : FWD;
                end = b.gaze_start + b.gaze_dur + 0.016;
            }
            if (drawn && bot_pose_same(&p, &last, 195.72)) continue;
            bot_pose_t moved = p;
            moved.lid_l = last.lid_l;
            moved.lid_r = last.lid_r;
            if (drawn && eye_step_px(&moved, &last) > 8 && fmin(p.lid_l, p.lid_r) < 0.5 && fmin(last.lid_l, last.lid_r) < 0.5) {
                all++;
                if (t <= end) n[kind]++;
            }
            last = p;
            drawn = true;
        }
        mins += 598 / 60.0;
    }
    printf("reading: open-eye steps over 8 px %.1f/min:", all / mins);
    for (int k = 0; k < KINDS; k++) printf(" %s %.2f", name[k], n[k] / mins);
    printf("\n");
    CHECK(n[RET] / mins < 1 && n[PAGE] / mins < 1, "reading: %.2f return sweeps and %.2f new pages a minute jump with open eyes",
          n[RET] / mins, n[PAGE] / mins);
    CHECK(n[TURN] / mins < 1.5, "reading: %.2f page turns a minute jump with open eyes", n[TURN] / mins);
    CHECK(all / mins < 62, "reading: %.1f open-eye steps over 8 px a minute", all / mins);
}

static uint64_t mood_hash(bot_mood_t m, uint64_t seed)
{
    bot_t b;
    bot_init(&b, seed, 0);
    bot_set_sleep_after(&b, 90);
    if (m != BOT_IDLE) bot_set_mood(&b, m, 0.5);
    uint64_t h = 1469598103934665603ULL;
    for (double t = 0.5; t < 150; t += 1.0 / 60) {
        bot_pose_t p = bot_pose(&b, t);
        double v[] = {p.mood, p.eyes, p.gaze_x, p.gaze_y, p.lid_l, p.lid_r, p.triangle, p.slump, p.badge, p.scale_x, p.scale_y,
                      p.offset_x, p.offset_y, p.hop_dy, p.breath, p.asleep};
        for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
            h ^= (uint64_t)(int64_t)llround(v[i] * 1e4);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

static void test_reading_other_moods(void)
{
    static const struct { bot_mood_t m; uint64_t h; } want[] = {
        {BOT_IDLE, 0x3ca4ed9805c3d4e1ULL}, {BOT_SLEEPY, 0x9c3bd55e2cde0a81ULL}, {BOT_WAITING, 0xeaa308eb09d1a937ULL},
        {BOT_ERROR, 0xc6c7e7104bd1fc32ULL}, {BOT_DONE, 0xee25ffabc0490d96ULL},
    };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        uint64_t h = mood_hash(want[i].m, 0x5A5A + i);
        CHECK(h == want[i].h, "mood %d: poses differ from the recorded ones (0.9.69; waiting, error and done 0.9.77) (hash %016llx)", want[i].m, (unsigned long long)h);
    }
    bot_t a;
    bot_init(&a, 9, 0);
    bot_set_mood(&a, BOT_WORKING, 0);
    for (double t = 0; t < 40; t += 1.0 / 60) bot_pose(&a, t);
    bot_set_mood(&a, BOT_DONE, 40);
    bot_set_mood(&a, BOT_WORKING, 41);
    CHECK(a.rd_line == 0 && !a.rd_turn && a.rd_lines >= 4 && a.rd_lines <= 6 && a.read_x >= -0.68 && a.read_x <= 0.7,
          "back to working: a new page (line %d, %d lines, x %.2f)", a.rd_line, a.rd_lines, a.read_x);
}

static void test_reading_moment(void)
{
    const double dt = 1.0 / 120;
    int marks_on = 0, marks_off = 0, low_off = 0, retargets = 0, lowered = 0, on_frames = 0, turns_on = 0, up_on = 0, in_line = 0, jumps = 0;
    double worst_jump = 0;
    double y_on = 1e9, top_on = -1e9;
    for (int seed = 0; seed < 24; seed++) {
        bot_t b;
        bot_init(&b, 0x5B0 + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WORKING, 0);
        double last_y = 0, line_y = NAN;
        bool was_turn = false;
        for (double t = dt; t < 300; t += dt) {
            bot_t w = b;
            double nsa = b.next_saccade_at;
            bool on = t > 5 && fmod(t + seed * 0.37, 9.7) < 4.5;
            if (on != b.rd_moment) {
                bool low = fmin(b.fix_y, last_y) < -0.19 || fmax(b.fix_y, last_y) > 0.15;
                bot_read_moment(&b, on, t);
                if (on && low) {
                    lowered++;
                    retargets += (fabs(b.gaze_start + b.gaze_dur - t) < 1e-9 || (b.gaze_start > t && last_y >= -0.19 && b.fix_y != w.fix_y)) &&
                                 b.fix_y >= -0.19 - 1e-9 && b.fix_y <= 0.15;
                }
            }
            bot_pose_t p = bot_pose(&b, t);
            last_y = p.gaze_y;
            if (on) {
                y_on = fmin(y_on, p.gaze_y);
                top_on = fmax(top_on, p.gaze_y);
                on_frames++;
            }
            bool turn = b.rd_turn && !was_turn;
            was_turn = b.rd_turn;
            if (b.next_saccade_at == nsa) continue;
            bool glance = b.read_x == w.read_x && b.rd_line == w.rd_line && !b.rd_turn && !w.rd_turn &&
                          (b.gaze_to_y == 0.1 || b.gaze_to_y == -0.35);
            if (b.rd_turn || w.rd_turn || b.rd_line != w.rd_line) line_y = NAN;
            if (!glance && !b.rd_turn) {
                if (!isnan(line_y)) {
                    in_line++;
                    double d = fabs(b.gaze_to_y - line_y);
                    worst_jump = fmax(worst_jump, d);
                    jumps += d > 0.04 + 1e-9;
                }
                line_y = b.gaze_to_y;
            }
            if (on && turn) {
                turns_on++;
                up_on += b.gaze_to_y != 0.1;
            }
            bool mark = b.gaze_to_y == -0.35;
            if (on) marks_on += mark;
            else {
                marks_off += mark;
                low_off += b.gaze_to_y < -0.21;
            }
        }
    }
    printf("reading moment: gaze y %.4f..%.4f over %d frames, %d of %d gazes off the band moved at once, %d page turns\n", y_on, top_on, on_frames,
           retargets, lowered, turns_on);
    CHECK(y_on >= -0.19 - 1e-9, "reading moment: the gaze reached y %.4f, below line 2's -0.19", y_on);
    CHECK(top_on <= 0.15, "reading moment: the gaze reached y %.4f, up toward the sprite", top_on);
    CHECK(turns_on > 5 && !up_on, "reading moment: %d of %d page turns look up", up_on, turns_on);
    CHECK(!marks_on && marks_off > 20, "reading moment: %d mark glances while on, %d off", marks_on, marks_off);
    CHECK(low_off > 100, "reading moment: lines 3-4 again once off (%d targets below -0.21)", low_off);
    CHECK(in_line > 1000 && !jumps, "reading moment: %d of %d steps along a line jump y by more than the jitter (worst %.3f)", jumps, in_line,
          worst_jump);
    CHECK(lowered > 10 && retargets == lowered, "reading moment: %d of %d gazes off the band moved at once", retargets, lowered);

    const double fine = 1.0 / 600;
    int clones = 0, risky_micro = 0, risky_sacc = 0, bad = 0;
    double worst = 0;
    for (int seed = 0; seed < 12; seed++) {
        bot_t b;
        bot_init(&b, 0x1F0 + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WORKING, 0);
        for (double t = dt; t < 200; t += dt) {
            bot_pose(&b, t);
            double tn = t + dt / 2, end = b.gaze_start + b.gaze_dur;
            if (b.gaze_start < 0 || tn >= end) continue;
            bot_t off = b, on = b;
            double gy = bot_pose(&off, tn).gaze_y, low = gy;
            for (double u = tn + fine; u < end + fine; u += fine) low = fmin(low, bot_pose(&off, u).gaze_y);
            if (gy >= -0.19 && b.fix_y >= -0.19 && low < -0.19) {
                if (b.gaze_dur > 0.0251) risky_sacc++;
                else risky_micro++;
            }
            bot_read_moment(&on, true, tn);
            clones++;
            for (double u = tn; u < tn + 1.0; u += fine) {
                double y = bot_pose(&on, u).gaze_y;
                if (y < -0.19 - 1e-9 || y > 0.15 + 1e-9) {
                    bad++;
                    worst = fmin(worst, y);
                    break;
                }
            }
        }
    }
    printf("reading moment in flight: %d moves turned on mid-flight, %d micro and %d saccades would leave the band, %d did\n", clones,
           risky_micro, risky_sacc, bad);
    CHECK(risky_micro > 0 && risky_sacc > 0, "reading moment: no in-flight move would leave the band (%d micro, %d saccades)", risky_micro,
          risky_sacc);
    CHECK(!bad, "reading moment: %d of %d moves in flight when it turned on left the band (worst y %.4f)", bad, clones, worst);

    int leads = 0, kept = 0, opened = 0, left = 0;
    for (int seed = 0; seed < 12; seed++) {
        bot_t b;
        bot_init(&b, 0x1E0 + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WORKING, 0);
        for (double t = dt; t < 300; t += dt) {
            bot_pose_t p = bot_pose(&b, t);
            double tn = t + dt / 2;
            if (tn >= b.gaze_start) continue;
            bool band = p.gaze_y >= -0.19 && p.gaze_y <= 0.15 && b.gaze_to_y >= -0.19 && b.gaze_to_y <= 0.15 && b.fix_y >= -0.19;
            bot_t on = b;
            bot_read_moment(&on, true, tn);
            leads++;
            bool moved = false;
            for (double u = tn; u < tn + 0.5; u += fine) {
                bot_pose_t q = bot_pose(&on, u);
                if (q.gaze_y < -0.19 - 1e-9 || q.gaze_y > 0.15 + 1e-9) {
                    left++;
                    break;
                }
                if (band && !moved && hypot(q.gaze_x - p.gaze_x, q.gaze_y - p.gaze_y) > 0.01) {
                    moved = true;
                    kept++;
                    opened += fmin(q.lid_l, q.lid_r) < 0.9 - 1e-9;
                }
            }
        }
    }
    printf("reading moment in a lead: %d jumps waiting for the lids, %d in the band, %d move with a lid under 0.9, %d leave the band\n", leads,
           kept, opened, left);
    CHECK(kept > 100 && !opened, "reading moment: %d of %d jumps in the band move before both lids are at 0.9 when it turns on in the lead",
          opened, kept);
    CHECK(!left, "reading moment: %d jumps in a lead leave the band", left);

    bot_t b;
    bot_init(&b, 3, 0);
    bot_set_mood(&b, BOT_DONE, 0);
    for (double t = 0; t < 3; t += dt) bot_pose(&b, t);
    b.fix_y = -0.5;
    double gs = b.gaze_start;
    bot_read_moment(&b, true, 3);
    CHECK(b.gaze_start == gs, "reading moment: no saccade outside working");
}

static void test_reading_blink_phases(void)
{
    static const double ages[] = {0.5, 0.26, 0.2, 0.15, 0.125, 0.116, 0.112, 0.105, 0.1, 0.085, 0.06, 0.0, -0.03, -0.059};
    static const double lags[] = {0, 0.01, 0.02};
    enum { AGES = sizeof ages / sizeof ages[0] };
    const double dt = 1.0 / 120, fine = 1.0 / 1000, shut_at = 0.075 * sqrt(0.9) + 0.02;
    int jumps = 0, turns = 0, shut = 0, need = 0, covered = 0, open_at = 0, late = 0, pops = 0, cuts = 0, stuck = 0, doubles = 0;
    double worst_pop = 0, worst_cut = 0, worst_lead = 0;
    for (int seed = 0; seed < AGES * 24; seed++) {
        double age = ages[seed % AGES], lag = lags[seed / AGES % 3];
        bool pending = seed / AGES / 3 % 2 && age >= 0, far = seed / AGES / 6 % 2, turn = seed / AGES / 12 % 2;
        bot_t b;
        bot_init(&b, 0x7B00 + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WORKING, 0);
        double t = 0;
        for (; t < 3; t += dt) bot_pose(&b, t);
        b.rd_line = turn ? b.rd_lines - 1 : 0;
        b.rd_steps = 0;
        b.rd_turn = false;
        b.blinking = true;
        b.blink_heavy = false;
        b.double_blink_pending = pending;
        b.blink_speed = 1;
        b.blink_lag = lag;
        b.blink_start = b.last_blink_at = t - age;
        b.next_blink_at = t + 100;
        b.next_saccade_at = t;
        if (far) {
            b.gaze_from_x = b.gaze_to_x = b.fix_x = 0.6;
            b.gaze_from_y = b.gaze_to_y = b.fix_y = turn ? -0.25 : -0.05;
            b.gaze_start = t - 1;
        }
        bot_t w = b;
        bot_pose_t was = bot_pose(&w, t - fine);
        bot_pose(&b, t);
        if (turn ? !b.rd_turn : b.rd_line != 1) continue;
        jumps++;
        turns += turn;
        double amp = hypot(b.gaze_to_x - b.gaze_from_x, b.gaze_to_y - b.gaze_from_y), lead = b.gaze_start - t, most = 0, low = 1;
        bool cover = amp > 0.6, reopened = false, again = false;
        if (cover) {
            covered++;
            worst_lead = fmax(worst_lead, lead);
            late += lead < 0 || lead > shut_at + 1e-9;
            bot_t c = b;
            bool open = false;
            for (double u = b.gaze_start; u < b.gaze_start + 0.025; u += fine) {
                bot_pose_t q = bot_pose(&c, u);
                open |= fmin(q.lid_l, q.lid_r) < 0.9 - 1e-9;
            }
            open_at += open;
        }
        for (double u = t; u < t + 0.8; u += fine) {
            bot_pose_t q = bot_pose(&b, u);
            double l[2] = {q.lid_l, q.lid_r}, k[2] = {was.lid_l, was.lid_r};
            for (int e = 0; e < 2; e++) {
                double d = fabs(l[e] - k[e]);
                if (d > 0.06) { pops++; worst_pop = fmax(worst_pop, d); }
                if (l[e] == 0 && k[e] > 0.002) { cuts++; worst_cut = fmax(worst_cut, k[e]); }
            }
            most = fmax(most, fmin(q.lid_l, q.lid_r));
            if (most >= 0.9) low = fmin(low, fmax(q.lid_l, q.lid_r));
            if (low <= 0.01) reopened = true;
            if (reopened && fmax(q.lid_l, q.lid_r) > 0.1) again = true;
            was = q;
        }
        if (turn || cover) {
            need++;
            shut += most >= 0.9 - 1e-9;
            doubles += again;

        }
        stuck += b.blinking || was.lid_l != 0 || was.lid_r != 0;
    }
    printf("reading: %d jumps (%d page turns) on a blink of every phase, %d of %d close both lids to 0.9, %d over 0.6 start shut, lead up to %.1f ms\n",
           jumps, turns, shut, need, covered - open_at, 1000 * worst_lead);
    CHECK(turns > 100 && jumps - turns > 100 && shut == need, "reading: %d of %d page turns and covered jumps close both lids to 0.9 whatever blink was showing",
          shut, need);
    CHECK(covered > 150 && !open_at, "reading: %d of %d jumps over 0.6 open a lid under 0.9 within 25 ms of their start", open_at, covered);
    CHECK(!late, "reading: %d jumps over 0.6 wait outside 0-%.0f ms (worst %.1f ms)", late, 1000 * shut_at, 1000 * worst_lead);
    CHECK(!doubles, "reading: %d covering blinks blink again", doubles);
    CHECK(!pops && !cuts, "reading: the blink jumps a lid %d times (worst %.3f), snaps it open %d times (worst %.4f)", pops, worst_pop, cuts,
          worst_cut);
    CHECK(!stuck, "reading: %d jumps leave a blink unfinished", stuck);
}

static void test_reading_moment_glide(void)
{
    const double fine = 1.0 / 1000;
    int n = 0, jumps = 0, out = 0;
    double worst = 0;
    for (int seed = 0; seed < 20; seed++) {
        for (double on = 0.05; on < 0.36; on += 0.1) {
            bot_t b;
            bot_init(&b, 0x61D + (uint64_t)seed, 0);
            bot_look(&b, 0.5, 0.0, 0.5);
            double t = 0.5;
            for (; t < 1.5; t += fine) bot_pose(&b, t);
            bot_set_mood(&b, BOT_WORKING, t);
            if (b.gaze_ease != 0 || b.gaze_to_y < -0.19 || b.gaze_to_y > 0.15) continue;
            n++;
            double prev_x = NAN, prev_y = NAN, t_on = t + on, end = b.gaze_start + b.gaze_dur;
            for (; t < end + 0.05; t += fine) {
                if (!b.rd_moment && t >= t_on) bot_read_moment(&b, true, t);
                bot_pose_t p = bot_pose(&b, t);
                if (!isnan(prev_x)) {
                    double d = hypot(p.gaze_x - prev_x, p.gaze_y - prev_y);
                    worst = fmax(worst, d);
                    jumps += d > 0.02;
                }
                out += b.rd_moment && (p.gaze_y < -0.19 - 1e-9 || p.gaze_y > 0.15 + 1e-9);
                prev_x = p.gaze_x;
                prev_y = p.gaze_y;
            }
        }
    }
    printf("reading moment: on mid entry glide, %d cases, largest gaze step %.4f per ms\n", n, worst);
    CHECK(n >= 60 && !jumps && !out, "a quiet moment on mid entry glide: the glide goes on (%d jumps over 0.02 per ms, worst %.3f, %d out of band, %d cases)",
          jumps, worst, out, n);
}

static void test_coupled_blink_expired(void)
{
    int big = 0, coupled = 0, stale = 0;
    for (int seed = 0; seed < 120; seed++) {
        static const bot_mood_t moods[] = {BOT_SLEEPY, BOT_IDLE, BOT_ERROR};
        bot_mood_t m = moods[seed % 3];
        bot_t b;
        bot_init(&b, 0xC0B1 + (uint64_t)seed, 0);
        if (m != BOT_IDLE) bot_set_mood(&b, m, 0);
        double t = 0;
        for (; t < 4; t += 1.0 / 60) bot_pose(&b, t);
        b.blinking = true;
        b.blink_heavy = m == BOT_SLEEPY;
        b.blink_speed = 1;
        b.double_blink_pending = false;
        b.blink_hold = 0.5;
        b.blink_lag = 0;
        b.blink_start = b.last_blink_at = t - 1.3;
        b.next_blink_at = t + 100;
        b.gaze_from_x = b.gaze_to_x = b.fix_x = 0;
        b.gaze_from_y = b.gaze_to_y = b.fix_y = 0.9;
        b.gaze_start = t - 2;
        b.next_saccade_at = t;
        bot_pose(&b, t);
        if (hypot(b.gaze_to_x - b.gaze_from_x, b.gaze_to_y - b.gaze_from_y) <= 0.8) continue;
        big++;
        coupled += b.last_blink_at == t;
        stale += b.blinking && b.blink_start < t;
    }
    printf("coupled blink: %d of %d big saccades (sleepy, idle, error) blink on the frame an expired blink is still flagged\n", coupled, big);
    CHECK(big >= 80 && coupled >= big * 4 / 10 && coupled <= big * 8 / 10 && !stale,
          "a blink over before a big saccade's frame: the saccade's 60 %% coupled blink still draws (%d of %d, %d stale)", coupled, big,
          stale);
}

static void test_reading_entry(void)
{
    int n = 0, bad = 0, glances = 0;
    double lo = 1e9, hi = 0;
    for (int seed = 0; seed < 300; seed++) {
        bot_t b;
        bot_init(&b, 0xE17 + (uint64_t)seed, 0);
        bot_pose(&b, 0.5);
        bot_set_mood(&b, BOT_WORKING, 1);
        double x = b.gaze_to_x, y = b.gaze_to_y, hold = b.next_saccade_at - b.gaze_start - b.gaze_dur;
        n++;
        glances += y == 0.1 || y == -0.35;
        if (b.rd_turn || b.rd_line != 0 || x < -0.68 - 1e-9 || x > -0.62 + 1e-9 || x != b.read_x || fabs(y + 0.05) > 0.02 + 1e-9) bad++;
        lo = fmin(lo, hold);
        hi = fmax(hi, hold);
    }
    CHECK(!bad && !glances, "reading: entering working, %d of %d first targets off line 0's start, %d glances", bad, n, glances);
    CHECK(lo >= 1.3 * 0.35 - 1e-9 && hi <= 1.3 * 2 + 1e-9 && hi > 2.0, "reading: entering working holds %.2f-%.2f s, want 1.3 x 0.35-2", lo, hi);
}

enum { HOP_PLAIN, HOP_BURST, HOP_REMIND };
typedef struct { double t, h, len; int kind; double gap; } wait_hop_t;
typedef struct { double t, x, y, amp, lead; bool shut, dbl; } wait_peek_t;
typedef struct {
    wait_hop_t hop[400];
    wait_peek_t peek[200];
    double nudge[100];
    int hops, peeks, nudges, snaps, overlap, outline[4];
    double outline_s[4];
} wait_log_t;

static int outline_key(const bot_pose_t *p)
{
    double ratio = p->scale_y / p->scale_x;
    int v = 0;
    if (fabs(ratio - 1) >= 0.015) {
        double best = fabs(ratio - 1);
        for (int k = -BOT_RIM_STEPS; k <= BOT_RIM_STEPS; k++) {
            double sx, sy;
            bot_rim_variant(k, &sx, &sy);
            if (fabs(ratio - sy / sx) < best) { best = fabs(ratio - sy / sx); v = k; }
        }
    }
    return v * 1000 + bot_rim_dy(p, 196, 0.45);
}

static void wait_run(bot_t *b, double t0, double secs, double dt, wait_log_t *w)
{
    memset(w, 0, sizeof *w);
    int key = 1 << 30, prev = 0;
    for (double t = t0; t < t0 + secs; t += dt) {
        bot_t was = *b;
        bot_pose_t p = bot_pose(b, t);
        int ph = b->w_phase;
        int k = outline_key(&p);
        if (k != key && t > t0) w->outline[ph]++;
        w->outline_s[ph] += dt;
        prev = key;
        key = k;
        double nu = t - b->nudge_at;
        w->overlap += nu >= 0 && nu < BOT_NUDGE_S && b->hopping;
        if (b->hop_start != was.hop_start && b->hopping && w->hops < 400) {
            wait_hop_t *h = &w->hop[w->hops++];
            h->t = t;
            h->h = b->hop_h;
            h->len = b->hop_len;
            h->kind = t >= was.burst_hop_at ? HOP_BURST : t >= was.next_rem_at ? HOP_REMIND : HOP_PLAIN;
            h->gap = t - was.next_hop_at;
            w->snaps += t > t0 && prev != 0;
        }
        if (b->gaze_start != was.gaze_start && t >= was.next_peek_at && w->peeks < 200) {
            wait_peek_t *k2 = &w->peek[w->peeks++];
            k2->t = t;
            k2->x = b->gaze_to_x;
            k2->y = b->gaze_to_y;
            k2->amp = hypot(b->gaze_to_x - b->gaze_from_x, b->gaze_to_y - b->gaze_from_y);
            k2->lead = b->gaze_start - t;
            k2->dbl = b->blinking && b->double_blink_pending;
            bot_t c = *b;
            bot_pose_t q = bot_pose(&c, b->gaze_start + 1e-6);
            k2->shut = q.lid_l >= 0.9 - 1e-9 && q.lid_r >= 0.9 - 1e-9;
        }
        if (b->nudge_at != was.nudge_at && w->nudges < 100) w->nudge[w->nudges++] = t;
    }
}

static int cmp_dbl(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void test_wait_phases(void)
{
    static wait_log_t w;
    const double dt = 1.0 / 120;
    int plain_a = 0, dbl_a = 0, bad = 0, peeks = 0, covered = 0, nudges = 0;
    double gaps_a[2000], gaps_b[400], ngap[400];
    int na = 0, nb = 0, nn = 0;
    double hlo = 9, hhi = 0, llo = 9, lhi = 0, out_s[4] = {0}, out_n[4] = {0};
    for (int seed = 0; seed < 12; seed++) {
        bot_t b;
        bot_init(&b, 0x3A17 + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WAITING, 1);
        wait_run(&b, 1, 620, dt, &w);
        CHECK(!w.snaps && !w.overlap, "seed %d: %d hops started off the base outline, %d frames nudge during a hop", seed, w.snaps,
              w.overlap);
        double prev = -1;
        for (int i = 0; i < w.hops; i++) {
            const wait_hop_t *h = &w.hop[i];
            double el = h->t - 1;
            if (h->kind == HOP_PLAIN) {
                hlo = fmin(hlo, h->h); hhi = fmax(hhi, h->h);
                llo = fmin(llo, h->len); lhi = fmax(lhi, h->len);
                if (i == 0) bad += fabs(el - 0.6) > dt;
                bad += h->gap > 0.21;
                if (el >= BOT_WAIT_C_S) bad++;
                if (prev >= 0 && el < BOT_WAIT_B_S) gaps_a[na++] = h->t - prev;
                if (prev >= 0 && prev - 1 >= BOT_WAIT_B_S && na + nb < 2400) gaps_b[nb++] = h->t - prev;
                if (el < BOT_WAIT_B_S) plain_a++;
                prev = h->t;
            } else if (h->kind == HOP_BURST) {
                const wait_hop_t *f = &w.hop[i - 1];
                bool first = f->kind == HOP_PLAIN;
                if (first) {
                    dbl_a++;
                    bad += f->t - 1 >= BOT_WAIT_B_S || h->t - f->t < f->len || h->t - f->t > f->len + 2 * dt || h->h != 0.6;
                } else {
                    bad += f->kind != HOP_REMIND || h->t - f->t < fmax(1.05, f->len) || h->t - f->t > fmax(1.05, f->len) + 2 * dt || h->h != 1;
                }
                bad += h->len < 0.9 || h->len > 1.1;
            } else {
                double r = fmod(el, BOT_WAIT_REMIND_S);
                bad += el < BOT_WAIT_REMIND_S - dt || (r > BOT_NUDGE_S + dt && r < BOT_WAIT_REMIND_S - dt) || h->h != 1;
            }
        }
        int rem = 0;
        for (int i = 0; i < w.hops; i++) rem += w.hop[i].kind == HOP_REMIND;
        CHECK(rem == 2, "seed %d: %d reminders in 620 s, want 2 (300 s and 600 s)", seed, rem);
        double lastp = -1;
        for (int i = 0; i < w.peeks; i++) {
            const wait_peek_t *k = &w.peek[i];
            double el = k->t - 1;
            peeks++;
            bad += el < BOT_WAIT_B_S + 1.5 - dt || el >= BOT_WAIT_C_S;
            if (i == 0) bad += el > BOT_WAIT_B_S + 3 + dt;
            if (lastp >= 0) bad += k->t - lastp < 4 - dt || k->t - lastp > 6 + dt;
            lastp = k->t;
            bad += fabs(k->x) > 0.05 + 1e-9 || fabs(k->y - 0.1) > 0.05 + 1e-9 || !k->dbl;
            if (k->amp > 0.6) {
                covered++;
                bad += !k->shut || k->lead < 0 || k->lead > 0.092;
            } else {
                bad += k->lead != 0;
            }
        }
        double lastn = -1;
        for (int i = 0; i < w.nudges; i++) {
            double el = w.nudge[i] - 1;
            nudges++;
            bad += el < BOT_WAIT_C_S + 1 - dt;
            if (i == 0) bad += el > BOT_WAIT_C_S + 3 + dt;
            if (lastn >= 0) ngap[nn++] = w.nudge[i] - lastn;
            lastn = w.nudge[i];
        }
        for (int ph = 1; ph <= 3; ph++) { out_n[ph] += w.outline[ph]; out_s[ph] += w.outline_s[ph]; }
    }
    for (int seed = 0; seed < 150; seed++) {
        bot_t b;
        bot_init(&b, 0x3B00 + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WAITING, 1);
        wait_run(&b, 1, BOT_WAIT_B_S, dt, &w);
        double prev = -1;
        for (int i = 0; i < w.hops; i++) {
            const wait_hop_t *h = &w.hop[i];
            if (h->kind == HOP_PLAIN) {
                plain_a++;
                if (prev >= 0 && na < 2000) gaps_a[na++] = h->t - prev;
                prev = h->t;
            } else {
                dbl_a += h->kind == HOP_BURST;
            }
        }
    }
    CHECK(!bad, "waiting phases: %d events off schedule", bad);
    qsort(gaps_a, (size_t)na, sizeof gaps_a[0], cmp_dbl);
    qsort(gaps_b, (size_t)nb, sizeof gaps_b[0], cmp_dbl);
    qsort(ngap, (size_t)nn, sizeof ngap[0], cmp_dbl);
    CHECK(na > 600 && gaps_a[0] >= 2 - dt && gaps_a[na - 1] <= 4.5 + 0.21 && fabs(gaps_a[na / 2] - 2.8) < 0.15,
          "phase A: %d gaps %.2f-%.2f s, median %.2f, want 2-4.5, about 2.8", na, gaps_a[0], gaps_a[na - 1], gaps_a[na / 2]);
    CHECK(nb > 100 && gaps_b[0] >= 5 - dt && gaps_b[nb - 1] <= 14 + 0.21 && fabs(gaps_b[nb / 2] - 8) < 0.8,
          "phase B: %d gaps %.2f-%.2f s, median %.2f, want 5-14, about 8", nb, gaps_b[0], gaps_b[nb - 1], gaps_b[nb / 2]);
    double share = (double)dbl_a / plain_a;
    CHECK(share > 0.21 && share < 0.29, "phase A: %d double hops of %d, %.2f, want about 0.25", dbl_a, plain_a, share);
    CHECK(hlo >= 0.8 && hhi <= 1.15 && hhi - hlo > 0.3 && llo >= 0.9 && lhi <= 1.1 && lhi - llo > 0.17,
          "hop jitter: height %.3f-%.3f, length %.3f-%.3f", hlo, hhi, llo, lhi);
    CHECK(peeks > 12 * 25 && covered > 0, "phase B: %d peeks, %d covered", peeks, covered);
    CHECK(nn > 100 && ngap[0] >= 12 - dt && ngap[nn - 1] <= 20 + 2.5 && nudges > 12 * 20, "phase C: %d nudges, gaps %.2f-%.2f s", nudges,
          ngap[0], ngap[nn - 1]);
    double a = out_n[1] / out_s[1] * 60, bm = out_n[2] / out_s[2] * 60, c = out_n[3] / out_s[3] * 60;
    double all = (out_n[1] + out_n[2] + out_n[3]) / (out_s[1] + out_s[2] + out_s[3]) * 60;
    printf("waiting: hop gaps A median %.2f s (%d), B %.2f s (%d), %.0f %% double; %d peeks (%d covered), %d nudges %.1f-%.1f s apart; "
           "outline moves/min A %.0f, B %.0f, C %.1f, 0-620 s %.1f\n",
           gaps_a[na / 2], na, gaps_b[nb / 2], nb, 100 * share, peeks, covered, nudges, ngap[0], ngap[nn - 1], a, bm, c, all);
    CHECK(c < a / 20 && bm < a / 2 && all < 200, "outline moves/min: A %.0f, B %.0f, C %.1f, all %.1f", a, bm, c, all);
}

static void test_wait_nudge(void)
{
    bot_t b;
    bot_init(&b, 0x4E, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    double t = 0, dt = 1.0 / 1000;
    for (; t < BOT_WAIT_C_S + 30 && b.nudge_at < 0; t += 1.0 / 60) bot_pose(&b, t);
    CHECK(b.nudge_at >= BOT_WAIT_C_S + 1 && b.nudge_at <= BOT_WAIT_C_S + 3 + 0.02, "first nudge at %.2f s", b.nudge_at);
    double n0 = b.nudge_at, m2x, m2y, p1x, p1y, peak = 0, peak_at = 0;
    bot_rim_variant(-2, &m2x, &m2y);
    bot_rim_variant(1, &p1x, &p1y);
    int bad = 0, keys[3] = {0};
    for (t = n0; t < n0 + 0.5; t += dt) {
        bot_pose_t p = bot_pose(&b, t);
        double u = t - n0;
        int k = u < 0.1 ? 0 : u < 0.2 ? 1 : 2;
        double sx = k == 0 ? m2x : k == 1 ? p1x : 1, sy = k == 0 ? m2y : k == 1 ? p1y : 1;
        bad += fabs(p.scale_x - sx) > 1e-12 || fabs(p.scale_y - sy) > 1e-12 || p.hop_dy != 0 || bot_rim_dy(&p, 196, 0.45) != 0;
        keys[k] = outline_key(&p);
        if (p.badge > peak) { peak = p.badge; peak_at = u; }
        if (u >= BOT_NUDGE_S) bad += p.badge != 1;
    }
    CHECK(!bad, "nudge: %d frames off the -2, +1, base sequence or the badge", bad);
    CHECK(keys[0] == -2000 && keys[1] == 1000 && keys[2] == 0, "nudge: outline variants %d, %d, %d", keys[0], keys[1], keys[2]);
    CHECK(fabs(peak - 1.18) < 1e-3 && fabs(peak_at - BOT_NUDGE_S / 2) < 0.002, "badge pulse peaks at %.4f at %.3f s", peak, peak_at);
    CHECK(fabs(m2x - 1.03) < 1e-12 && fabs(m2y - 0.958) < 1e-12 && fabs(p1x - 0.9895) < 1e-12 && fabs(p1y - 1.015) < 1e-12,
          "variant -2 %.4f x %.4f, +1 %.4f x %.4f", m2x, m2y, p1x, p1y);

    double n1 = b.next_nudge_at;
    for (t = n0 + 0.5; t < n1 + 0.02; t += 1.0 / 60) bot_pose(&b, t);
    CHECK(b.nudge_at > n0, "a second nudge");
    double nt = b.nudge_at;
    bot_set_mood(&b, BOT_DONE, nt + 0.05);
    bot_pose_t q = bot_pose(&b, nt + 0.06);
    CHECK(fabs(q.scale_y / q.scale_x - 1) < 1e-9, "a mood change ends the nudge (ratio %.4f)", q.scale_y / q.scale_x);
}

static void wait_to(bot_t *b, double *t, double until)
{
    for (; *t < until; *t += 1.0 / 60) bot_pose(b, *t);
}

static void test_wait_restart(void)
{
    static wait_log_t w;
    bot_t b;
    bot_init(&b, 0x5E, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    double t = 0;
    wait_to(&b, &t, 200);
    CHECK(b.w_phase == 3, "phase C at 200 s, got %d", b.w_phase);
    bot_wait_restart(&b, t, true);
    CHECK(b.w_t0 == 0 && b.w_phase == 3, "phase C before a reminder period: no restart yet");
    wait_run(&b, t, 125, 1.0 / 120, &w);
    bool remind = false;
    for (int i = 0; i < w.hops; i++) remind |= w.hop[i].kind == HOP_REMIND;
    CHECK(b.w_t0 >= BOT_WAIT_REMIND_S && b.w_t0 < BOT_WAIT_REMIND_S + 0.01 && w.hops >= 5 && fabs(w.hop[0].t - b.w_t0 - 0.6) < 0.01 &&
              !remind && b.w_phase == 2,
          "a restart asked at 200 s waits for the gate: phase A from %.2f s, first hop %.2f s after, no reminder", b.w_t0,
          w.hops ? w.hop[0].t - b.w_t0 : -1);

    for (int how = 0; how < 2; how++) {
        bot_init(&b, 0x5E, 0);
        bot_set_mood(&b, BOT_WAITING, 0);
        t = 0;
        if (how) bot_set_calm(&b, true, 0);
        wait_to(&b, &t, 200);
        if (how) bot_set_calm(&b, false, t);
        else bot_wait_restart(&b, t, false);
        wait_run(&b, t, 105, 1.0 / 120, &w);
        int rem = 0;
        for (int i = 0; i < w.hops; i++) rem += w.hop[i].kind == HOP_REMIND;
        CHECK(b.w_t0 == 0 && b.w_phase == 3 && rem == 1 && !b.w_pending,
              "%s at 200 s, no latch: dropped, the 300 s reminder comes (%d) and w_t0 stays %.2f", how ? "calm off" : "the page shown", rem, b.w_t0);
    }

    bot_init(&b, 0x5E, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    t = 0;
    wait_to(&b, &t, 320);
    bot_wait_restart(&b, t, true);
    wait_run(&b, t, 25, 1.0 / 120, &w);
    CHECK(w.hops >= 5 && fabs(w.hop[0].t - t - 0.6) < 0.01 && w.nudges == 0 && b.w_phase == 2,
          "restart: phase A again, first hop %.2f s after, %d hops, %d nudges", w.hops ? w.hop[0].t - t : -1, w.hops, w.nudges);

    bot_init(&b, 0x65, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    t = 0;
    wait_to(&b, &t, 10);
    double w0 = b.w_t0;
    bot_wait_restart(&b, t, true);
    wait_to(&b, &t, 100);
    bot_wait_restart(&b, t, true);
    CHECK(b.w_t0 == w0 && b.w_phase == 2, "phases A and B: no restart yet");
    wait_to(&b, &t, BOT_WAIT_REMIND_S + 1);
    CHECK(b.w_t0 >= BOT_WAIT_REMIND_S && b.w_t0 < BOT_WAIT_REMIND_S + 0.02 && b.w_phase == 1 && !b.w_pending,
          "asked twice in A and B: one restart at the gate (%.2f s)", b.w_t0);
    wait_to(&b, &t, 2 * BOT_WAIT_REMIND_S + 2);
    CHECK(b.w_t0 < BOT_WAIT_REMIND_S + 0.02 && b.w_phase == 3, "and none after it, unasked");

    bot_init(&b, 0x65, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    t = 0;
    wait_to(&b, &t, BOT_WAIT_REMIND_S - 1);
    double h0 = b.hop_start;
    for (; b.hop_start == h0; t += 1.0 / 120) bot_pose(&b, t);
    bot_wait_restart(&b, b.hop_start + 0.05, true);
    double first = b.hop_start, end = first + b.hop_len;
    wait_run(&b, t, 3, 1.0 / 120, &w);
    CHECK(!w.snaps && w.hops >= 1 && w.hop[0].t >= end - 1e-9 && w.hop[0].t < end + 0.02 && b.burst_hop_at == INFINITY,
          "a restart mid-reminder: the reminder's second hop dropped, the next hop after its end (%.3f s, ends %.3f s)",
          w.hops ? w.hop[0].t - first : -1, end - first);

    int cases = 0, kept = 0;
    for (int seed = 0; seed < 20; seed++) {
        bot_init(&b, 0x70 + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WAITING, 0);
        for (t = 0; t < 15 && !(b.next_hop_at - t > 0.05 && b.next_hop_at - t < 0.5 && !b.hopping); t += 1.0 / 120) bot_pose(&b, t);
        if (t >= 15) continue;
        cases++;
        double due = b.next_hop_at;
        bot_wait_scene(&b, 1, t);
        for (double h = b.hop_start; b.hop_start == h && t < due + 1; t += 1.0 / 120) bot_pose(&b, t);
        kept += b.next_hop_at != due && b.hop_start >= due && b.hop_start < due + 0.01;
    }
    CHECK(cases >= 10 && kept == cases, "a restart in phase A keeps a hop due sooner than 0.6 s (%d of %d)", kept, cases);

    static const double every[] = {0.5, 2.5, 7.5, 60};
    for (size_t i = 0; i < sizeof every / sizeof every[0]; i++) {
        int hops = 0, c_seen = 0;
        for (int seed = 0; seed < 4; seed++) {
            bot_init(&b, 0x66 + (uint64_t)seed, 0);
            bot_set_mood(&b, BOT_WAITING, 0);
            double next = every[i];
            for (t = 0; t < 1200; t += 1.0 / 60) {
                bot_t was = b;
                bot_pose(&b, t);
                hops += b.hop_start != was.hop_start && b.hopping;
                c_seen += b.w_phase == 3 && was.w_phase != 3;
                if (t >= next) {
                    bot_wait_restart(&b, t, true);
                    next += every[i];
                }
            }
        }
        double rate = hops / (4 * 1200.0);
        printf("waiting with a restart every %.1f s: %.3f hops/s (0.9.72 about 0.22), phase C reached %d times in 4 x 20 min\n", every[i], rate,
               c_seen);
        CHECK(rate < 0.12 && c_seen >= 4 * 3, "restart every %.1f s: %.3f hops/s, phase C %d times", every[i], rate, c_seen);
    }

    bot_t o;
    bot_init(&o, 0x5F, 0);
    bot_set_mood(&o, BOT_ERROR, 0);
    for (t = 0; t < 30; t += 1.0 / 60) bot_pose(&o, t);
    bot_t c = o;
    bot_wait_restart(&o, t, true);
    CHECK(memcmp(&c, &o, sizeof c) == 0, "restart outside waiting changes nothing");

    bot_init(&b, 0x60, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    t = 0;
    wait_to(&b, &t, 200);
    bot_set_mood(&b, BOT_DONE, t);
    wait_to(&b, &t, 205);
    bot_set_mood(&b, BOT_WAITING, t);
    double t1 = t;
    wait_to(&b, &t, t1 + 1);
    CHECK(b.w_phase == 1 && b.w_t0 == t1, "a new waiting mood starts at phase A");

    bot_init(&b, 0x61, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    t = 0;
    wait_to(&b, &t, 200);
    bot_set_calm(&b, true, t);
    wait_run(&b, t, 400, 1.0 / 60, &w);
    CHECK(w.hops == 0 && w.nudges == 0, "calm: %d hops and %d nudges (a reminder at 300 s too)", w.hops, w.nudges);
    t += 400;
    bot_set_calm(&b, false, t);
    wait_run(&b, t, 30, 1.0 / 60, &w);
    CHECK(w.hops >= 5 && fabs(w.hop[0].t - t - 0.6) < 0.02 && w.peeks >= 1, "calm off: phase A again (%d hops), then B (%d peeks)",
          w.hops, w.peeks);
    bot_init(&b, 0x62, 0);
    bot_set_calm(&b, true, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    wait_run(&b, 0, 60, 1.0 / 60, &w);
    CHECK(w.hops == 0 && w.peeks >= 5, "calm: peeks go on (%d), hops don't (%d)", w.peeks, w.hops);

    bot_init(&b, 0x63, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    t = 0;
    wait_to(&b, &t, 30);
    double tl = t;
    bot_look(&b, 0.8, 0.5, tl);
    b.next_peek_at = tl + 1;
    int moved = 0;
    for (; t < tl + 2.5; t += 1.0 / 120) {
        bot_t was = b;
        bot_pose(&b, t);
        moved += b.gaze_start != was.gaze_start && t >= was.next_peek_at;
    }
    CHECK(!moved && b.next_peek_at >= tl + 5 - 0.01 && b.next_peek_at <= tl + 7 + 0.01,
          "a knob look holds peeks for 2.5 s (%d moved), the peek skipped to %.2f s after", moved, b.next_peek_at - tl);
    bot_init(&b, 0x64, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    t = 0;
    wait_to(&b, &t, 25);
    bot_act_begin(&b, t);
    wait_run(&b, t, 20, 1.0 / 120, &w);
    CHECK(w.peeks == 0, "acting: no peeks (%d)", w.peeks);
}

static void test_wait_hop_chain(void)
{
    int doubles = 0, double_bad = 0, reminders = 0, long_rem = 0, rem_bad = 0;
    for (int seed = 0; seed < 40; seed++) {
        bot_t b;
        bot_init(&b, 0xC4A + (uint64_t)seed, 0);
        bot_set_mood(&b, BOT_WAITING, 0);
        double t = 0;
        for (; t < 20 && b.burst_hop_at == INFINITY; t += 1.0 / 120) bot_pose(&b, t);
        if (b.burst_hop_at != INFINITY) {
            doubles++;
            double_bad += b.burst_hop_at != b.hop_start + b.hop_len;
        }
        for (; t < BOT_WAIT_REMIND_S + 2 && !(b.hopping && b.hop_start >= BOT_WAIT_REMIND_S); t += 1.0 / 120) bot_pose(&b, t);
        if (!b.hopping || b.hop_start < BOT_WAIT_REMIND_S) continue;
        reminders++;
        long_rem += b.hop_len > 1.05;
        rem_bad += b.burst_hop_at != b.hop_start + fmax(1.05, b.hop_len);
    }
    CHECK(doubles >= 5 && !double_bad, "a double hop's second is set for the first's end (%d of %d off)", double_bad, doubles);
    CHECK(reminders == 40 && long_rem >= 3 && !rem_bad, "a reminder's second at 1.05 s or the first's end if later (%d of %d off, %d longer)",
          rem_bad, reminders, long_rem);

    static const int kinds[] = {HOP_PLAIN, HOP_BURST, HOP_REMIND};
    int early = 0, waited = 0;
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        for (int seed = 0; seed < 10; seed++) {
            bot_t b;
            bot_init(&b, 0xC5A + (uint64_t)seed, 0);
            bot_set_mood(&b, BOT_WAITING, 0);
            double t = 0, at = kinds[k] == HOP_REMIND ? 200 : 1;
            for (; t < at; t += 1.0 / 120) bot_pose(&b, t);
            b.calm = false;
            bot_hop(&b, t);
            double start = b.hop_start, end = start + b.hop_len;
            if (kinds[k] == HOP_PLAIN) b.next_hop_at = t + 0.3;
            if (kinds[k] == HOP_BURST) b.burst_hop_at = t + 0.3, b.burst_h = 0.6;
            if (kinds[k] == HOP_REMIND) b.next_rem_at = t + 0.3, b.nudge_at = -INFINITY, b.next_nudge_at = INFINITY;
            for (; b.hop_start == start && t < end + 0.5; t += 1.0 / 120) bot_pose(&b, t);
            early += b.hop_start < end;
            waited += b.hop_start != start && b.hop_start < end + 0.02;
        }
    }
    CHECK(!early && waited == 30, "a plain, second or reminder hop due mid-hop waits for its end (%d early, %d of 30 right after)", early, waited);
}

static void test_wait_demo(void)
{
    static wait_log_t w;
    int nudged = 0, reminded = 0, peeked = 0;
    for (int seed = 0; seed < 20; seed++) {
        bot_t b;
        bot_init(&b, 0x70 + (uint64_t)seed, 0);
        bot_wait_scene(&b, 1, 0);
        bot_set_mood(&b, BOT_WAITING, 0);
        wait_run(&b, 0, 60, 1.0 / 120, &w);
        int rem = 0, ph3 = 0;
        for (int i = 0; i < w.hops; i++) rem += w.hop[i].kind == HOP_REMIND && w.hop[i].t >= BOT_WAIT_DEMO_REMIND_S && w.hop[i].t < BOT_WAIT_DEMO_REMIND_S + BOT_NUDGE_S + 0.01;
        for (int i = 0; i < w.hops; i++) ph3 += w.hop[i].kind == HOP_PLAIN && w.hop[i].t >= BOT_WAIT_DEMO_C_S;
        CHECK(!ph3 && w.nudge[0] >= BOT_WAIT_DEMO_C_S + 1 && w.nudge[0] <= BOT_WAIT_DEMO_C_S + 3 + 0.01,
              "demo seed %d: phase C from 35 s (first nudge %.2f, %d hops after)", seed, w.nudges ? w.nudge[0] : -1, ph3);
        for (int i = 0; i < w.peeks; i++) peeked += w.peek[i].t >= BOT_WAIT_B_S && w.peek[i].t < BOT_WAIT_DEMO_C_S;
        nudged += w.nudges >= 2;
        reminded += rem == 1;
    }
    CHECK(nudged == 20 && reminded == 20 && peeked >= 20 * 2,
          "demo, 60 s hold: %d of 20 with 2 nudges, %d with the reminder at 50 s, %d peeks in phase B", nudged, reminded, peeked);

    bot_t b;
    bot_init(&b, 0x90, 0);
    bot_set_mood(&b, BOT_WAITING, 0);
    double t = 0;
    wait_to(&b, &t, 10);
    bot_wait_scene(&b, 1, t);
    CHECK(b.w_t0 == t && b.w_scene == 1, "a demo starting mid-waiting restarts it");
    double t0 = t;
    bot_wait_scene(&b, 1, t + 5);
    CHECK(b.w_t0 == t0, "the same scene: no restart");
    wait_to(&b, &t, t0 + 40);
    CHECK(b.w_phase == 3, "demo: phase C at 40 s");
    bot_wait_scene(&b, 2, t);
    double t2 = t;
    wait_to(&b, &t, t2 + 1);
    CHECK(b.w_t0 == t2 && b.w_phase == 1, "a scene replaced by the next in the same frame: phase A again");
    wait_to(&b, &t, t2 + 40);
    bot_wait_scene(&b, 0, t);
    wait_to(&b, &t, t + 40);
    CHECK(b.w_phase == 2, "demo over: real timings again, phase B at 40 s");
}

static double lid_max(const bot_pose_t *p) { return fmax(p->lid_l, p->lid_r); }

static bot_eyes_t mood_eyes(bot_mood_t m)
{
    return m == BOT_WAITING ? BOT_EYES_ROUND : m == BOT_DONE ? BOT_EYES_HAPPY : m == BOT_ERROR ? BOT_EYES_ANGRY : BOT_EYES_DASH;
}

static double settle_open(bot_t *b, double t)
{
    for (; b->blinking || t < 3; t += 0.001) bot_pose(b, t);
    return t;
}

typedef struct {
    double beat, shut_from, shut_to, open_after;
    bool beat_shut, beat_eyes, beat_badge, held, early;
} beat_log_t;

static void beat_run(bot_t *b, bot_mood_t m, double t0, double ready_at, beat_log_t *g)
{
    *g = (beat_log_t){.beat = -1, .shut_from = -1, .shut_to = -1, .open_after = -1, .held = true};
    bot_mood_t face0 = b->face;
    bot_eyes_t eyes0 = b->eyes;
    double badge0 = 0;
    bot_set_mood(b, m, t0);
    for (double t = t0; t < t0 + 1; t += 0.001) {
        bot_rim_ready(b, t >= ready_at);
        bot_pose_t p = bot_pose(b, t);
        if (g->beat < 0 && p.face == m) {
            g->beat = t;
            g->beat_shut = lid_max(&p) >= 0.9;
            g->beat_eyes = p.eyes == mood_eyes(m);
            g->beat_badge = b->badge.start == t && fabs(p.badge - badge0) < 1e-9;
        }
        if (g->beat < 0) {
            g->early |= p.face != face0 || p.eyes != eyes0;
            badge0 = p.badge;
            if (lid_max(&p) >= 0.999 && fmin(p.lid_l, p.lid_r) >= 0.999) {
                if (g->shut_from < 0) g->shut_from = t;
                g->shut_to = t;
            } else if (g->shut_from >= 0) {
                g->held = false;
            }
        }
        if (g->beat >= 0 && g->open_after < 0 && lid_max(&p) < 0.05) g->open_after = t - g->beat;
    }
}

static void test_mood_beat(void)
{
    bot_t b;
    beat_log_t g;
    bot_init(&b, 5, 0);
    double t0 = settle_open(&b, 0);
    beat_run(&b, BOT_DONE, t0, t0 + 0.25, &g);
    CHECK(g.beat >= t0 + 0.25 && g.beat < t0 + 0.252 && g.beat_shut && g.beat_eyes && g.beat_badge && !g.early,
          "outline late (0.25 s): the beat waits for it, eyes and badge in that frame behind shut lids (beat +%.3f s)", g.beat - t0);
    CHECK(g.held && g.shut_from <= t0 + 0.15 && g.shut_to >= t0 + 0.249, "the lids stay shut %.3f..%.3f s while it waits",
          g.shut_from - t0, g.shut_to - t0);
    CHECK(g.open_after > 0.1 && g.open_after < 0.3, "then they open as a 1.6x blink does (%.3f s)", g.open_after);

    bot_init(&b, 5, 0);
    t0 = settle_open(&b, 0);
    beat_run(&b, BOT_WAITING, t0, INFINITY, &g);
    CHECK(g.beat >= t0 + BOT_BEAT_MAX_S && g.beat < t0 + BOT_BEAT_MAX_S + 0.002 && g.beat_shut && g.beat_eyes && g.beat_badge,
          "no outline: the beat at %.2f s anyway, still behind the lids (+%.3f s)", BOT_BEAT_MAX_S, g.beat - t0);

    bot_init(&b, 5, 0);
    t0 = settle_open(&b, 0);
    beat_run(&b, BOT_ERROR, t0, t0, &g);
    CHECK(g.beat > t0 + 0.1 && g.beat < t0 + 0.14 && g.beat_shut && g.beat_eyes && g.beat_badge && !g.early,
          "outline ready at once: the beat at the first close (+%.3f s)", g.beat - t0);
    CHECK(g.shut_to - g.shut_from < 0.06, "and no held lids (%.3f s shut)", g.shut_to - g.shut_from);

    bot_init(&b, 5, 0);
    t0 = settle_open(&b, 0);
    bot_blink(&b, t0);
    double t = t0, prev = 0, jump = 0;
    for (; t < t0 + 0.2; t += 0.001) {
        bot_pose_t p = bot_pose(&b, t);
        prev = lid_max(&p);
    }
    bot_set_mood(&b, BOT_WORKING, t);
    double tm = t, beat = -1;
    for (; t < tm + 1; t += 0.001) {
        bot_rim_ready(&b, t >= tm + 0.3);
        bot_pose_t p = bot_pose(&b, t);
        jump = fmax(jump, lid_max(&p) - prev);
        prev = lid_max(&p);
        if (beat < 0 && p.face == BOT_WORKING) beat = t;
    }
    CHECK(jump < 0.05, "a change while the lids open: no snap shut (largest step %.3f in 1 ms)", jump);
    CHECK(beat >= tm + 0.3 && beat < tm + 0.302, "the double blink carries the beat once the outline is ready (+%.3f s)", beat - tm);

    bot_init(&b, 5, 0);
    t0 = settle_open(&b, 0);
    bot_sleep(&b, t0);
    for (t = t0; t < t0 + 3; t += 0.01) bot_pose(&b, t);
    bot_rim_ready(&b, true);
    beat_run(&b, BOT_WAITING, t, t + 0.3, &g);
    CHECK(g.beat >= t + 0.3 && g.beat < t + 0.302 && g.beat_eyes && g.shut_from <= t + 0.001 && g.held,
          "asleep, then waiting: the lids stay shut until the outline (+%.3f s)", g.beat - t);

    int beats = 0, bad = 0, slow = 0;
    for (int seed = 0; seed < 300; seed++) {
        bot_init(&b, 0xBEA7 + (uint64_t)seed, 0);
        bot_set_sleep_after(&b, 0);
        uint64_t r = 0x9E37 * (uint64_t)(seed + 1);
        t = 0;
        double ready_at = 0, since = 0;
        bot_pose_t last = bot_pose(&b, 0);
        for (int k = 0; k < 12; k++) {
            r = r * 6364136223846793005ULL + 1442695040888963407ULL;
            double dwell = 0.15 + (double)(r >> 40) / (double)(1ULL << 24) * 4;
            double lat = (double)((r >> 20) & 0xFFFF) / 65536.0 * 0.6;
            bot_mood_t m = (bot_mood_t)((r >> 8) % 6);
            if (bot_set_mood(&b, m, t)) since = t, ready_at = t + lat;
            for (double end = t + dwell; t < end; t += 1.0 / 120) {
                bot_rim_ready(&b, t >= ready_at);
                bot_pose_t p = bot_pose(&b, t);
                if (p.face != last.face) {
                    beats++;
                    bad += lid_max(&p) < 0.9 || p.eyes != mood_eyes(p.face) || b.badge.start != t;
                }
                bad += p.face == last.face && p.eyes != last.eyes && !b.acting;
                slow += p.face != p.mood && t - since > BOT_BEAT_MAX_S + 0.35;
                last = p;
            }
        }
    }
    printf("mood beat: %d beats over 300 seeds, %d off the lids, eyes or badge, %d late\n", beats, bad, slow);
    CHECK(beats > 1000 && !bad && !slow, "every beat behind closed lids with its eyes and badge (%d bad, %d late of %d)", bad, slow, beats);
}

typedef struct {
    double beat, lid, resume;
    bool early, wrong;
} gap_log_t;

static void gap_run(uint64_t seed, double gap_at, double gap_len, double ready_after, gap_log_t *g)
{
    bot_t b;
    bot_init(&b, seed, 0);
    double t0 = settle_open(&b, 0);
    bot_eyes_t eyes0 = b.eyes;
    bot_set_mood(&b, BOT_DONE, t0);
    *g = (gap_log_t){.beat = -1, .resume = t0 + gap_at + gap_len};
    bool gapped = false;
    for (double t = t0; t < g->resume + 2; t += 1.0 / 60) {
        if (!gapped && t > t0 + gap_at) {
            gapped = true;
            t = g->resume;
        }
        bot_rim_ready(&b, t >= g->resume + ready_after);
        bot_pose_t p = bot_pose(&b, t);
        if (g->beat < 0 && p.face == BOT_DONE) {
            g->beat = t - g->resume;
            g->lid = lid_max(&p);
            g->wrong = p.eyes != BOT_EYES_HAPPY;
        }
        if (g->beat < 0) g->early |= p.eyes != eyes0;
    }
}

static void test_mood_beat_gaps(void)
{
    gap_log_t g;
    int bad = 0;
    double worst = 1;
    for (int seed = 0; seed < 20; seed++) {
        gap_run(0x6A9 + (uint64_t)seed, 0.03, 5, 0.12, &g);
        bad += g.beat < 0.12 || g.beat > BOT_BEAT_MAX_S + 0.02 || g.lid < 0.9 || g.early || g.wrong;
        worst = fmin(worst, g.lid);
    }
    CHECK(!bad, "an OTA pause from the closing blink: the beat on a new closure once the outline is back (%d bad, lowest lid %.2f)", bad,
          worst);
    bad = 0;
    for (int seed = 0; seed < 20; seed++) {
        gap_run(0x6A9 + (uint64_t)seed, 0.03, 5, INFINITY, &g);
        bad += g.beat < BOT_BEAT_MAX_S - 0.02 || g.beat > BOT_BEAT_MAX_S + 0.02 || g.lid < 0.9 || g.early || g.wrong;
    }
    CHECK(!bad, "an OTA pause and no outline after it: the beat %.2f s after the pause, behind the lids (%d bad)", BOT_BEAT_MAX_S, bad);
    bad = 0;
    worst = 1;
    int n = 0;
    for (int seed = 0; seed < 20; seed++) {
        for (double len = 0.08; len < 0.6; len += 0.01, n++) {
            gap_run(0x6A9 + (uint64_t)seed, 0.001, len, -INFINITY, &g);
            bad += g.beat < 0 || g.lid < 0.9 || g.early || g.wrong;
            worst = fmin(worst, g.lid);
        }
    }
    CHECK(!bad, "a frame gap across the closing blink, outline ready: the beat on another closure, never open (%d of %d bad, lowest lid %.2f)",
          bad, n, worst);
    bad = 0;
    double latest = 0;
    n = 0;
    for (int seed = 0; seed < 40; seed++) {
        for (double dt = 0.11; dt < 0.4; dt += 0.07, n++) {
            bot_t b;
            bot_init(&b, 0x6B0 + (uint64_t)seed, 0);
            double t0 = settle_open(&b, 0), beat = -1, lid = 0;
            bot_set_mood(&b, BOT_DONE, t0);
            bot_rim_ready(&b, seed % 2);
            for (double t = t0 + dt; t < t0 + 5 && beat < 0; t += dt) {
                bot_pose_t p = bot_pose(&b, t);
                if (p.face == BOT_DONE) beat = t - t0, lid = lid_max(&p);
            }
            double by = seed % 2 ? 2 : BOT_BEAT_MAX_S + 2 * dt;
            latest = fmax(latest, beat < 0 ? INFINITY : beat - by);
            bad += beat < 0 || beat > by + 1e-9 || lid < 0.9;
        }
    }
    CHECK(!bad, "slow frames (0.11-0.39 s): the beat behind the lids, without an outline 0.45 s after the first gap (not every gap), with one "
          "within 2 s, no re-closure that frames keep missing (%d of %d bad, worst %.2f s past)", bad, n, latest);

    bot_t b;
    bot_init(&b, 0x6A9, 0);
    double t0 = settle_open(&b, 0);
    bot_act_lid(&b, 0.8, 0.01, t0 - 0.5);
    for (double t = t0 - 0.5; t < t0; t += 0.001) bot_pose(&b, t);
    bot_set_mood(&b, BOT_DONE, t0);
    double blink = -1, at = -1;
    for (double t = t0; t < t0 + 1 && at < 0; t += 0.001) {
        bot_pose_t p = bot_pose(&b, t);
        if (p.face == BOT_DONE) {
            at = t - b.blink_start;
            blink = fmin(p.blink_l, p.blink_r);
            worst = lid_max(&p);
        }
    }
    CHECK(at >= 0 && worst >= 0.9 && blink >= BOT_BLINK_HIDE, "lids at 0.8 (an act's lid): the beat once the blink alone hides both eyes, "
          "not when lid and blink reach 0.9 together, which leaves the clipped eyes' crescent showing (lid %.2f, blink %.2f)", worst, blink);
}

static void test_error_shake(void)
{
    int bad = 0, crossings_bad = 0, runs = 0;
    double peak_all = 0, late_all = 0;
    for (int seed = 1; seed <= 40; seed++) {
        bot_t a, c;
        bot_init(&a, seed, 0);
        bot_init(&c, seed, 0);
        double t0 = settle_open(&a, 0);
        settle_open(&c, 0);
        bot_set_calm(&c, true, 0);
        bot_set_mood(&a, BOT_ERROR, t0);
        bot_set_mood(&c, BOT_ERROR, t0);
        double tb = -1, peak = 0, early = 0, late = 0, prev = 0;
        int cross = 0;
        for (double t = t0; t < t0 + 4; t += 0.001) {
            bot_pose_t p = bot_pose(&a, t), q = bot_pose(&c, t);
            if (tb < 0 && p.face == BOT_ERROR) tb = t;
            double d = p.offset_x - q.offset_x, u = tb < 0 ? -1 : t - tb - BOT_SHAKE_AT_S;
            if (u < 0) early = fmax(early, fabs(d));
            else if (u < BOT_SHAKE_S) {
                peak = fmax(peak, fabs(d));
                if (d * prev < 0) cross++;
                if (d != 0) prev = d;
            } else late = fmax(late, fabs(d));
        }
        runs++;
        if (early > 0 || tb < 0) bad++;
        if (cross != 5) crossings_bad++;
        peak_all = fmax(peak_all, peak);
        late_all = fmax(late_all, late);
    }
    CHECK(!bad, "shake: %d of %d runs move the eyes before the beat + %.1f s", bad, runs, BOT_SHAKE_AT_S);
    CHECK(peak_all > 0.8 * BOT_SHAKE_AMP && peak_all <= BOT_SHAKE_AMP, "shake: peak %.4f, want about %.4f (6 px)", peak_all, BOT_SHAKE_AMP);
    CHECK(!crossings_bad, "shake: %d of %d runs not 3 cycles (5 sign changes) in %.1f s", crossings_bad, runs, BOT_SHAKE_S);
    CHECK(late_all == 0, "shake: over after %.1f s (%.4f later)", BOT_SHAKE_S, late_all);

    bot_t b;
    bot_init(&b, 5, 0);
    double t0 = settle_open(&b, 0), moved = 0;
    bot_set_mood(&b, BOT_ERROR, t0);
    bot_set_mood(&b, BOT_IDLE, t0 + 0.05);
    bot_t r;
    bot_init(&r, 5, 0);
    settle_open(&r, 0);
    bot_set_mood(&r, BOT_ERROR, t0);
    bot_set_mood(&r, BOT_IDLE, t0 + 0.05);
    bot_set_calm(&r, true, 0);
    for (double t = t0 + 0.05; t < t0 + 3; t += 0.001) moved = fmax(moved, fabs(bot_pose(&b, t).offset_x - bot_pose(&r, t).offset_x));
    CHECK(moved == 0, "shake: error left before its beat shakes nothing (%.4f)", moved);
}

static void test_error_huff(void)
{
    int huffs = 0, n_gaps = 0, outside = 0, shape_bad = 0;
    double gaps[200], first_min = 1e9, dip = 0, lid = 0;
    for (int seed = 1; seed <= 12; seed++) {
        bot_t b;
        bot_init(&b, seed, 0);
        bot_set_sleep_after(&b, 0);
        double t0 = settle_open(&b, 0), tb = -1, last = -1, start = -1;
        bot_set_mood(&b, BOT_ERROR, t0);
        bool was = false;
        for (double t = t0; t < t0 + 400; t += 1.0 / 120) {
            bot_pose_t p = bot_pose(&b, t);
            if (tb < 0 && p.face == BOT_ERROR) tb = t;
            bool on = p.eye_dy < 0;
            if (on && !was) {
                huffs++;
                start = t;
                if (last < 0) first_min = fmin(first_min, t - tb);
                else if (n_gaps < 200) gaps[n_gaps++] = t - last;
                last = t;
            }
            if (on) {
                double k = sin(M_PI * (t - b.huff_at) / BOT_HUFF_S);
                if (fabs(p.eye_dy + BOT_HUFF_DIP * k) > 1e-9 || p.lid_l < BOT_HUFF_LID * k - 1e-9) shape_bad++;
                dip = fmin(dip, p.eye_dy);
                lid = fmax(lid, fmin(p.lid_l, p.lid_r) * (b.blinking ? 0 : 1));
                if (t - start > BOT_HUFF_S) shape_bad++;
            }
            was = on;
        }
        bot_set_mood(&b, BOT_WORKING, t0 + 400);
        for (double t = t0 + 400; t < t0 + 520; t += 1.0 / 60) outside += bot_pose(&b, t).eye_dy != 0;
    }
    for (int i = 1; i < n_gaps; i++)
        for (int j = i; j > 0 && gaps[j] < gaps[j - 1]; j--) { double x = gaps[j]; gaps[j] = gaps[j - 1]; gaps[j - 1] = x; }
    double med = n_gaps ? gaps[n_gaps / 2] : 0;
    printf("error huffs: %d in 12 x 400 s, median gap %.1f s (%.1f-%.1f), first after %.1f s\n", huffs, med, n_gaps ? gaps[0] : 0,
           n_gaps ? gaps[n_gaps - 1] : 0, first_min);
    CHECK(med > 20 && med < 30 && gaps[0] >= BOT_HUFF_MEDIAN_S / 2 - 0.02 && gaps[n_gaps - 1] <= BOT_HUFF_MEDIAN_S * 2 + 0.02,
          "huff: every lognormal(%.0f s) in %.1f-%.0f s, median %.1f", BOT_HUFF_MEDIAN_S, BOT_HUFF_MEDIAN_S / 2, BOT_HUFF_MEDIAN_S * 2, med);
    CHECK(first_min >= BOT_SHAKE_AT_S + BOT_HUFF_MEDIAN_S / 2 - 0.02, "huff: the first one after the shake, %.1f s after the beat", first_min);
    CHECK(!shape_bad && fabs(dip + BOT_HUFF_DIP) < 1e-3 && fabs(lid - BOT_HUFF_LID) < 0.01,
          "huff: eyes dip %.3f and lids squeeze to %.2f as a half sine over %.1f s (%d bad frames)", dip, lid, BOT_HUFF_S, shape_bad);
    CHECK(!outside, "huff: none outside error (%d frames)", outside);
    bot_pose_t e0 = {.eyes = BOT_EYES_ANGRY, .gaze_x = 0.3, .scale_x = 1, .scale_y = 1, .triangle = 1}, e1 = e0;
    e1.eye_dy = -BOT_HUFF_DIP;
    bot_stroke_t s0[2], s1[2];
    bot_eye_strokes(&e0, 1.15, s0);
    bot_eye_strokes(&e1, 1.15, s1);
    CHECK(fabs(s1[0].y[0] - s0[0].y[0] + BOT_HUFF_DIP) < 1e-12 && fabs(s1[1].y[1] - s0[1].y[1] + BOT_HUFF_DIP) < 1e-12 &&
              s1[0].x[0] == s0[0].x[0],
          "huff: eye_dy moves the eyes down");

    bot_t b;
    bot_init(&b, 3, 0);
    bot_set_sleep_after(&b, 0);
    double t0 = settle_open(&b, 0);
    bot_set_calm(&b, true, t0);
    bot_set_mood(&b, BOT_ERROR, t0);
    int calm = 0, animating = 0;
    for (double t = t0; t < t0 + 200; t += 1.0 / 60) calm += bot_pose(&b, t).eye_dy != 0;
    CHECK(!calm, "huff: none while calm (%d frames)", calm);
    bot_set_calm(&b, false, t0 + 200);
    int after = 0;
    bool was = false;
    for (double t = t0 + 200; t < t0 + 400; t += 1.0 / 60) {
        bot_pose_t p = bot_pose(&b, t);
        if (p.eye_dy != 0) animating += !b.animating;
        after += p.eye_dy != 0 && !was;
        was = p.eye_dy != 0;
    }
    CHECK(!animating && after > 0, "huff: %d huffs after calm ends, each counted as animating (%d frames not)", after, animating);
}

static void test_error_exit_continuity(void)
{
    int bad_huff = 0, bad_calm = 0, bad_shake = 0, runs = 0;
    for (int seed = 1; seed <= 12; seed++)
        for (int how = 0; how < 2; how++) {
            bot_t b;
            bot_init(&b, seed, 0);
            bot_set_sleep_after(&b, 0);
            double t0 = settle_open(&b, 0), t = t0, left = -1, h0 = 0;
            bot_set_mood(&b, BOT_ERROR, t0);
            for (; t < t0 + 120; t += 1.0 / 120) {
                bot_pose_t p = bot_pose(&b, t);
                if (left < 0 && p.eye_dy < 0 && t - b.huff_at >= BOT_HUFF_S / 2) {
                    left = t;
                    h0 = b.huff_at;
                    if (how == 0) bot_set_mood(&b, BOT_IDLE, t);
                    else bot_set_calm(&b, true, t);
                    continue;
                }
                if (left < 0) continue;
                if (t - h0 >= BOT_HUFF_S) break;
                double want = -BOT_HUFF_DIP * sin(M_PI * (t - h0) / BOT_HUFF_S), lid = fmin(p.lid_l, p.lid_r);
                if (fabs(p.eye_dy - want) > 1e-9 || lid < BOT_HUFF_LID * -want / BOT_HUFF_DIP - 1e-9)
                    *(how ? &bad_calm : &bad_huff) += 1;
            }
            runs += left > 0;
        }
    CHECK(runs == 24 && !bad_huff, "huff: leaving error at its peak lets it finish (%d frames snap, %d runs)", bad_huff, runs);
    CHECK(!bad_calm, "huff: calm turning on at its peak lets it finish (%d frames snap)", bad_calm);

    for (int seed = 1; seed <= 20; seed++) {
        bot_t a, c;
        bot_init(&a, seed, 0);
        bot_init(&c, seed, 0);
        double t0 = settle_open(&a, 0), tb = -1;
        settle_open(&c, 0);
        bot_set_calm(&c, true, 0);
        bot_set_mood(&a, BOT_ERROR, t0);
        bot_set_mood(&c, BOT_ERROR, t0);
        bool left = false;
        for (double t = t0; t < t0 + 3; t += 0.001) {
            bot_pose_t p = bot_pose(&a, t), q = bot_pose(&c, t);
            if (tb < 0 && p.face == BOT_ERROR) tb = t;
            double u = tb < 0 ? -1 : (t - tb - BOT_SHAKE_AT_S) / BOT_SHAKE_S;
            if (!left && u >= 0.5) {
                bot_set_mood(&a, BOT_WORKING, t);
                bot_set_mood(&c, BOT_WORKING, t);
                left = true;
                continue;
            }
            if (!left || u >= 1) continue;
            double want = BOT_SHAKE_AMP * sin(2 * M_PI * BOT_SHAKE_CYCLES * u) * (1 - u);
            if (fabs(p.offset_x - q.offset_x - want) > 1e-6) bad_shake++;
        }
    }
    CHECK(!bad_shake, "shake: leaving error mid-shake lets it finish (%d frames snap)", bad_shake);
}

static void test_error_overlaps(void)
{
    int track_bad = 0, sac_bad = 0;
    for (int seed = 1; seed <= 20; seed++) {
        bot_t a, c;
        bot_init(&a, seed, 0);
        bot_init(&c, seed, 0);
        double t0 = settle_open(&a, 0), tb = -1, track_at = -1;
        settle_open(&c, 0);
        bot_set_calm(&c, true, 0);
        bot_set_mood(&a, BOT_ERROR, t0);
        bot_set_mood(&c, BOT_ERROR, t0);
        for (double t = t0; t < t0 + 4; t += 0.001) {
            if (track_at > 0 && t - track_at < 2) {
                bot_track(&a, 0.4, -0.2, t);
                bot_track(&c, 0.4, -0.2, t);
            }
            bot_pose_t p = bot_pose(&a, t), q = bot_pose(&c, t);
            if (tb < 0 && p.face == BOT_ERROR) tb = t;
            if (track_at < 0 && tb > 0 && t >= tb + BOT_SHAKE_AT_S + 0.1) track_at = t;
            if (track_at > 0 && t > track_at && t - track_at < 2) {
                if (p.offset_x != q.offset_x) track_bad++;
                if (fabs(a.gaze_to_x) >= 0.7 && a.gaze_start > track_at) sac_bad++;
            }
        }
    }
    CHECK(!track_bad, "error: touch tracking stops the shake (%d frames shake under the finger)", track_bad);
    CHECK(!sac_bad, "error: no side darts while tracking");

    int act_huffs = 0, free_huffs = 0;
    for (int seed = 1; seed <= 8; seed++) {
        bot_t b;
        bot_init(&b, seed, 0);
        bot_set_sleep_after(&b, 0);
        double t0 = settle_open(&b, 0);
        bot_set_mood(&b, BOT_ERROR, t0);
        for (double t = t0; t < t0 + 2; t += 1.0 / 60) bot_pose(&b, t);
        bot_act_begin(&b, t0 + 2);
        bot_act_lid(&b, 0.3, 0.2, t0 + 2);
        for (double t = t0 + 2; t < t0 + 200; t += 1.0 / 60) act_huffs += bot_pose(&b, t).eye_dy != 0;
        bot_act_end(&b, true, 0.3, t0 + 200);
        for (double t = t0 + 200; t < t0 + 400; t += 1.0 / 60) free_huffs += bot_pose(&b, t).eye_dy != 0;
    }
    CHECK(!act_huffs && free_huffs > 0, "error: a scripted act gets no huff (%d frames), huffs come back after it (%d)", act_huffs, free_huffs);
}

#define BEAT_PX_W 160
#define BEAT_PX_H 200

static int eye_px(const bot_pose_t *p)
{
    static uint8_t a[BEAT_PX_W * BEAT_PX_H];
    static bot_raster_scratch_t sc;
    const double R = 195.72;
    bot_stroke_t st[2];
    bot_eye_strokes(p, 1.15, st);
    int n = 0;
    for (int e = 0; e < 2; e++) {
        double xs[BOT_STROKE_MAX], ys[BOT_STROKE_MAX], wd = st[e].width * R * (p->scale_x + p->scale_y) / 2, x0 = 1e9, y0 = 1e9, y1 = -1e9;
        float fx[BOT_STROKE_MAX], fy[BOT_STROKE_MAX];
        int segs[BOT_STROKE_MAX];
        for (int i = 0; i < st[e].n; i++) {
            xs[i] = 236 + R * (p->offset_x + p->scale_x * st[e].x[i]);
            ys[i] = 233 - R * (p->offset_y * 0.45 - (1 - p->scale_y) + p->scale_y * st[e].y[i]);
            fx[i] = (float)xs[i];
            fy[i] = (float)ys[i];
            segs[i] = i;
            x0 = fmin(x0, xs[i]);
            y0 = fmin(y0, ys[i]);
            y1 = fmax(y1, ys[i]);
        }
        int ox = ((int)floor(x0 - wd / 2 - 1)) & ~1, oy = ((int)floor(y0 - wd / 2 - 1)) & ~1, h = ((int)ceil(y1 + wd / 2 + 1) - oy + 1) & ~1;
        if (h > BEAT_PX_H) h = BEAT_PX_H;
        float line = (float)bot_lid_line_y(ys, st[e].n, wd, st[e].lid, st[e].keep);
        bot_raster_lid_box(line, &oy, &h);
        bot_raster_stroke_below(&sc, NULL, a, line, BEAT_PX_W, h, ox, oy, fx, fy, st[e].n - 1, segs, (float)wd / 2, 0xF4F4F2, 1.0f);
        for (int i = 0; i < BEAT_PX_W * h; i++) n += a[i] != 0;
    }
    return n;
}

static void test_beat_hidden(void)
{
    static const bot_mood_t M[] = {BOT_IDLE, BOT_WORKING, BOT_WAITING, BOT_ERROR, BOT_DONE, BOT_SLEEPY};
    int swaps = 0, shown = 0, worst = 0;
    for (int seed = 1; seed <= 60; seed++) {
        bot_t b;
        bot_init(&b, seed, 0);
        bot_set_sleep_after(&b, 0);
        double t = settle_open(&b, 0);
        bot_eyes_t eyes = b.eyes;
        uint64_t r = seed * 0x9E3779B97F4A7C15ULL;
        for (int k = 0; k < 8; k++) {
            r = r * 6364136223846793005ULL + 1442695040888963407ULL;
            bot_mood_t m = M[(r >> 33) % 6];
            double ready = ((r >> 20) % 61) / 100.0, at = t;
            bot_rim_ready(&b, false);
            bot_set_mood(&b, m, at);
            for (; t < at + 2.5; t += 1.0 / 60) {
                bot_rim_ready(&b, t - at >= ready);
                bot_pose_t p = bot_pose(&b, t);
                if (p.eyes != eyes) {
                    int px = eye_px(&p);
                    swaps++;
                    shown += px > 0;
                    if (px > worst) worst = px;
                    eyes = p.eyes;
                }
            }
        }
    }
    printf("beat: %d eye swaps, %d with eye pixels showing (worst %d px)\n", swaps, shown, worst);
    CHECK(swaps > 200 && !shown, "beat: every eye swap lands with no eye pixel showing (%d of %d show, worst %d px)", shown, swaps, worst);
}

static void test_error_gaze(void)
{
    int sac = 0, calm = 0, dart = 0, other = 0, fix_bad = 0;
    for (int seed = 1; seed <= 20; seed++) {
        bot_t b;
        bot_init(&b, seed, 0);
        bot_set_sleep_after(&b, 0);
        double t0 = settle_open(&b, 0), seen = -1;
        bot_set_mood(&b, BOT_ERROR, t0);
        for (double t = t0 + 1; t < t0 + 300; t += 1.0 / 60) {
            bot_pose(&b, t);
            if (b.gaze_start == seen || b.gaze_dur <= 0.0251) continue;
            seen = b.gaze_start;
            double x = b.gaze_to_x, y = b.gaze_to_y, fix = b.next_saccade_at - b.gaze_start - b.gaze_dur;
            sac++;
            if ((fabs(x) <= 0.08 + 1e-9 && fabs(y - 0.1) <= 0.08 + 1e-9) || (fabs(x) <= 0.15 + 1e-9 && fabs(y + 0.5) <= 0.1 + 1e-9)) {
                calm++;
                fix_bad += fix < 1.2 - 1e-9 || fix > 2.5 + 1e-9;
            } else if (fabs(x) >= 0.7 - 1e-9 && fabs(x) <= 0.9 + 1e-9 && fabs(y) <= 0.3 + 1e-9) {
                dart++;
                fix_bad += fix < 0.4 - 1e-9 || fix > 0.9 + 1e-9;
            } else {
                other++;
            }
        }
    }
    double share = sac ? (double)calm / sac : 0;
    printf("error gaze: %d saccades, %.0f %% viewer or down, %.0f %% darts, %d other\n", sac, 100 * share, 100.0 * dart / (sac ? sac : 1), other);
    CHECK(!other && share > 0.66 && share < 0.74, "error gaze: 70 %% to the viewer or down-centre, 30 %% side darts (%.2f, %d other)", share, other);
    CHECK(!fix_bad, "error gaze: %d fixations outside 1.2-2.5 s (viewer, down) or 0.4-0.9 s (darts)", fix_bad);
}

static void test_done_reward(void)
{
    bot_t b;
    bot_init(&b, 11, 0);
    double t0 = settle_open(&b, 0), tb = -1, hop = -1, hop_dy = 0, lap0 = -1, lap1 = -1, giggle_err = 0, peak = 0;
    bot_set_mood(&b, BOT_DONE, t0);
    int laps = 0, hops = 0;
    bool was_hop = false, was_lap = false, animating = true;
    for (double t = t0; t < t0 + 7; t += 0.001) {
        bot_pose_t p = bot_pose(&b, t);
        if (tb < 0 && p.face == BOT_DONE) tb = t;
        if (tb >= 0 && !b.blinking) b.next_blink_at = INFINITY;
        if (b.hopping && !was_hop) {
            hops++;
            hop = t;
            CHECK(b.hop_h == BOT_DONE_HOP_H, "the reward hop at 0.7 height");
        }
        was_hop = b.hopping;
        hop_dy = fmax(hop_dy, p.hop_dy);
        if (p.lap && !was_lap) {
            laps++;
            lap0 = t;
        }
        if (!p.lap && was_lap) lap1 = t;
        was_lap = p.lap;
        double u = tb < 0 ? -1 : t - tb - BOT_GIGGLE_S, want = 0;
        if (u >= 0 && u < 0.12) want = 0.45 * sin(M_PI * u / 0.12);
        if (u >= 0.27 && u < 0.39) want = 0.45 * sin(M_PI * (u - 0.27) / 0.12);
        if (u > -0.1 && u < 0.5) {
            giggle_err = fmax(giggle_err, fmax(fabs(p.lid_l - want), fabs(p.lid_r - want)));
            peak = fmax(peak, p.lid_l);
            animating &= b.animating || want == 0;
        }
        if (p.lap) animating &= b.animating;
    }
    CHECK(hops == 1 && fabs(hop - tb - BOT_DONE_HOP_S) < 0.0015 && fabs(hop_dy - 0.22 * BOT_DONE_HOP_H) < 1e-3,
          "one hop %.3f s after the beat, lift %.3f", hop - tb, hop_dy);
    CHECK(giggle_err < 1e-9 && fabs(peak - BOT_GIGGLE_LID) < 1e-3, "giggle: two squeezes to 0.45, 120 ms, 150 ms apart (err %.2g, peak %.3f)",
          giggle_err, peak);
    CHECK(laps == 1 && fabs(lap0 - tb - BOT_LAP_AT_S) < 0.0015 && fabs(lap1 - lap0 - BOT_LAP_S) < 0.0015,
          "one glint lap from %.3f s for %.3f s", lap0 - tb, lap1 - lap0);
    CHECK(animating, "giggle and lap count as animating");

    bot_init(&b, 11, 0);
    t0 = settle_open(&b, 0);
    bot_set_calm(&b, true, t0);
    bot_set_mood(&b, BOT_DONE, t0);
    bool any = false;
    for (double t = t0; t < t0 + 7; t += 0.001) {
        bot_pose_t p = bot_pose(&b, t);
        if (t > t0 + 0.5 && !b.blinking) b.next_blink_at = INFINITY;
        any |= b.hopping || p.lap || (t > t0 + 0.6 && lid_max(&p) > 0);
    }
    CHECK(!any, "calm: no hop, giggle or lap");

    bot_init(&b, 11, 0);
    t0 = settle_open(&b, 0);
    bot_set_mood(&b, BOT_DONE, t0);
    for (double t = t0; t < t0 + 1; t += 0.001) bot_pose(&b, t);
    bot_set_mood(&b, BOT_WORKING, t0 + 1);
    any = false;
    for (double t = t0 + 1; t < t0 + 7; t += 0.001) {
        double hs = b.hop_start;
        any |= bot_pose(&b, t).lap || (b.hopping && b.hop_start != hs);
    }
    CHECK(!any && b.giggle_at == INFINITY, "done left before its reward: none of it later");

    any = false;
    for (int m = BOT_IDLE; m <= BOT_ERROR; m++) {
        bot_init(&b, 12, 0);
        t0 = settle_open(&b, 0);
        bot_set_mood(&b, (bot_mood_t)m, t0);
        for (double t = t0; t < t0 + 8; t += 0.004) any |= bot_pose(&b, t).lap || (b.hopping && b.hop_h == BOT_DONE_HOP_H);
    }
    CHECK(!any, "only done has the reward");

    bot_init(&b, 11, 0);
    t0 = settle_open(&b, 0);
    bot_set_mood(&b, BOT_DONE, t0);
    double lap_at = -1, back = -1;
    int steps = 0, last = -1, first = -1;
    bool constant = true;
    for (double t = t0; t < t0 + 7; t += 1.0 / 60) {
        bot_pose_t p = bot_pose(&b, t);
        if (p.lap && lap_at < 0) {
            lap_at = p.lap_at;
            back = lap_at + BOT_LAP_S / 2;
        }
        if (p.lap) constant &= p.lap_at == lap_at;
        if (back < 0 || t < back) continue;
        int k = p.lap ? bot_lap_step(p.lap_at, t, BOT_LAP_S / 45, 45) : -1;
        if (k >= 0 && first < 0) first = k;
        if (k >= 0 && k != last) steps++;
        if (k >= 0) last = k;
    }
    CHECK(constant && lap_at >= t0, "the pose carries the lap's start, the same all lap");
    CHECK(first >= 21 && first <= 23 && last == 44 && steps == 45 - first,
          "back on the bot page halfway through: the lap goes on from its step %d to its end (%d, %d steps), not from 0", first, last, steps);
    CHECK(bot_lap_step(10, 9.99, 0.1, 30) == -1 && bot_lap_step(10, 10, 0.1, 30) == 0 && bot_lap_step(10, 12.95, 0.1, 30) == 29 &&
              bot_lap_step(10, 13.0, 0.1, 30) == -1,
          "lap steps: 0 at the start, the last before its end, none outside");

    bot_init(&b, 11, 0);
    bot_set_ring(&b, false);
    t0 = settle_open(&b, 0);
    bot_set_mood(&b, BOT_DONE, t0);
    any = false;
    bool idle_after = true;
    for (double t = t0; t < t0 + 7; t += 0.001) {
        bot_pose_t p = bot_pose(&b, t);
        if (t > t0 + 0.5 && !b.blinking) b.next_blink_at = INFINITY;
        if (t > t0 + 0.5) b.next_saccade_at = b.next_micro_at = INFINITY;
        any |= p.lap;
        if (t > t0 + BOT_GIGGLE_S + 1) idle_after &= !b.animating || b.hopping;
    }
    CHECK(!any && idle_after, "working ring off: no lap, and nothing keeps the frame rate up for it");
}

static void rs_wake_nop(void *ctx) { (void)ctx; }

static void test_rim_beat(void)
{
    int idle = bot_rim_key(BOT_IDLE, false), work = bot_rim_key(BOT_WORKING, false), err = bot_rim_key(BOT_ERROR, false),
        sleepy = bot_rim_key(BOT_SLEEPY, false), nap = bot_rim_key(BOT_SLEEPY, true);
    CHECK(err == (BOT_ERROR | BOT_RIM_TRIANGLE) && bot_rim_mood(err) == BOT_ERROR && bot_rim_mood(nap) == BOT_SLEEPY &&
              (nap & BOT_RIM_ASLEEP) && !(work & BOT_RIM_TRIANGLE) && bot_rim_alpha(err) == 1.0f,
          "keys: the triangle only in error, the mood under both flags");
    rim_beat_t r;
    rim_beat_out_t o;
    rim_beat_init(&r);
    CHECK(!rim_beat_ready(&r, BOT_IDLE, true), "no job: not ready");
    rim_beat_step(&r, idle, idle, true, true, true, true, 0, &o);
    CHECK(o.start && !o.swap && !o.own && !o.settle, "first frame: a job, any readiness from before ignored");
    CHECK(!rim_beat_ready(&r, BOT_IDLE, false) && rim_beat_ready(&r, BOT_IDLE, true) && !rim_beat_ready(&r, BOT_WORKING, true),
          "ready: the job's mood with its base rendered");
    rim_beat_step(&r, idle, idle, false, false, false, true, 0.05, &o);
    CHECK(!o.start && !o.swap && !o.own, "base not rendered: nothing to show");
    rim_beat_step(&r, idle, idle, true, false, false, true, 0.12, &o);
    CHECK(o.swap && o.own && !o.settle, "base rendered: shown, no settle at boot");
    rim_beat_step(&r, idle, idle, true, true, true, true, 0.5, &o);
    CHECK(!o.swap && o.own && !o.settle, "no settle without a mood change");

    rim_beat_step(&r, work, idle, true, true, true, true, 1.0, &o);
    CHECK(o.start && !o.swap && !o.own, "mood change: a job, the old base alone meanwhile");
    rim_beat_step(&r, work, idle, true, true, true, true, 1.12, &o);
    CHECK(!o.swap && !o.own && rim_beat_ready(&r, BOT_WORKING, true), "base ready before the beat: the old outline stays");
    rim_beat_step(&r, work, work, true, false, false, true, 1.15, &o);
    CHECK(o.swap && o.own && !o.settle && rim_beat_ready(&r, BOT_WORKING, false), "the beat: swap, and shown counts as ready");
    rim_beat_step(&r, work, work, true, true, false, true, 1.24, &o);
    CHECK(!o.settle, "-1 alone: no settle yet");
    int bad = 0, m1 = 0, p1 = 0;
    for (double t = 1.36; t < 1.6; t += 0.001) {
        rim_beat_step(&r, work, work, true, true, true, true, t, &o);
        double s = t - 1.36;
        int want = s < RIM_SETTLE_M1_S ? -1 : s < RIM_SETTLE_S ? 1 : 0;
        bad += o.settle != want || !o.own;
        m1 += o.settle == -1;
        p1 += o.settle == 1;
    }
    CHECK(!bad && abs(m1 - 60) <= 1 && abs(p1 - 80) <= 1, "settle: -1 for 60 ms, +1 for 80 ms, then the base (%d off, %d, %d)", bad, m1, p1);

    rim_beat_step(&r, err, work, false, false, false, true, 2.0, &o);
    rim_beat_step(&r, err, err, true, false, false, true, 2.12, &o);
    CHECK(o.swap, "error: swap");
    bad = 0;
    for (double t = 2.2; t < 2.4; t += 0.001) {
        rim_beat_step(&r, err, err, true, true, true, t > 2.25, t, &o);
        double s = t - 2.2;
        int want = t <= 2.25 ? 0 : s < RIM_SETTLE_M1_S ? -1 : s < RIM_SETTLE_S ? 1 : 0;
        bad += o.settle != want;
    }
    CHECK(!bad, "a squashed pose keeps its own variant: the settle shows only while it asks for the base");

    rim_beat_step(&r, sleepy, err, true, true, true, true, 3.0, &o);
    rim_beat_step(&r, sleepy, sleepy, true, false, false, true, 3.15, &o);
    CHECK(o.swap, "sleepy: swap");
    rim_beat_step(&r, sleepy, sleepy, true, true, true, true, 4.2, &o);
    CHECK(!o.settle, "variants over 1 s after the swap: no settle");

    rim_beat_step(&r, nap, nap, true, true, true, true, 5.0, &o);
    CHECK(o.start && !o.swap, "asleep: a job");
    rim_beat_step(&r, nap, nap, true, false, false, true, 5.12, &o);
    CHECK(o.swap, "the faded outline swaps once ready, no beat to wait for");
    rim_beat_step(&r, nap, nap, true, true, true, true, 5.3, &o);
    CHECK(!o.settle, "same mood: no settle");

    rim_beat_step(&r, work, nap, true, true, true, true, 6.0, &o);
    rim_beat_step(&r, err, nap, false, false, false, true, 6.05, &o);
    CHECK(o.start && !o.own, "a second change before the beat: a new job");
    rim_beat_step(&r, err, err, false, false, false, true, 6.1, &o);
    CHECK(!o.swap && !o.own, "its base not rendered: the old outline stays even after the beat");
    rim_beat_step(&r, err, err, true, false, false, true, 6.6, &o);
    CHECK(o.swap, "then swaps when ready");
    rim_beat_step(&r, idle, err, true, false, false, true, 7.0, &o);
    rim_beat_step(&r, err, err, true, true, true, true, 7.05, &o);
    CHECK(o.start && !o.swap, "back before the beat: a job again");
    rim_beat_step(&r, err, err, true, false, false, true, 7.17, &o);
    rim_beat_step(&r, err, err, true, true, true, true, 7.3, &o);
    CHECK(o.own && !o.settle, "the same outline again: shown, no settle");

    rim_beat_init(&r);
    rim_beat_step(&r, idle, idle, false, false, false, true, 10.0, &o);
    rim_beat_step(&r, idle, idle, true, false, false, true, 10.12, &o);
    rim_beat_step(&r, work, idle, false, false, false, true, 11.0, &o);
    rim_beat_step(&r, idle, idle, false, false, false, true, 11.1, &o);
    CHECK(o.start && !o.own && rim_beat_ready(&r, BOT_IDLE, false),
          "A, B never shown, A again: A's base is on screen, so the beat back to A waits for nothing");
    rim_beat_step(&r, idle, idle, false, false, false, true, 11.15, &o);
    CHECK(!o.swap && !o.own, "and its variants stay off until the job's own base is rendered");
    rim_beat_step(&r, idle, idle, true, false, false, true, 11.22, &o);
    CHECK(o.swap && o.own && !o.settle, "then it swaps in place, no settle");

    rim_beat_init(&r);
    rim_beat_step(&r, idle, idle, true, false, false, true, 20.0, &o);
    rim_beat_step(&r, idle, idle, true, false, false, true, 20.12, &o);
    rim_beat_step(&r, work, idle, false, false, false, true, 21.0, &o);
    rim_beat_step(&r, work, idle, true, true, true, true, 21.36, &o);
    CHECK(!o.swap && !o.settle, "a late beat: the base and both settle variants rendered before it");
    rim_beat_step(&r, work, work, true, true, true, true, 21.4, &o);
    CHECK(o.swap && o.settle == -1, "the settle starts in the swap frame itself (-1 behind the lids)");
    rim_beat_step(&r, work, work, true, true, true, true, 21.4 + RIM_SETTLE_M1_S + 0.001, &o);
    CHECK(o.settle == 1, "then +1");
    rim_beat_step(&r, work, work, true, true, true, true, 21.4 + RIM_SETTLE_S + 0.001, &o);
    CHECK(!o.settle, "then the base");

    rim_beat_step(&r, err, work, false, false, false, true, 30.0, &o);
    r.off = true;
    rim_beat_step(&r, err, err, true, false, false, true, 30.12, &o);
    CHECK(o.swap, "off the bot page: the beat's swap");
    r.off = false;
    bool settled = false;
    for (double t = 30.5; t < 31.5; t += 0.001) {
        rim_beat_step(&r, err, err, true, true, true, true, t, &o);
        settled |= o.settle != 0;
    }
    CHECK(!settled, "back on the page within 1 s of an off-page swap: no settle with no beat");
    rim_beat_step(&r, idle, err, false, false, false, true, 32.0, &o);
    rim_beat_step(&r, idle, idle, true, false, false, true, 32.12, &o);
    r.off = true;
    rim_beat_step(&r, idle, idle, true, false, false, true, 32.2, &o);
    r.off = false;
    settled = false;
    for (double t = 32.3; t < 33; t += 0.001) {
        rim_beat_step(&r, idle, idle, true, true, true, true, t, &o);
        settled |= o.settle != 0;
    }
    CHECK(!settled, "a swap on the page, then off it before the variants: no settle on the return");

    static char slot_mem[RIM_SLOTS];
    void *bufs[RIM_SLOTS];
    for (int v = 0; v < RIM_SLOTS; v++) bufs[v] = &slot_mem[v];
    static rim_slots_t sl;
    rim_slots_init(&sl, bufs);
    rim_slots_ops_t ops = {.wake = rs_wake_nop};
    rim_slots_feed(&sl, idle, false, &ops);
    bool off = sl.beat.off;
    rim_slots_feed(&sl, idle, true, &ops);
    CHECK(off && !sl.beat.off, "rim_slots_feed tells the beat whether the bot page shows");
}

typedef struct {
    rim_beat_t r;
    double base_at, m1_at, p1_at, lat;
} fake_view_t;

static void test_mood_beat_view(void)
{
    static const double lats[] = {0.05, 0.12, 0.3, 0.6};
    for (size_t i = 0; i < sizeof lats / sizeof lats[0]; i++) {
        int beats = 0, same = 0, settles = 0, late = 0, open_swap = 0;
        for (int seed = 0; seed < 50; seed++) {
            bot_t b;
            bot_init(&b, 0x5E77 + (uint64_t)seed, 0);
            bot_set_sleep_after(&b, 0);
            fake_view_t v = {.lat = lats[i], .base_at = INFINITY, .m1_at = INFINITY, .p1_at = INFINITY};
            rim_beat_init(&v.r);
            static const bot_mood_t seq[] = {BOT_WORKING, BOT_WAITING, BOT_DONE, BOT_ERROR, BOT_IDLE, BOT_WORKING};
            double t = 0, beat_t = -1, swap_t = -1, settle_t = -1;
            int k = 0;
            for (; t < 6 * 4.0; t += 1.0 / 60) {
                if (k < 6 && t >= 1 + 4.0 * k) {
                    bot_set_mood(&b, seq[k++], t);
                    beat_t = swap_t = settle_t = -1;
                }
                bot_rim_ready(&b, rim_beat_ready(&v.r, (int)b.mood, t >= v.base_at));
                bot_pose_t p = bot_pose(&b, t);
                rim_beat_out_t o;
                rim_beat_step(&v.r, bot_rim_key(p.mood, p.asleep), bot_rim_key(p.face, p.asleep), t >= v.base_at, t >= v.m1_at,
                              t >= v.p1_at, fabs(p.scale_y / p.scale_x - 1) < 0.015, t, &o);
                if (o.start) {
                    v.base_at = t + v.lat;
                    v.m1_at = v.base_at + 0.12;
                    v.p1_at = v.m1_at + 0.12;
                }
                if (beat_t < 0 && k > 0 && p.face == seq[k - 1]) {
                    beat_t = t;
                    beats++;
                }
                if (o.swap && k > 0) {
                    swap_t = t;
                    same += swap_t == beat_t;
                    open_swap += lid_max(&p) < 0.9;
                }
                if (o.settle && settle_t < 0) {
                    settle_t = t;
                    settles++;
                    late += t - swap_t > 0.5;
                }
            }
        }
        printf("mood beat view, outline in %.2f s: %d beats, %d with the outline in the same frame, %d settles (%d late), %d swaps with open lids\n",
               lats[i], beats, same, settles, late, open_swap);
        if (lats[i] < BOT_BEAT_MAX_S) {
            CHECK(beats == 300 && same == beats && !open_swap, "outline in %.2f s: eyes, badge and outline in one closure (%d of %d)", lats[i],
                  same, beats);
            CHECK(settles >= 200 && !late, "outline in %.2f s: the settle follows (%d, %d late)", lats[i], settles, late);
        } else {
            CHECK(beats == 300 && same == 0, "outline in %.2f s: the beat goes at %.2f s, the outline follows", lats[i], BOT_BEAT_MAX_S);
        }
    }
}


typedef struct {
    int key;
    float sx, sy;
    bool writing;
} fake_buf_t;

static struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int turn;
    bool quit;
    uint64_t rng;
    int race_pct, raster_frames;
    rim_slots_t s;
    fake_buf_t bufs[RIM_SLOTS];
    fake_buf_t *canvas;
    int notify, badge_key;
    unsigned raster_gen;
    int replaced, races, off_frames, off_vars, on_vars;
} M;
static _Thread_local int s_me;

static void m_give(int who)
{
    pthread_mutex_lock(&M.mu);
    M.turn = who;
    pthread_cond_broadcast(&M.cv);
    while (M.turn != s_me && !(s_me == 1 && M.quit)) pthread_cond_wait(&M.cv, &M.mu);
    pthread_mutex_unlock(&M.mu);
}

static bool m_coin(void)
{
    M.rng = M.rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((M.rng >> 33) % 100) < M.race_pct;
}

static void m_race(void)
{
    if (!m_coin()) return;
    M.races++;
    m_give(!s_me);
}

void rs_race_point(rim_slots_t *s, int at);
void rs_race_point(rim_slots_t *s, int at)
{
    (void)s;
    (void)at;
    m_race();
}

static void m_raster(void *ctx, void *buf, int shape, int tone, float sx, float sy)
{
    (void)ctx;
    fake_buf_t *f = buf;
    unsigned gen = rim_job_gen(&M.s.job);
    if (sx != 1.0f || sy != 1.0f) {
        M.off_vars += M.off_frames >= 3;
        M.on_vars += M.off_frames == 0;
    }
    f->writing = true;
    for (int i = 0; i < M.raster_frames; i++) m_give(0);
    M.replaced += rim_job_gen(&M.s.job) != gen;
    f->key = tone | (shape ? BOT_RIM_TRIANGLE : 0);
    f->sx = sx;
    f->sy = sy;
    f->writing = false;
}

static void m_yield(void *ctx)
{
    (void)ctx;
    m_give(0);
}

static void *m_task(void *arg)
{
    (void)arg;
    s_me = 1;
    pthread_mutex_lock(&M.mu);
    while (M.turn != 1 && !M.quit) pthread_cond_wait(&M.cv, &M.mu);
    pthread_mutex_unlock(&M.mu);
    while (!M.quit) {
        if (M.notify) {
            M.notify = 0;
            rim_slots_run(&M.s, m_raster, m_yield, NULL);
        } else {
            m_give(0);
        }
    }
    return NULL;
}

static void m_show(void *ctx, void *buf)
{
    (void)ctx;
    M.canvas = buf;
}

static void m_wake(void *ctx)
{
    (void)ctx;
    M.notify = 1;
}

static void m_swapped(void *ctx, int key)
{
    (void)ctx;
    M.badge_key = key;
}

typedef struct {
    int stale_vars, frames, tears, wrong_slot, badge_off, open_mismatch, returns, return_mismatch, beats, beat_open, ota_own, ota_unswapped;
    double worst_open;
} model_log_t;

static void model_run(uint64_t seed, int race_pct, model_log_t *g)
{
    static const rim_slots_ops_t ops = {.show = m_show, .wake = m_wake, .swapped = m_swapped};
    memset(&M, 0, sizeof M);
    pthread_mutex_init(&M.mu, NULL);
    pthread_cond_init(&M.cv, NULL);
    M.rng = seed * 0x9E3779B97F4A7C15ULL + 1;
    M.race_pct = race_pct;
    M.raster_frames = 1 + (int)(seed % 7);
    M.badge_key = -1;
    void *bufs[RIM_SLOTS];
    for (int v = 0; v < RIM_SLOTS; v++) {
        M.bufs[v].key = -1;
        bufs[v] = &M.bufs[v];
    }
    rim_slots_init(&M.s, bufs);
    s_me = 0;
    s_rj_hook = m_race;
    pthread_t th;
    pthread_create(&th, NULL, m_task, NULL);

    bot_t b;
    bot_init(&b, seed, 0);
    bot_set_sleep_after(&b, 0);
    uint64_t r = seed * 2654435761ULL + 7;
#define RND(n) ((r = r * 6364136223846793005ULL + 1442695040888963407ULL), (double)(r >> 11) / (double)(1ULL << 53) * (n))
    double next_mood = 1, page_off = 2 + RND(4), page_on = -1, ota_at = 3 + RND(6), ota_end = -1;
    bool on_page = true, ota = false, held = false, back = false;
    bot_mood_t last_face = b.face;
    float ratio = 1;
    double quiet_from = 0;
    int last_job = -1;
    for (double t = 0; t < 40; t += 1.0 / 60) {
        g->frames++;
        if (t >= next_mood) {
            bot_set_mood(&b, (bot_mood_t)((int)RND(6) % 6), t);
            next_mood = t + (RND(1) < 0.3 ? 2 + RND(2) : 0.03 + RND(1.5));
        }
        if (on_page && t >= page_off) {
            on_page = false;
            page_on = t + 0.05 + RND(2);
        } else if (!on_page && t >= page_on) {
            on_page = back = true;
            page_off = t + 1 + RND(5);
        }
        if (!ota && t >= ota_at) {
            ota = true;
            ota_end = t + 0.05 + RND(3);
        } else if (ota && t >= ota_end) {
            ota = false;
            ota_at = t + 2 + RND(7);
        }
        if (ota) {
            if (!held) {
                held = true;
                g->ota_own += atomic_load(&M.s.own);
                g->ota_unswapped += !atomic_load(&M.s.own);
                rim_slots_hold(&M.s, true, &ops);
            }
            m_give(1);
            continue;
        }
        if (held) {
            quiet_from = t;
            held = false;
            rim_slots_hold(&M.s, false, &ops);
        }
        M.off_frames = on_page ? 0 : M.off_frames + 1;
        if (back || M.s.beat.job != last_job) quiet_from = t;
        last_job = M.s.beat.job;
        bot_rim_ready(&b, rim_slots_feed(&M.s, bot_rim_key(b.mood, b.asleep), on_page, &ops));
        bot_pose_t p = bot_pose(&b, t);
        bool beat = p.face != last_face;
        last_face = p.face;
        int face = bot_rim_key(p.face, p.asleep);
        if (on_page) ratio = (float)(p.scale_y / p.scale_x);
        int v = rim_slots_sync(&M.s, bot_rim_key(p.mood, p.asleep), face, ratio, t, &ops);
        if (v != M.s.shown) rim_slots_show(&M.s, v, &ops);
        if (on_page) {
            fake_buf_t *c = M.canvas;
            if (c) {
                g->tears += c->writing;
                g->wrong_slot += c != M.s.buf[M.s.shown] || c->sx != M.s.sx[M.s.shown] || c->sy != M.s.sy[M.s.shown];
                g->badge_off += c->key != M.badge_key;
                bool mismatch = c->key != face;
                if (mismatch && lid_max(&p) < 0.9) {
                    g->open_mismatch++;
                    g->worst_open = fmax(g->worst_open, t - b.mood_since);
                }
                if (back) g->return_mismatch += mismatch;
                if (beat) {
                    g->beats++;
                    g->beat_open += lid_max(&p) < 0.9;
                }
            }
            unsigned gen = rim_job_gen(&M.s.job);
            g->stale_vars += t - quiet_from > 1.5 && t - b.mood_since > 1.5 && M.s.beat.job == last_job &&
                             (atomic_load(&M.s.var_gen[RIM_SLOT_BASE - 1]) != gen || atomic_load(&M.s.var_gen[RIM_SLOT_BASE + 1]) != gen);
            g->returns += back;
            back = false;
        }
        m_give(1);
    }
#undef RND
    pthread_mutex_lock(&M.mu);
    M.quit = true;
    M.turn = 1;
    pthread_cond_broadcast(&M.cv);
    pthread_mutex_unlock(&M.mu);
    pthread_join(th, NULL);
    s_rj_hook = NULL;
    pthread_cond_destroy(&M.cv);
    pthread_mutex_destroy(&M.mu);
}

static void test_rim_slots_model(void)
{
    model_log_t g = {0};
    int replaced = 0, races = 0, off_vars = 0, on_vars = 0;
    for (int seed = 1; seed <= 80; seed++) {
        model_run((uint64_t)seed, seed % 5 == 0 ? 0 : 35, &g);
        replaced += M.replaced;
        races += M.races;
        off_vars += M.off_vars;
        on_vars += M.on_vars;
    }
    printf("rim slots model: %d variant rasters on the bot page, %d started off it, %d frames on it without -1 and +1 1.5 s after a "
           "change, return or hold\n", on_vars, off_vars, g.stale_vars);
    CHECK(!off_vars && on_vars > 1000, "off the bot page only bases render; the variants wait for the page (%d started off it)", off_vars);
    CHECK(!g.stale_vars, "back on the page the variants follow (%d frames without -1 and +1)", g.stale_vars);
    printf("rim slots model: %d frames, %d beats, %d page returns, %d OTA holds swapped and %d not, %d rasters replaced mid-way, %d race "
           "switches; %d tears, %d wrong slots, %d badge off, %d open mismatches (worst %.2f s after a change), %d on return, %d beats open\n",
           g.frames, g.beats, g.returns, g.ota_own, g.ota_unswapped, replaced, races, g.tears, g.wrong_slot, g.badge_off, g.open_mismatch,
           g.worst_open, g.return_mismatch, g.beat_open);
    CHECK(g.beats > 500 && g.returns > 50 && g.ota_own > 20 && g.ota_unswapped > 20 && replaced > 50 && races > 1000,
          "the model covers beats, page returns, holds before and after the swap, and replacement mid-raster");
    CHECK(!g.tears, "the shown outline is never being rendered (%d frames)", g.tears);
    CHECK(!g.wrong_slot, "the canvas shows the slot's own variant (%d frames)", g.wrong_slot);
    CHECK(!g.badge_off, "badge and label follow the outline on screen (%d frames)", g.badge_off);
    CHECK(!g.beat_open, "every beat behind the lids (%d)", g.beat_open);
    CHECK(!g.return_mismatch, "back on the bot page: the outline matches the eyes in the first frame (%d)", g.return_mismatch);
    CHECK(!g.open_mismatch, "never the face's eyes with another outline on open lids (%d frames)", g.open_mismatch);
}

int main(void)
{
    test_rim_beat();
    test_mood_beat();
    test_mood_beat_gaps();
    test_mood_beat_view();
    test_rim_slots_model();
    test_done_reward();
    test_error_shake();
    test_error_huff();
    test_error_gaze();
    test_error_exit_continuity();
    test_error_overlaps();
    test_beat_hidden();
    test_wait_phases();
    test_wait_nudge();
    test_wait_restart();
    test_wait_hop_chain();
    test_wait_demo();
    test_reading();
    test_reading_redraws();
    test_reading_open_steps();
    test_reading_other_moods();
    test_reading_moment();
    test_reading_blink_phases();
    test_reading_entry();
    test_coupled_blink_expired();
    test_reading_moment_glide();
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
    test_asleep_guards();
    test_sleepy_transitions();
    test_sleepy_still_redraws();
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
    test_tilt_mirror();
    test_lid_shape();
    test_lid_line();
    printf(failures ? "%d FAILED\n" : "all bot tests passed\n", failures);
    return failures ? 1 : 0;
}
