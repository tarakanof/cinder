#include <stdio.h>
#include <string.h>

#include "pomo_client.h"
#include "usage_client.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool s_have_off = true;
static double s_off = 1782000000.0 - 50, s_now = 50;

bool pomo_client_srv_offset(double *offset)
{
    *offset = s_off;
    return s_have_off;
}

double pomo_client_now(void) { return s_now; }

static usage_block_t block(int pct)
{
    usage_block_t b;
    memset(&b, 0, sizeof b);
    b.n = 1;
    snprintf(b.a[0].id, sizeof b.a[0].id, "claude:1");
    b.a[0].five.have = true;
    b.a[0].five.pct = pct;
    return b;
}

int main(void)
{
    unsigned gen = 0;
    usage_block_t out;
    usage_clock_t clk;
    CHECK(!usage_client_take(&gen, &out, &clk), "before init: nothing");
    usage_client_init();
    usage_client_feed(NULL, false, 0);
    CHECK(usage_client_take(&gen, &out, &clk) && out.n == 0 && clk.have_srv, "the first view: the server clock arrives");
    usage_client_feed(NULL, false, 0);
    CHECK(!usage_client_take(&gen, &out, &clk), "empty to empty: no change");
    usage_block_t b = block(40);
    usage_client_feed(&b, true, 3600);
    CHECK(usage_client_take(&gen, &out, &clk) && out.n == 1 && out.a[0].five.pct == 40, "a block arrives");
    CHECK(clk.have_srv && clk.has_utc_offset && clk.utc_offset == 3600 && clk.fed_at == 1782000000, "with its clock: %lld", clk.fed_at);
    CHECK(!usage_client_take(&gen, &out, &clk), "taken once");
    s_now = 80;
    usage_client_feed(&b, true, 3600);
    CHECK(!usage_client_take(&gen, &out, &clk) && clk.fed_at == 1782000030, "the same bytes: no change, the clock still moves");
    s_off += 2;
    usage_client_feed(&b, true, 3600);
    CHECK(!usage_client_take(&gen, &out, &clk), "a new server offset alone: no change");
    usage_client_feed(&b, true, 7200);
    CHECK(usage_client_take(&gen, &out, &clk) && clk.utc_offset == 7200, "a new utc_offset: change");
    usage_client_feed(&b, false, 0);
    CHECK(usage_client_take(&gen, &out, &clk) && !clk.has_utc_offset, "utc_offset gone: change");
    s_have_off = false;
    usage_client_feed(&b, false, 0);
    CHECK(usage_client_take(&gen, &out, &clk) && !clk.have_srv && clk.fed_at == 0, "server clock lost: change");
    b.a[0].five.pct = 41;
    usage_client_feed(&b, false, 0);
    CHECK(usage_client_take(&gen, &out, &clk) && out.a[0].five.pct == 41, "a value: change");
    b.a[0].stale = true;
    usage_client_feed(&b, false, 0);
    CHECK(usage_client_take(&gen, &out, &clk) && out.a[0].stale, "stale flips: change");
    usage_client_feed(NULL, false, 0);
    CHECK(usage_client_take(&gen, &out, &clk) && out.n == 0 && out.a[0].id[0] == 0, "cleared, the old account wiped");
    if (failures) {
        printf("usage client: %d failure(s)\n", failures);
        return 1;
    }
    printf("usage client: all tests passed\n");
    return 0;
}
