#include <stdio.h>
#include <string.h>

#include "refr_stats.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void refresh(refr_stats_t *s, int kind, int areas, int64_t px, int64_t us)
{
    refr_stats_start(s, kind, areas, px);
    refr_stats_end(s, us);
}

static void test_window_totals(void)
{
    static refr_stats_t s;
    refr_kind_t k[REFR_KINDS], t;
    memset(&s, 0, sizeof s);
    refresh(&s, 1, 2, 800, 4000);
    refresh(&s, 1, 3, 1200, 6000);
    refresh(&s, 2, 5, 300, 2000);
    refresh(&s, 3, 1, 100, 9000);
    refr_stats_take(&s, k, &t);
    CHECK(k[1].n == 2 && k[1].areas == 5 && k[1].px == 2000 && k[1].areas_max == 3, "pose: n %d areas %lld px %lld max %d",
          k[1].n, (long long)k[1].areas, (long long)k[1].px, k[1].areas_max);
    CHECK(k[1].us == 10000 && k[1].us_max == 6000, "pose us %lld max %lld", (long long)k[1].us, (long long)k[1].us_max);
    CHECK(k[2].areas == 5 && k[3].areas == 1 && k[0].n == 0, "glint %lld pose+glint %lld other %d", (long long)k[2].areas,
          (long long)k[3].areas, k[0].n);
    CHECK(t.n == 4 && t.areas == 11 && t.px == 2400, "total n %d areas %lld px %lld", t.n, (long long)t.areas,
          (long long)t.px);
    CHECK(t.us == 21000 && t.us_max == 9000 && t.areas_max == 5, "total us %lld max %lld areas max %d", (long long)t.us,
          (long long)t.us_max, t.areas_max);
}

static void test_take_resets(void)
{
    static refr_stats_t s;
    refr_kind_t k[REFR_KINDS], t;
    memset(&s, 0, sizeof s);
    refresh(&s, 0, 4, 400, 1000);
    refr_stats_take(&s, k, &t);
    CHECK(t.areas == 4, "first window %lld", (long long)t.areas);
    refr_stats_take(&s, k, &t);
    CHECK(t.n == 0 && t.areas == 0 && t.px == 0 && t.us_max == 0, "second window not empty: %d %lld", t.n,
          (long long)t.areas);
    refr_stats_start(&s, 1, 2, 200);
    refr_stats_take(&s, k, &t);
    CHECK(t.n == 0, "a refresh in flight counts in the window it ends in");
    CHECK(refr_stats_end(&s, 500), "refresh in flight lost at the window boundary");
    refr_stats_take(&s, k, &t);
    CHECK(t.n == 1 && t.areas == 2 && k[1].px == 200, "boundary refresh: n %d areas %lld", t.n, (long long)t.areas);
}

static void test_empty_and_bad_kind(void)
{
    static refr_stats_t s;
    refr_kind_t k[REFR_KINDS], t;
    memset(&s, 0, sizeof s);
    refr_stats_start(&s, 1, 0, 0);
    CHECK(!refr_stats_end(&s, 700), "an empty refresh counted");
    refr_stats_start(&s, 1, 3, 0);
    CHECK(!refr_stats_end(&s, 700), "zero px counted");
    CHECK(!refr_stats_end(&s, 700), "an end without a start counted");
    refresh(&s, 7, 2, 50, 100);
    refresh(&s, -1, 1, 50, 100);
    refr_stats_take(&s, k, &t);
    CHECK(t.n == 2 && k[0].n == 2 && k[0].areas == 3, "out-of-range kinds go to other: n %d other %d areas %lld", t.n,
          k[0].n, (long long)k[0].areas);
}

int main(void)
{
    test_window_totals();
    test_take_resets();
    test_empty_and_bad_kind();
    if (failures) {
        printf("refr: %d failures\n", failures);
        return 1;
    }
    printf("refr: all tests passed\n");
    return 0;
}
