#include <stdio.h>
#include <string.h>

#include "span_stats.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    static span_stats_t s;
    span_stats_init(&s, 500);
    CHECK(s.since_us == 500 && s.span[SPAN_FADE].n == 0 && span_avg_us(&s.span[SPAN_FADE]) == 0, "fresh table is empty");
    span_stats_add(&s, SPAN_FADE, 1000);
    span_stats_add(&s, SPAN_FADE, 4000);
    span_stats_add(&s, SPAN_FADE, 2500);
    span_stats_add(&s, SPAN_RING, 90);
    CHECK(s.span[SPAN_FADE].n == 3 && s.span[SPAN_FADE].total_us == 7500 && s.span[SPAN_FADE].max_us == 4000, "fade n %u total %lld max %lld",
          s.span[SPAN_FADE].n, (long long)s.span[SPAN_FADE].total_us, (long long)s.span[SPAN_FADE].max_us);
    CHECK(span_avg_us(&s.span[SPAN_FADE]) == 2500, "avg %lld", (long long)span_avg_us(&s.span[SPAN_FADE]));
    CHECK(s.span[SPAN_RING].n == 1 && s.span[SPAN_UPDATE].n == 0, "spans are independent");
    span_stats_add(&s, SPAN_FADE, -5);
    CHECK(s.span[SPAN_FADE].n == 4 && s.span[SPAN_FADE].total_us == 7500, "negative clamps to 0");
    span_stats_add(&s, -1, 10);
    span_stats_add(&s, SPAN_COUNT, 10);
    CHECK(s.span[SPAN_FADE].n == 4, "out of range ids are ignored");
    span_stats_add(&s, SPAN_M8, 1);
    span_stats_add(&s, SPAN_M8, 2);
    CHECK(span_avg_us(&s.span[SPAN_M8]) == 2, "avg rounds to nearest: %lld", (long long)span_avg_us(&s.span[SPAN_M8]));
    span_stats_init(&s, 9000);
    CHECK(s.since_us == 9000 && s.span[SPAN_FADE].n == 0 && s.span[SPAN_FADE].max_us == 0 && s.span[SPAN_RING].total_us == 0, "reset clears all");
    CHECK(strcmp(span_name(SPAN_COVER_DECODE), "cover_decode") == 0 && strcmp(span_name(SPAN_UPDATE), "update") == 0 && span_name(99)[0] == 0, "names");
    if (failures) return 1;
    printf("span_stats: ok\n");
    return 0;
}
