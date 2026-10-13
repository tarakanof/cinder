#include <stdio.h>
#include <string.h>

#include "refr_stats.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void refresh(refr_stats_t *s, int kind, int areas, int32_t side, int64_t us)
{
    refr_area_t a[8];
    for (int i = 0; i < areas; i++) a[i] = (refr_area_t){i * 100, 0, i * 100 + side - 1, side - 1};
    refr_stats_begin(s, kind);
    refr_stats_render(s, a, NULL, areas);
    refr_stats_end(s, us);
}

static void test_window_totals(void)
{
    static refr_stats_t s;
    refr_kind_t k[REFR_KINDS], t;
    refr_stats_init(&s, 0);
    refresh(&s, 1, 2, 20, 4000);
    refresh(&s, 1, 3, 20, 6000);
    refresh(&s, 2, 5, 10, 2000);
    refresh(&s, 3, 1, 10, 9000);
    int64_t win = refr_stats_take(&s, 30000000, k, &t);
    CHECK(win == 30000000, "window %lld us", (long long)win);
    CHECK(k[1].n == 2 && k[1].areas == 5 && k[1].px == 2000 && k[1].areas_max == 3, "pose: n %d areas %lld px %lld max %d",
          k[1].n, (long long)k[1].areas, (long long)k[1].px, k[1].areas_max);
    CHECK(k[1].us == 10000 && k[1].us_max == 6000, "pose us %lld max %lld", (long long)k[1].us, (long long)k[1].us_max);
    CHECK(k[2].areas == 5 && k[3].areas == 1 && k[0].n == 0, "glint %lld pose+glint %lld other %d", (long long)k[2].areas,
          (long long)k[3].areas, k[0].n);
    CHECK(t.n == 4 && t.areas == 11 && t.px == 2600, "total n %d areas %lld px %lld", t.n, (long long)t.areas,
          (long long)t.px);
    CHECK(t.us == 21000 && t.us_max == 9000 && t.areas_max == 5, "total us %lld max %lld areas max %d", (long long)t.us,
          (long long)t.us_max, t.areas_max);
}

static void test_joined_areas_skipped(void)
{
    static refr_stats_t s;
    refr_kind_t k[REFR_KINDS], t;
    refr_stats_init(&s, 0);
    const refr_area_t a[3] = {{0, 0, 19, 9}, {10, 0, 29, 9}, {100, 100, 109, 109}};
    const uint8_t joined[3] = {0, 1, 0};
    refr_stats_begin(&s, 1);
    refr_stats_render(&s, a, joined, 3);
    CHECK(refr_stats_end(&s, 100), "refresh not counted");
    refr_stats_take(&s, 1, k, &t);
    CHECK(t.areas == 2 && t.px == 300 && t.areas_max == 2, "joined area counted: areas %lld px %lld", (long long)t.areas,
          (long long)t.px);
}

static void test_move_only_refresh(void)
{
    static refr_stats_t s;
    refr_kind_t k[REFR_KINDS], t;
    refr_stats_init(&s, 0);
    const refr_area_t dest = {200, 200, 239, 239};
    refr_stats_begin(&s, 1);
    refr_stats_render(&s, &dest, NULL, 1);
    CHECK(refr_stats_end(&s, 100), "a refresh whose only area came from layout was dropped");
    refr_stats_take(&s, 1, k, &t);
    CHECK(t.n == 1 && t.px == 1600, "move destination: n %d px %lld", t.n, (long long)t.px);
}

static void test_take_resets(void)
{
    static refr_stats_t s;
    refr_kind_t k[REFR_KINDS], t;
    refr_stats_init(&s, 5000000);
    refresh(&s, 0, 4, 10, 1000);
    int64_t w1 = refr_stats_take(&s, 35000000, k, &t);
    CHECK(t.areas == 4 && w1 == 30000000, "first window %lld areas, %lld us", (long long)t.areas, (long long)w1);
    int64_t w2 = refr_stats_take(&s, 67000000, k, &t);
    CHECK(w2 == 32000000, "second window measured from the first take: %lld us", (long long)w2);
    CHECK(t.n == 0 && t.areas == 0 && t.px == 0 && t.us_max == 0, "second window not empty: %d %lld", t.n,
          (long long)t.areas);
    const refr_area_t a = {0, 0, 9, 19};
    refr_stats_begin(&s, 1);
    refr_stats_render(&s, &a, NULL, 1);
    refr_stats_take(&s, 70000000, k, &t);
    CHECK(t.n == 0, "a refresh in flight counts in the window it ends in");
    CHECK(refr_stats_end(&s, 500), "refresh in flight lost at the window boundary");
    refr_stats_take(&s, 71000000, k, &t);
    CHECK(t.n == 1 && t.areas == 1 && k[1].px == 200, "boundary refresh: n %d areas %lld", t.n, (long long)t.areas);
}

static void test_empty_and_bad_kind(void)
{
    static refr_stats_t s;
    refr_kind_t k[REFR_KINDS], t;
    refr_stats_init(&s, 0);
    refr_stats_begin(&s, 1);
    CHECK(!refr_stats_end(&s, 700), "a refresh with no render counted");
    const refr_area_t bad = {10, 10, 9, 20};
    refr_stats_begin(&s, 1);
    refr_stats_render(&s, &bad, NULL, 1);
    CHECK(!refr_stats_end(&s, 700), "an empty area counted");
    CHECK(!refr_stats_end(&s, 700), "an end without a start counted");
    refresh(&s, 7, 2, 5, 100);
    refresh(&s, -1, 1, 5, 100);
    refr_stats_take(&s, 1, k, &t);
    CHECK(t.n == 2 && k[0].n == 2 && k[0].areas == 3, "out-of-range kinds go to other: n %d other %d areas %lld", t.n,
          k[0].n, (long long)k[0].areas);
}

int main(void)
{
    test_window_totals();
    test_joined_areas_skipped();
    test_move_only_refresh();
    test_take_resets();
    test_empty_and_bad_kind();
    if (failures) {
        printf("refr: %d failures\n", failures);
        return 1;
    }
    printf("refr: all tests passed\n");
    return 0;
}
