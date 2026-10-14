#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bot_behavior.h"
#include "bot_shape.h"
#include "music_moment.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define CX 236.0
#define CY 233.0
#define BODY_R (233.0 * 0.84)
#define HOP_SCALE 0.45
#define EYE_SCALE 1.15

static void test_key(void)
{
    uint32_t a = mm_track_key("Glass Harbor", "Northern Static", "Low Tide Radio", "Plex");
    CHECK(a == mm_track_key("Glass Harbor", "Northern Static", "Low Tide Radio", "Plex"), "same track, same key");
    CHECK(a != mm_track_key("Glass Harbor", "Northern Static", "Low Tide Radio", "Apple Music"), "another source");
    CHECK(a != mm_track_key("Paper Moons", "Northern Static", "Low Tide Radio", "Plex"), "another title");
    CHECK(mm_track_key("ab", "", "", "") != mm_track_key("a", "b", "", ""), "fields do not run together");
    CHECK(mm_track_key("", "x", "y", "Plex") == 0 && mm_track_key(NULL, NULL, NULL, NULL) == 0, "no title: 0");
    CHECK(mm_track_key("a", "", "", "") != 0, "a title alone");
}

typedef struct {
    mm_trigger_t tr;
    mm_in_t in;
    int shows;
    double shown_at;
} rig_t;

static void rig(rig_t *r)
{
    memset(r, 0, sizeof *r);
    mm_init(&r->tr);
    r->in = (mm_in_t){.have = true, .bot_mood = BOT_IDLE, .rest = true, .shown = true, .other_at = -INFINITY};
}

static void run(rig_t *r, double until)
{
    for (; r->in.t < until; r->in.t += 1.0 / 60) {
        if (mm_tick(&r->tr, &r->in)) {
            mm_shown(&r->tr, r->in.t);
            r->shows++;
            r->shown_at = r->in.t;
        }
    }
}

static void test_trigger(void)
{
    uint32_t A = mm_track_key("A", "x", "y", "Plex"), B = mm_track_key("B", "x", "y", "Plex"), C = mm_track_key("C", "x", "y", "Plex");
    rig_t r;

    rig(&r);
    r.in.playing = true;
    r.in.track = A;
    run(&r, 60);
    CHECK(r.shows == 0, "playing at boot is the baseline: %d", r.shows);
    r.in.track = B;
    run(&r, 120);
    CHECK(r.shows == 1 && fabs(r.shown_at - 60) < 0.02, "a new track: shown at %.2f", r.shown_at);

    rig(&r);
    run(&r, 10);
    r.in.playing = true;
    r.in.track = A;
    run(&r, 20);
    CHECK(r.shows == 1 && fabs(r.shown_at - 10) < 0.02, "music starts: shown at %.2f (%d)", r.shown_at, r.shows);
    r.in.track = B;
    run(&r, 600);
    CHECK(r.shows == 1, "a track change within %.0f s is absorbed", MM_SAME_S);
    run(&r, 1250);
    CHECK(r.shows == 1, "the absorbed track stays seen after the cooldown");
    r.in.track = C;
    run(&r, 1300);
    CHECK(r.shows == 2 && fabs(r.shown_at - 1250) < 0.02, "after the cooldown the next track shows (%d at %.2f)", r.shows, r.shown_at);

    rig(&r);
    run(&r, 1);
    r.in.playing = true;
    r.in.track = A;
    r.in.rest = false;
    r.in.bot_mood = BOT_WORKING;
    run(&r, 300);
    CHECK(r.shows == 0, "not while working");
    r.in.rest = true;
    r.in.bot_mood = BOT_DONE;
    run(&r, 305);
    CHECK(r.shows == 0, "waits %.0f s of a steady mood", MM_SETTLE_S);
    run(&r, 310);
    CHECK(r.shows == 1 && fabs(r.shown_at - 306) < 0.02, "then plays the waiting track (%.2f)", r.shown_at);

    rig(&r);
    run(&r, 1);
    r.in.playing = true;
    r.in.track = A;
    r.in.shown = false;
    run(&r, 100);
    r.in.shown = true;
    run(&r, 103);
    CHECK(r.shows == 0, "the page shown restarts the settle");
    run(&r, 110);
    CHECK(r.shows == 1, "then it plays");

    rig(&r);
    run(&r, 1);
    r.in.playing = true;
    r.in.track = A;
    r.in.blocked = true;
    run(&r, 100);
    CHECK(r.shows == 0, "blocked");
    r.in.blocked = false;
    r.in.other_at = 90;
    run(&r, 389);
    CHECK(r.shows == 0, "within %.0f s of a weather moment", MM_GAP_S);
    run(&r, 391);
    CHECK(r.shows == 1, "after the gap");
    CHECK(mm_recent(&r.tr, r.shown_at + MM_GAP_S - 1) && !mm_recent(&r.tr, r.shown_at + MM_GAP_S + 1), "weather waits the same gap");

    rig(&r);
    run(&r, 1);
    r.in.playing = true;
    r.in.track = A;
    run(&r, 10);
    CHECK(r.shows == 1, "first start");
    r.in.playing = false;
    run(&r, 2000);
    r.in.playing = true;
    run(&r, 2010);
    CHECK(r.shows == 2, "the same track after a long stop shows again (%d)", r.shows);
    r.in.playing = false;
    run(&r, 2100);
    r.in.playing = true;
    run(&r, 3400);
    CHECK(r.shows == 2, "a short pause resumes without a moment (%d)", r.shows);

    rig(&r);
    r.in.have = false;
    r.in.playing = true;
    r.in.track = A;
    run(&r, 50);
    r.in.have = true;
    run(&r, 70);
    CHECK(r.shows == 0, "the first block seen is the baseline, also after a gap");

    rig(&r);
    run(&r, 1);
    r.in.playing = true;
    r.in.track = mm_track_key("", "x", "y", "Plex");
    run(&r, 30);
    CHECK(r.shows == 0, "an empty title, no moment");
    r.in.track = A;
    run(&r, 40);
    CHECK(r.shows == 1, "then the titled track");

    rig(&r);
    r.in.playing = true;
    r.in.track = A;
    run(&r, 10);
    r.in.track = B;
    r.in.rest = false;
    r.in.bot_mood = BOT_WORKING;
    run(&r, 60);
    r.in.playing = false;
    run(&r, 90);
    r.in.playing = true;
    r.in.rest = true;
    r.in.bot_mood = BOT_IDLE;
    run(&r, 200);
    CHECK(r.shows == 0, "a track still waiting for its moment, paused briefly: no moment on the resume (%d)", r.shows);

    rig(&r);
    r.in.playing = true;
    r.in.track = A;
    run(&r, 10);
    r.in.playing = false;
    run(&r, 20);
    r.in.have = false;
    run(&r, 1000);
    r.in.have = true;
    run(&r, 1010);
    r.in.playing = true;
    run(&r, 1020);
    r.in.playing = false;
    run(&r, 1021);
    r.in.playing = true;
    run(&r, 1100);
    CHECK(r.shows == 0, "a gap without the block resolves the stop: the next block is a baseline (%d)", r.shows);

    rig(&r);
    r.in.playing = true;
    r.in.track = A;
    run(&r, 10);
    r.in.playing = false;
    run(&r, 20);
    r.in.track = B;
    run(&r, 40);
    r.in.playing = true;
    run(&r, 60);
    CHECK(r.shows == 1, "a track skipped to while paused, then played within %.0f s: a moment (%d)", MM_RESUME_S, r.shows);

    rig(&r);
    r.in.playing = true;
    r.in.track = A;
    run(&r, 10);
    r.in.have = false;
    run(&r, 110);
    r.in.have = true;
    run(&r, 200);
    CHECK(r.shows == 0, "a demo (no block for the trigger) then the same track: no moment (%d)", r.shows);

    rig(&r);
    r.in.playing = false;
    r.in.track = A;
    run(&r, 10);
    r.in.playing = true;
    run(&r, 130);
    CHECK(r.shows == 0, "paused at boot, resumed within %.0f s: no moment, as when playing at boot (%d)", MM_RESUME_S, r.shows);
    r.in.playing = false;
    run(&r, 800);
    r.in.playing = true;
    run(&r, 820);
    CHECK(r.shows == 1, "resumed after a long pause: a moment");
}

static void test_frames(void)
{
    mm_frame_t f, prev;
    int opa_prev = -1, in_last = -1;
    float lo = 0, hi = 0;
    int cup_lo = 0, cup_hi = 0, notes_on = 0;
    for (int j = 0; j < MM_TICKS; j++) {
        mm_frame(j, &f);
        if (j < MM_IN) {
            CHECK(f.phones_opa >= opa_prev && f.phones_dy <= 0 && f.phones_dy >= -MM_DROP, "in %d: opa %d, dy %d", j, f.phones_opa, f.phones_dy);
            opa_prev = f.phones_opa;
            in_last = j;
        } else if (j < MM_OUT) {
            CHECK(f.phones_opa == 255 && f.phones_dy == 0, "hold %d", j);
        } else {
            CHECK(f.phones_opa <= opa_prev && f.phones_dy <= 0, "out %d: opa %d", j, f.phones_opa);
            opa_prev = f.phones_opa;
        }
        if (j % 2 == 1) {
            CHECK(prev.phones_opa == f.phones_opa && prev.phones_dy == f.phones_dy, "phones step on the 15 fps grid only (%d)", j);
            CHECK(prev.eye_px == f.eye_px && prev.cup_dy == f.cup_dy, "the bob steps at 15 fps (%d)", j);
            for (int i = 0; i < MM_NOTES; i++)
                CHECK(memcmp(&prev.note[i], &f.note[i], sizeof f.note[i]) == 0, "notes step on the 15 fps grid only (%d)", j);
        }
        lo = fminf(lo, f.eye_px);
        hi = fmaxf(hi, f.eye_px);
        if (f.cup_dy < cup_lo) cup_lo = f.cup_dy;
        if (f.cup_dy > cup_hi) cup_hi = f.cup_dy;
        for (int i = 0; i < MM_NOTES; i++) {
            if (f.note[i].opa <= 0) continue;
            notes_on++;
            CHECK(j >= MM_IN && j < MM_OUT, "note %d at tick %d outside the hold", i, j);
        }
        prev = f;
    }
    CHECK(in_last == MM_IN - 1, "arrival ticks");
    mm_frame(MM_IN - 1, &f);
    CHECK(f.phones_opa == 255 && f.phones_dy == 0, "arrived: opa %d dy %d", f.phones_opa, f.phones_dy);
    mm_frame(MM_TICKS - 1, &f);
    CHECK(f.phones_opa == 0, "gone at the last tick: %d", f.phones_opa);
    mm_frame(MM_TICKS, &f);
    mm_frame_t none = {0};
    CHECK(memcmp(&f, &none, sizeof f) == 0, "nothing after the end");
    CHECK(fabsf(lo + 5) < 0.01f && hi > 3.8f && hi <= 4 && cup_lo == -MM_CUP_BOB && cup_hi == MM_CUP_BOB, "bob %.2f..%.2f px, cups %d..%d", lo, hi,
          cup_lo, cup_hi);
    mm_frame(MM_IN, &f);
    CHECK(f.eye_px == 0 && f.cup_dy == 0, "the bob starts at rest");
    mm_frame(MM_OUT - 1, &f);
    CHECK(fabsf(f.eye_px) < 1 && f.cup_dy == 0, "the bob ends near rest (%.2f)", f.eye_px);
    mm_frame(MM_IN + MM_BEAT / 4 + 1, &f);
    CHECK(f.eye_px > 3.5f, "down on the beat's first quarter (%.2f)", f.eye_px);
    CHECK(MM_BEAT * 1.0 / MM_FPS == 0.6, "0.6 s beat");
    CHECK(notes_on > 0, "notes rise");
    int cycles = 0;
    for (int j = MM_IN; j < MM_OUT; j += 2) {
        mm_frame(j, &f);
        mm_frame_t g;
        mm_frame(j - 2, &g);
        for (int i = 0; i < MM_NOTES; i++) cycles += f.note[i].opa > 0 && g.note[i].opa == 0;
    }
    printf("music moment: %d ticks (%.2f s + %.1f s pre-roll), bob %.0f..%.0f px eyes / %d px cups, %d note flights\n", MM_TICKS,
           MM_TICKS / (double)MM_FPS, MM_PRE_S, lo, hi, MM_CUP_BOB, cycles);
    CHECK(cycles == 7, "seven note flights: %d", cycles);
}

static long box_px(const mm_box_t *b) { return (long)(b->x1 - b->x0 + 1) * (b->y1 - b->y0 + 1); }

static bool inside(const mm_box_t *in, const mm_box_t *out)
{
    return in->x0 >= out->x0 && in->y0 >= out->y0 && in->x1 <= out->x1 && in->y1 <= out->y1;
}

static void test_areas(void)
{
    mm_box_t ext;
    mm_extent(&ext);
    CHECK(ext.x0 % 2 == 0 && ext.x1 % 2 == 1 && ext.y0 % 2 == 0 && ext.y1 % 2 == 1, "extent aligned");
    mm_frame_t a, b;
    long max = 0, frames = 0;
    int max_j = -1;
    for (int j = 0; j <= MM_TICKS; j++) {
        mm_frame(j, &b);
        mm_box_t box;
        bool any = mm_diff(j ? &a : NULL, &b, &box);
        if (j == 0) CHECK(any, "the first tick draws");
        if (j == MM_TICKS) CHECK(!any && a.phones_opa == 0, "the last tick already shows nothing");
        if (any) {
            CHECK(inside(&box, &ext), "tick %d: box inside the extent", j);
            CHECK(box.x0 % 2 == 0 && box.x1 % 2 == 1 && box.y0 % 2 == 0 && box.y1 % 2 == 1, "tick %d aligned", j);
            long px = box_px(&box);
            frames++;
            if (px > max) max = px, max_j = j;
        }
        a = b;
    }
    mm_frame(MM_IN + 3, &a);
    mm_frame(MM_IN + 3, &b);
    mm_box_t box;
    CHECK(!mm_diff(&a, &b, &box), "the same frame: nothing");
    long hold = 0, hold_n = 0;
    for (int j = MM_IN + 1; j < MM_OUT; j++) {
        mm_frame(j - 1, &a);
        mm_frame(j, &b);
        if (mm_diff(&a, &b, &box)) hold += box_px(&box), hold_n++;
    }
    printf("music moment areas: 1 box per tick, %ld redraws over %d ticks, max %ld px (tick %d), hold avg %ld px, extent (%d,%d)-(%d,%d) %ld px\n",
           frames, MM_TICKS, max, max_j, hold_n ? hold / hold_n : 0, ext.x0, ext.y0, ext.x1, ext.y1, box_px(&ext));
    CHECK(max <= box_px(&ext), "max box");
}

static void test_abort(void)
{
    float prev = -5;
    for (int k = 0; k < 12; k++) {
        float e = mm_eye_settle(-5, k);
        CHECK(e >= prev && e <= 0 && e - prev <= 1.3f, "settle tick %d: %.2f after %.2f", k, e, prev);
        if (k % 2) CHECK(e == mm_eye_settle(-5, k - 1), "settle steps at 15 fps");
        prev = e;
    }
    CHECK(mm_eye_settle(-5, MM_SETTLE_STEPS * MM_BOB_STEP) == 0 && mm_eye_settle(4, 1) > 0, "settled to 0, from either side");
    for (int j = -1; j < MM_TICKS; j++) {
        mm_frame_t f, g;
        mm_frame(j, &f);
        int k = mm_abort_tick(j);
        CHECK(k >= j, "abort at %d goes on (%d)", j, k);
        if (j < 0) {
            CHECK(k >= MM_TICKS, "before the first tick: over");
            continue;
        }
        if (j >= MM_OUT) {
            CHECK(k == j, "already leaving");
            continue;
        }
        mm_frame(k, &g);
        CHECK(k >= MM_OUT && g.phones_opa <= f.phones_opa, "abort at %d: out tick %d, opa %d <= %d", j, k, g.phones_opa, f.phones_opa);
        for (int i = 0; i < MM_NOTES; i++) CHECK(g.note[i].opa == 0, "notes stop");
        CHECK(g.eye_px == 0 && g.cup_dy == 0, "the bob stops");
    }
}

typedef struct {
    double x0, y0, x1, y1;
} fbox_t;

static fbox_t eye_box(const bot_pose_t *p)
{
    bot_stroke_t st[2];
    bot_eye_strokes(p, EYE_SCALE, st);
    fbox_t b = {1e9, 1e9, -1e9, -1e9};
    double k = BODY_R * (p->scale_x + p->scale_y) / 2;
    for (int e = 0; e < 2; e++) {
        double hw = st[e].width * k / 2 + 1;
        for (int i = 0; i < st[e].n; i++) {
            double by = p->offset_y * HOP_SCALE - (1 - p->scale_y) + p->scale_y * st[e].y[i];
            double sx = CX + BODY_R * (p->offset_x + p->scale_x * st[e].x[i]), sy = CY - BODY_R * by;
            b.x0 = fmin(b.x0, sx - hw);
            b.x1 = fmax(b.x1, sx + hw);
            b.y0 = fmin(b.y0, sy - hw);
            b.y1 = fmax(b.y1, sy + hw);
        }
    }
    return b;
}

static bool overlaps(const fbox_t *e, double x0, double y0, double x1, double y1)
{
    return e->x1 > x0 && e->x0 < x1 + 1 && e->y1 > y0 && e->y0 < y1 + 1;
}

static void test_acting(void)
{
    static const bot_mood_t MOODS[] = {BOT_IDLE, BOT_DONE, BOT_SLEEPY};
    fbox_t all = {1e9, 1e9, -1e9, -1e9};
    int runs = 0, max_areas = 0;
    long eye_frames = 0;
    for (int mi = 0; mi < 3; mi++)
        for (int seed = 0; seed < 60; seed++) {
            bot_t b;
            bot_init(&b, 0x3A5EED00ULL + (uint64_t)seed * 7919 + (uint64_t)mi * 131, 0);
            bot_set_sleep_after(&b, MOODS[mi] == BOT_SLEEPY ? 1 : 0);
            double t = 0, start = 8 + seed * 0.37;
            if (MOODS[mi] != BOT_SLEEPY) bot_set_mood(&b, MOODS[mi], 0.01);
            for (; t < start; t += 1.0 / 60) bot_pose(&b, t);
            bot_mood_t mood = b.mood;
            bot_act_begin(&b, t);
            double t0 = t + MM_PRE_S;
            int n, i = 0, last_j = -1;
            const mm_cue_t *c = mm_script(&n);
            int rim0 = bot_rim_dy(&(bot_pose_t){.scale_x = 1, .scale_y = 1}, BODY_R, HOP_SCALE);
            bot_pose_t last;
            bool have = false;
            mm_frame_t shown;
            for (; t < t0 + (MM_TICKS + 30) / (double)MM_FPS; t += 1.0 / 60) {
                while (i < n && t - t0 >= c[i].t) {
                    if (c[i].op == MM_HAPPY) bot_act_eyes(&b, BOT_EYES_HAPPY, t);
                    else bot_act_end(&b, true, 0.3, t);
                    i++;
                }
                int j = (int)floor((t - t0) * MM_FPS);
                mm_frame_t f;
                mm_frame(j, &f);
                bot_pose_t p = bot_pose(&b, t);
                p.eye_dy -= f.eye_px / BODY_R;
                if (j >= 0 && j < MM_TICKS) {
                    CHECK(bot_rim_dy(&p, BODY_R, HOP_SCALE) == rim0 && fabs(p.scale_y / p.scale_x - 1) < 0.015, "mood %d seed %d tick %d: the outline moves",
                          mood, seed, j);
                    fbox_t e = eye_box(&p);
                    all.x0 = fmin(all.x0, e.x0);
                    all.y0 = fmin(all.y0, e.y0);
                    all.x1 = fmax(all.x1, e.x1);
                    all.y1 = fmax(all.y1, e.y1);
                    CHECK(e.x0 >= MM_BOB_X0 && e.y0 >= MM_BOB_Y0 && e.x1 <= MM_BOB_X1 + 1 && e.y1 <= MM_BOB_Y1 + 1,
                          "mood %d seed %d tick %d: eyes (%.1f,%.1f)-(%.1f,%.1f) outside the bob box", mood, seed, j, e.x0, e.y0, e.x1, e.y1);
                    if (f.phones_opa > 0) {
                        int cy = MM_CUP_Y + f.phones_dy + f.cup_dy;
                        CHECK(!overlaps(&e, MM_CUP_L_X, cy, MM_CUP_L_X + MM_CUP_W - 1, cy + MM_CUP_H - 1) &&
                                  !overlaps(&e, MM_CUP_R_X, cy, MM_CUP_R_X + MM_CUP_W - 1, cy + MM_CUP_H - 1),
                              "mood %d seed %d tick %d: eyes under a cup", mood, seed, j);
                        double band_low = MM_BAND_CY - sqrt((double)MM_BAND_R * MM_BAND_R - fmax(fabs(e.x0 - MM_BAND_CX), fabs(e.x1 - MM_BAND_CX)) *
                                                                                              fmax(fabs(e.x0 - MM_BAND_CX), fabs(e.x1 - MM_BAND_CX))) +
                                          MM_BAND_HW + f.phones_dy;
                        CHECK(e.y0 > band_low, "mood %d seed %d tick %d: eyes reach the band (%.1f, band to %.1f)", mood, seed, j, e.y0, band_low);
                    }
                    for (int q = 0; q < MM_NOTES; q++)
                        if (f.note[q].opa > 0)
                            CHECK(!overlaps(&e, f.note[q].x, f.note[q].y, f.note[q].x + MM_NOTE_W - 1, f.note[q].y + MM_NOTE_H - 1),
                                  "mood %d seed %d tick %d: eyes under a note", mood, seed, j);
                    int areas = 0;
                    bool eyes = have && !bot_pose_same(&p, &last, BODY_R);
                    mm_box_t box = {0};
                    bool music = false;
                    if (j != last_j) {
                        music = mm_diff(last_j >= 0 ? &shown : NULL, &f, &box);
                        shown = f;
                        last_j = j;
                    }
                    areas = music;
                    if (eyes) {
                        eye_frames++;
                        fbox_t pe = eye_box(&last), ne = eye_box(&p);
                        bool in = music && pe.x0 >= box.x0 && ne.x0 >= box.x0 && pe.y0 >= box.y0 && ne.y0 >= box.y0 && pe.x1 <= box.x1 + 1 &&
                                  ne.x1 <= box.x1 + 1 && pe.y1 <= box.y1 + 1 && ne.y1 <= box.y1 + 1;
                        areas += !in;
                    }
                    if (areas > max_areas) max_areas = areas;
                    CHECK(areas <= 1, "mood %d seed %d tick %d: %d areas", mood, seed, j, areas);
                }
                last = p;
                have = true;
            }
            CHECK(!b.acting && b.act_eyes == -1, "mood %d: the act is over", mood);
            bot_pose_t p = bot_pose(&b, t);
            CHECK(b.mood != mood || p.eyes == (mood == BOT_DONE ? BOT_EYES_HAPPY : BOT_EYES_DASH), "mood %d: its eyes back (%d)", mood, p.eyes);
            runs++;
        }
    printf("music moment acting: %d runs, eyes within (%.1f,%.1f)-(%.1f,%.1f), bob box (%d,%d)-(%d,%d), max %d areas per tick (%ld eye redraws)\n", runs,
           all.x0, all.y0, all.x1, all.y1, MM_BOB_X0, MM_BOB_Y0, MM_BOB_X1, MM_BOB_Y1, max_areas, eye_frames);
}

static uint8_t s_arena[MM_ARENA];

static void test_paint(void)
{
    memset(s_arena, 0xA5, sizeof s_arena);
    struct timespec a, b;
    double best = 1e9;
    for (int r = 0; r < 20; r++) {
        clock_gettime(CLOCK_MONOTONIC, &a);
        mm_paint(s_arena);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
        if (ms < best) best = ms;
    }
    const uint8_t *band = s_arena + MM_BAND_AT;
#define BAND(x, y) band[((y) - MM_BAND_Y) * MM_BAND_W + (x) - MM_BAND_X]
    CHECK(BAND(MM_BAND_CX, MM_BAND_CY - MM_BAND_R) == 255, "band top opaque");
    CHECK(BAND(MM_BAND_CX, MM_BAND_CY - MM_BAND_R - 8) == 0 && BAND(MM_BAND_CX, MM_BAND_CY - MM_BAND_R + 8) == 0, "band 12 px");
    CHECK(BAND(MM_BAND_CX, MM_BAND_CY) == 0, "nothing in the middle");
    CHECK(BAND(MM_BAND_CX - MM_BAND_R, MM_BAND_CY + 2) == 255 && BAND(MM_BAND_CX + MM_BAND_R, MM_BAND_CY + 2) == 255, "round caps");
    CHECK(BAND(MM_BAND_CX - MM_BAND_R, MM_BAND_CY + 7) == 0, "caps end");
    int edge = 0;
    for (int y = 0; y < MM_BAND_H; y++) edge += band[y * MM_BAND_W] + band[y * MM_BAND_W + MM_BAND_W - 1];
    for (int x = 0; x < MM_BAND_W; x++) edge += band[x] + band[(MM_BAND_H - 1) * MM_BAND_W + x];
    CHECK(edge == 0, "the band fits its image");
    for (int s = 0; s < 2; s++) {
        const uint16_t *c = (const uint16_t *)(s_arena + MM_CUP_AT(s));
        const uint8_t *al = s_arena + MM_CUP_AT(s) + MM_CUP_W * MM_CUP_H * 2;
        uint16_t soft = (uint16_t)(((0xF2 >> 3) << 11) | ((0xA0 >> 2) << 5) | (0x7E >> 3));
        CHECK(c[36 * MM_CUP_W + 18] == soft && al[36 * MM_CUP_W + 18] == 255, "cup %d: soft colour", s);
        int sx = s == 0 ? 12 : 24;
        CHECK(c[36 * MM_CUP_W + sx] != soft && ((c[36 * MM_CUP_W + sx] >> 11) < (soft >> 11)), "cup %d: the darker stripe outside", s);
        CHECK(al[0] == 0 && al[MM_CUP_W - 1] == 0 && al[36 * MM_CUP_W] == 255, "cup %d: rounded corners", s);
    }
    const uint8_t *note = s_arena + MM_NOTE_AT;
    CHECK(note[MM_NOTE_OY * MM_NOTE_W + MM_NOTE_OX] == 255, "note head");
    CHECK(note[(MM_NOTE_OY - 12) * MM_NOTE_W + MM_NOTE_OX + 5] == 255, "note stem");
    CHECK(note[(MM_NOTE_OY - 15) * MM_NOTE_W + MM_NOTE_OX + 13] > 128, "note flag");
    edge = 0;
    for (int y = 0; y < MM_NOTE_H; y++) edge += note[y * MM_NOTE_W] + note[y * MM_NOTE_W + MM_NOTE_W - 1];
    for (int x = 0; x < MM_NOTE_W; x++) edge += note[x] + note[(MM_NOTE_H - 1) * MM_NOTE_W + x];
    CHECK(edge == 0, "the note fits its image");
    printf("music moment paint: %d B arena, best %.3f ms on the host\n", MM_ARENA, best);
}

int main(void)
{
    test_key();
    test_trigger();
    test_frames();
    test_areas();
    test_abort();
    test_acting();
    test_paint();
    if (failures) {
        printf("test_music_moment: %d failures\n", failures);
        return 1;
    }
    printf("test_music_moment: ok\n");
    return 0;
}
