#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wx_cast.h"
#include "wx_days.h"
#include "wx_moment.h"
#include "wx_paint.h"
#include "wx_temp.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NS 400
#define MAXP (4 * NS + 1)

static double PX[MAXP], PY[MAXP];
static int NP;

static double off_puff(const wx_circle_t *a, const wx_circle_t *b, double t, double *x, double *y)
{
    *x = a->x + a->r * cos(t);
    *y = a->y + a->r * sin(t);
    return hypot(*x - b->x, *y - b->y) - b->r;
}

static void cross(const wx_circle_t *a, const wx_circle_t *b, double *cx, double *cy)
{
    *cx = a->x;
    *cy = 1e9;
    const int N = 7200;
    for (int i = 0; i < N; i++) {
        double lo = -M_PI + 2 * M_PI * i / N, hi = -M_PI + 2 * M_PI * (i + 1) / N, x, y;
        if ((off_puff(a, b, lo, &x, &y) > 0) == (off_puff(a, b, hi, &x, &y) > 0)) continue;
        for (int k = 0; k < 60; k++) {
            double mid = (lo + hi) / 2;
            if ((off_puff(a, b, mid, &x, &y) > 0) == (off_puff(a, b, lo, &x, &y) > 0)) lo = mid;
            else hi = mid;
        }
        off_puff(a, b, lo, &x, &y);
        if (y < *cy) {
            *cx = x;
            *cy = y;
        }
    }
}

static void path(const wx_cloud_t *cl)
{
    double vx[5], vy[5];
    const wx_circle_t *c = cl->c;
    int n = cl->n;
    vx[0] = c[0].x - sqrt((double)c[0].r * c[0].r - ((double)c[0].y - cl->base) * ((double)c[0].y - cl->base));
    vy[0] = cl->base;
    for (int i = 0; i + 1 < n; i++) cross(&c[i], &c[i + 1], &vx[i + 1], &vy[i + 1]);
    vx[n] = c[n - 1].x + sqrt((double)c[n - 1].r * c[n - 1].r - ((double)c[n - 1].y - cl->base) * ((double)c[n - 1].y - cl->base));
    vy[n] = cl->base;
    NP = 0;
    for (int k = 0; k < n; k++) {
        double a0 = atan2(vy[k] - c[k].y, vx[k] - c[k].x), a1 = atan2(vy[k + 1] - c[k].y, vx[k + 1] - c[k].x);
        double sw = fmod(a1 - a0 + 4 * M_PI, 2 * M_PI);
        for (int i = 0; i < NS; i++) {
            double a = a0 + sw * i / NS;
            PX[NP] = c[k].x + c[k].r * cos(a);
            PY[NP++] = c[k].y + c[k].r * sin(a);
        }
    }
    PX[NP] = vx[n];
    PY[NP++] = vy[n];
}

static double seg(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypot(px - ax - t * dx, py - ay - t * dy);
}

static double truth(double x, double y)
{
    double d = 1e9;
    bool in = false;
    for (int i = 0, j = NP - 1; i < NP; j = i++) {
        d = fmin(d, seg(x, y, PX[j], PY[j], PX[i], PY[i]));
        if ((PY[i] > y) != (PY[j] > y) && x < (PX[j] - PX[i]) * (y - PY[i]) / (PY[j] - PY[i]) + PX[i]) in = !in;
    }
    return in ? -d : d;
}

static int s_large;

static void brute(const char *what, const wx_cloud_t *cl, float step)
{
    wx_cloud_shape_t sh;
    CHECK(wx_cloud_shape(cl, &sh), "%s: a valid cloud", what);
    for (int k = 0; k < cl->n; k++) s_large += sh.large[k];
    path(cl);
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f;
    for (int k = 0; k < cl->n; k++) {
        x0 = fminf(x0, cl->c[k].x - cl->c[k].r);
        x1 = fmaxf(x1, cl->c[k].x + cl->c[k].r);
        y0 = fminf(y0, cl->c[k].y - cl->c[k].r);
    }
    double worst = 0;
    int near = 0, far_bad = 0, near_bad = 0;
    for (float y = y0 - 7; y <= cl->base + 7; y += step)
        for (float x = x0 - 7; x <= x1 + 7; x += step) {
            float edge = fabsf(y - cl->base);
            for (int k = 0; k < cl->n; k++) edge = fminf(edge, fabsf(hypotf(x - cl->c[k].x, y - cl->c[k].y) - cl->c[k].r));
            bool coarse = fmodf((x - x0) / step, 4) < 1 && fmodf((y - y0) / step, 4) < 1;
            if (edge > 4.5f && !coarse) continue;
            double t = truth(x, y), d = wx_cloud_sd(&sh, x, y);
            if (fabs(t) < WX_CLOUD_EXACT) {
                near++;
                worst = fmax(worst, fabs(t - d));
                near_bad += fabs(t - d) >= 0.05;
            } else {
                far_bad += (t < 0) != (d < 0) || fabs(d) < WX_CLOUD_EXACT - 0.05;
            }
        }
    CHECK(near > 500 && near_bad == 0, "%s: within 4 of the path off the brute force by %.3f (%d of %d points)", what,
          worst, near_bad, near);
    CHECK(far_bad == 0, "%s: past 4 the sign and the bound hold (%d points)", what, far_bad);
}

static void test_brute(void)
{
    char what[48];
    for (int band = 1; band <= 4; band++) {
        wx_clouds_t c;
        wx_clouds_for(band, false, &c);
        for (int i = 0; i < c.n; i++) {
            snprintf(what, sizeof what, "band %d cloud %d", band, i);
            brute(what, &c.cl[i], 0.37f);
        }
    }
    for (int v = 1; v <= 3; v++)
        for (int side = 0; side < wx_cast_sides(v); side++) {
            wx_clouds_t c;
            wx_cast_clouds(v, side, &c);
            for (int i = 0; i < c.n; i++) {
                snprintf(what, sizeof what, "forecast %d side %d cloud %d", v, side, i);
                brute(what, &c.cl[i], 0.37f);
            }
        }
    for (int icon = 0; icon < WX_ICONS; icon++) {
        const wx_cloud_t *cl = wx_day_cloud(icon);
        if (!cl) continue;
        snprintf(what, sizeof what, "day icon %d", icon);
        brute(what, cl, 0.13f);
    }
    const wx_cloud_t *m[WXM_CLOUDS];
    int nm = wxm_clouds(m);
    CHECK(nm == WXM_CLOUDS, "moment clouds: %d", nm);
    for (int i = 0; i < nm; i++) {
        snprintf(what, sizeof what, "moment cloud %d", i);
        brute(what, m[i], 0.13f);
    }
    CHECK(s_large >= 5, "arcs past 180 degrees covered: %d", s_large);
    static const wx_cloud_t ODD[] = {
        {false, 60, 1, {{50, 50, 20}}},
        {false, 69, 1, {{50, 50, 20}}},
        {false, 40, 1, {{50, 50, 20}}},
        {false, 40, 2, {{30, 50, 15}, {50, 50, 15}}},
        {false, 60, 2, {{30, 50, 25}, {50, 50, 25}}},
        {false, 60, 3, {{30, 50, 20}, {50, 48, 6}, {70, 50, 20}}},
        {false, 60, 2, {{30, 50, 15}, {44, 70, 15}}},
    };
    static const char *ODD_N[] = {"one puff", "one puff, base near its bottom", "one puff, base above its centre",
                                  "base above the centres", "deep overlap",
                                  "a small middle puff just showing", "a low right puff"};
    for (size_t i = 0; i < sizeof ODD / sizeof ODD[0]; i++) brute(ODD_N[i], &ODD[i], 0.29f);
}

static void test_contract(void)
{
    static const struct {
        wx_cloud_t cl;
        const char *what;
    } BAD[] = {
        {{false, 70, 1, {{50, 50, 20}}}, "one puff, base tangent to its bottom"},
        {{false, 80, 1, {{50, 50, 20}}}, "one puff, base below it"},
        {{false, 10, 1, {{0, 0, 10}}}, "one puff, base tangent (Codex)"},
        {{false, 60, 2, {{30, 50, 15}, {80, 50, 15}}}, "puffs apart"},
        {{false, 60, 2, {{30, 50, 15}, {60, 50, 15}}}, "puffs touching"},
        {{false, 60, 2, {{40, 45, 20}, {45, 45, 8}}}, "a puff inside its neighbour"},
        {{false, 5, 2, {{0, 0, 10}, {1, 0, 2}}}, "a puff inside its neighbour (Codex)"},
        {{false, 60, 3, {{30, 50, 15}, {50, 30, 8}, {70, 50, 15}}}, "the middle puff crossing under the others"},
        {{false, 60, 3, {{35, 50, 20}, {50, 48, 6}, {65, 50, 20}}}, "a hidden middle puff"},
        {{false, 60, 2, {{30, 50, 15}, {50, 80, 15}}}, "a puff below the base"},
        {{false, 60, 2, {{30, 50, 15}, {48, 75, 16}}}, "a crossing below the base"},
        {{false, 60, 0, {{0, 0, 0}}}, "no puff"},
        {{false, 60, 5, {{30, 50, 15}, {50, 50, 15}, {70, 50, 15}, {90, 50, 15}}}, "five puffs"},
        {{false, 60, 1, {{50, 50, 0}}}, "a zero radius"},
        {{false, NAN, 1, {{50, 50, 20}}}, "no base"},
        {{false, 58.16f, 3, {{50, 61.12f, 16.97f}, {62.97f, 30.98f, 22.30f}, {66.04f, 45.10f, 29.84f}}}, "a crossing inside a third puff"},
        {{false, 41.26f, 2, {{50, 43.92f, 5.72f}, {52.95f, 63.72f, 25.55f}}}, "the left base end inside its neighbour"},
        {{false, 53, 2, {{50, 65.08f, 22.48f}, {50.96f, 48.45f, 7.78f}}}, "the right base end inside a neighbour"},
        {{false, 73.89f, 3, {{50, 61.34f, 13.20f}, {62.14f, 37.75f, 27.02f}, {84.36f, 57.20f, 17.34f}}}, "crossings out of order"},
        {{false, 77.54f, 3, {{50, 67.99f, 19.07f}, {71.31f, 48.37f, 22.52f}, {80.82f, 63.28f, 14.80f}}}, "the right base end left of the last crossing"},
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        wx_cloud_shape_t sh;
        memset(&sh, 0x5A, sizeof sh);
        bool ok = wx_cloud_shape(&BAD[i].cl, &sh);
        int ink = 0;
        for (float y = -40; y < 120; y += 1)
            for (float x = -40; x < 140; x += 1) ink += wx_cloud_sd(&sh, x, y) < 3;
        CHECK(!ok && sh.np == 0 && ink == 0, "%s: refused, no vertices, no ink (%d np %d ink %d)", BAD[i].what, ok, sh.np,
              ink);
        wx_clouds_t one = {.n = 1, .tone = WX_TONE_WHITE, .fade_from = 195};
        one.cl[0] = BAD[i].cl;
        one.cl[0].n = BAD[i].cl.n > 4 ? 4 : BAD[i].cl.n;
        static uint16_t px[64 * 64];
        static uint8_t a[64 * 64];
        wx_img_t im = {.px = px, .a = a, .w = 64, .h = 64, .x0 = 20, .y0 = 20};
        wx_paint_clouds(&im, &one);
        int lit = 0;
        for (int k = 0; k < 64 * 64; k++) lit += a[k] != 0;
        if (BAD[i].cl.n <= 4) CHECK(lit == 0, "%s: the painter draws nothing (%d px)", BAD[i].what, lit);
    }
}

int main(void)
{
    test_contract();
    test_brute();
    if (failures) {
        printf("cloud: %d failure(s)\n", failures);
        return 1;
    }
    printf("cloud: all tests passed (%d arcs past 180 degrees)\n", s_large);
    return 0;
}
