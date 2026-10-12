#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wx_moment.h"
#include "wx_paint.h"

#define wxm_clouds ref_wxm_clouds
#define wxm_layout ref_wxm_layout
#define wxm_paint ref_wxm_paint
#include "ref_moment_paint.c"
#undef wxm_clouds
#undef wxm_layout
#undef wxm_paint

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *const KIND[WXM_KINDS] = {"none", "glance", "night", "frost", "heat", "wind", "afterrain", "snow", "rain", "storm"};
static const wxm_kind_t ALL[] = {WXM_GLANCE, WXM_NIGHT, WXM_FROST, WXM_HEAT, WXM_WIND, WXM_AFTERRAIN, WXM_SNOW, WXM_RAIN, WXM_STORM};
#define N_ALL ((int)(sizeof ALL / sizeof ALL[0]))

static uint8_t s_got[WXM_ARENA + 16], s_ref[WXM_ARENA + 16];

static wxm_moment_t moment(wxm_kind_t k, bool sky, int phase, int illum)
{
    wxm_moment_t m = {.kind = k, .sun = sky, .moon = sky, .moon_phase = phase, .moon_illum = illum};
    snprintf(m.text, sizeof m.text, "x");
    return m;
}

static int s_rows, s_calls, s_most, s_since, s_pixels, s_last_y;
static const void *s_last_layer;

void wxm_test_pixel(const void *layer, int y)
{
    s_pixels++;
    if (layer == s_last_layer && y == s_last_y) return;
    s_last_layer = layer;
    s_last_y = y;
    if (++s_since > s_most) s_most = s_since;
}

static void count_yield(void)
{
    s_calls++;
    s_since = 0;
    s_last_layer = NULL;
}

static int rows_of(const wxm_layout_t *l)
{
    int n = 0;
    for (int i = 0; i < l->nl; i++) n += l->layer[i].h;
    return n;
}

static void compare(const wxm_moment_t *m, const char *what)
{
    wxm_layout_t l, rl;
    wxm_layout(m, &l);
    ref_wxm_layout(m, &rl);
    CHECK(!memcmp(&l, &rl, sizeof l), "%s %s: layout as before", KIND[m->kind], what);
    for (int fillv = 0; fillv < 2; fillv++) {
        memset(s_got, fillv ? 0xFF : 0, sizeof s_got);
        memset(s_ref, fillv ? 0xFF : 0, sizeof s_ref);
        s_calls = s_most = s_since = s_pixels = 0;
        s_last_layer = NULL;
        wxm_paint(m, &l, s_got, count_yield);
        ref_wxm_paint(m, &rl, s_ref);
        int diff = -1;
        for (int i = 0; i < l.total && diff < 0; i++)
            if (s_got[i] != s_ref[i]) diff = i;
        CHECK(diff < 0, "%s %s: byte %d of %d differs from the old painter (%02X, want %02X)", KIND[m->kind], what, diff, l.total,
              diff < 0 ? 0 : s_got[diff], diff < 0 ? 0 : s_ref[diff]);
        CHECK(s_got[l.total] == (fillv ? 0xFF : 0), "%s %s: nothing past the layout", KIND[m->kind], what);
        CHECK(s_calls >= rows_of(&l), "%s %s: %d yield calls, want one per row at least (%d)", KIND[m->kind], what, s_calls, rows_of(&l));
        CHECK(s_pixels > 0, "%s %s: no pixel evaluated through design_at", KIND[m->kind], what);
        CHECK(s_most <= 1, "%s %s: %d layer rows evaluated between two yield calls, want one at most", KIND[m->kind], what, s_most);
        s_rows += rows_of(&l);
    }
}

static void test_same(void)
{
    for (int a = 0; a < N_ALL; a++)
        for (int sky = 0; sky < 2; sky++) {
            wxm_moment_t m = moment(ALL[a], sky, 30, 60);
            compare(&m, sky ? "with sun/moon" : "without");
        }
    for (int p = 0; p < 2; p++)
        for (int illum = 0; illum <= 100; illum++) {
            char what[48];
            snprintf(what, sizeof what, "phase %d illum %d", p ? 80 : 30, illum);
            wxm_moment_t m = moment(WXM_NIGHT, true, p ? 80 : 30, illum);
            compare(&m, what);
        }
}

static void test_no_hook(void)
{
    wxm_moment_t m = moment(WXM_RAIN, false, 0, 0);
    wxm_layout_t l;
    wxm_layout(&m, &l);
    memset(s_got, 0, sizeof s_got);
    memset(s_ref, 0, sizeof s_ref);
    wxm_paint(&m, &l, s_got, NULL);
    wxm_paint(&m, &l, s_ref, count_yield);
    CHECK(!memcmp(s_got, s_ref, (size_t)l.total), "a NULL hook paints the same");
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static void bench(void)
{
    const char *b = getenv("CINDER_BENCH");
    bool on = b && *b && strcmp(b, "0");
    int reps = on ? 30 : 1;
    double got_all = 0, ref_all = 0;
    for (int a = 0; a < N_ALL; a++) {
        wxm_moment_t m = moment(ALL[a], true, 30, 60);
        wxm_layout_t l;
        wxm_layout(&m, &l);
        double got = 1e9, ref = 1e9;
        for (int k = 0; k < reps; k++) {
            double t0 = now_ms();
            wxm_paint(&m, &l, s_got, NULL);
            double t1 = now_ms();
            ref_wxm_paint(&m, &l, s_ref);
            double t2 = now_ms();
            if (t1 - t0 < got) got = t1 - t0;
            if (t2 - t1 < ref) ref = t2 - t1;
        }
        got_all += got;
        ref_all += ref;
        if (on) printf("moment paint %-9s %7.3f ms, old painter %7.3f ms (x%.1f, best of %d)\n", KIND[m.kind], got, ref, ref / got, reps);
        if (on && m.kind == WXM_RAIN) CHECK(got * 4 < ref, "CINDER_BENCH: the rain bake is well under a quarter of the old one");
    }
    printf("moment paint: all kinds %.2f ms, old painter %.2f ms on the host\n", got_all, ref_all);
}

int main(void)
{
    test_same();
    test_no_hook();
    bench();
    if (failures) {
        printf("moment paint: %d failure(s)\n", failures);
        return 1;
    }
    printf("moment paint: all passed (%d rows compared)\n", s_rows);
    return 0;
}
