#include <stdio.h>
#include <string.h>

#include "knob_rotation.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define VIS_X1 (KR_FRAME_W - 1)
#define VIS_Y1 (KR_FRAME_H - 1)

static void test_supported(void)
{
    CHECK(KR_N_SUPPORTED == 2 && KR_SUPPORTED[0] == 0 && KR_SUPPORTED[1] == 180, "supported [0,180]");
    CHECK(kr_effective(0) == 0 && kr_effective(180) == 180, "supported kept");
    const int other[] = {90, 270, 45, -180, 360, 1, -1};
    for (size_t i = 0; i < sizeof other / sizeof other[0]; i++)
        CHECK(kr_effective(other[i]) == 0, "%d falls back to 0", other[i]);
    CHECK(kr_valid(0) && kr_valid(90) && kr_valid(180) && kr_valid(270), "protocol values");
    CHECK(!kr_valid(45) && !kr_valid(360) && !kr_valid(-90), "non-protocol values");
}

static void test_panel(void)
{
    kr_panel_t p = kr_panel(0);
    CHECK(!p.mirror_x && !p.mirror_y && p.gap_x == 0 && p.gap_y == 0, "0: as before");
    p = kr_panel(90);
    CHECK(!p.mirror_x && !p.mirror_y && p.gap_x == 0 && p.gap_y == 0, "90 unsupported: as 0");
    p = kr_panel(180);
    CHECK(p.mirror_x && p.mirror_y && p.gap_x == 2 && p.gap_y == 14, "180: mirror, gap 2/14: %d/%d", p.gap_x, p.gap_y);
    CHECK(p.gap_x % 2 == 0 && p.gap_y % 2 == 0, "even gaps keep the 2 px windows");
}

static int gram_x(int deg, int x)
{
    kr_panel_t p = kr_panel(deg);
    return p.mirror_x ? KR_GRAM - 1 - (x + p.gap_x) : x + p.gap_x;
}

static int gram_y(int deg, int y)
{
    kr_panel_t p = kr_panel(deg);
    return p.mirror_y ? KR_GRAM - 1 - (y + p.gap_y) : y + p.gap_y;
}

static void test_coverage(void)
{
    for (int deg = 0; deg <= 180; deg += 180) {
        int hit_x[KR_GRAM] = {0}, hit_y[KR_GRAM] = {0};
        for (int x = 0; x < KR_FRAME_W; x++) {
            int g = gram_x(deg, x);
            CHECK(g >= 0 && g < KR_GRAM, "%d: column %d inside GRAM: %d", deg, x, g);
            if (g >= 0 && g < KR_GRAM) hit_x[g]++;
        }
        for (int y = 0; y < KR_FRAME_H; y++) {
            int g = gram_y(deg, y);
            CHECK(g >= 0 && g < KR_GRAM, "%d: row %d inside GRAM: %d", deg, y, g);
            if (g >= 0 && g < KR_GRAM) hit_y[g]++;
        }
        int miss = 0;
        for (int g = KR_VIS_X0; g <= VIS_X1; g++) miss += hit_x[g] != 1;
        for (int g = 0; g <= VIS_Y1; g++) miss += hit_y[g] != 1;
        CHECK(miss == 0, "%d: every visible GRAM column and row written once (no green line): %d missed", deg, miss);
    }
    for (int x = 0; x < KR_FRAME_W; x += 2) {
        int a = gram_x(180, x), b = gram_x(180, x + 1);
        CHECK(b % 2 == 0 && a == b + 1, "180: even/odd window %d-%d stays a 2 px pair in GRAM (%d-%d)", x, x + 1, b, a);
    }
    for (int y = 0; y < KR_FRAME_H; y += 2) {
        int a = gram_y(180, y), b = gram_y(180, y + 1);
        CHECK(b % 2 == 0 && a == b + 1, "180: rows %d-%d stay a 2 px pair in GRAM (%d-%d)", y, y + 1, b, a);
    }
}

static void test_image_rotated(void)
{
    for (int x = KR_VIS_X0; x <= VIS_X1; x++)
        CHECK(gram_x(180, x) == KR_VIS_X0 + VIS_X1 - x, "180: column %d lands mirrored about the glass centre", x);
    for (int y = 0; y <= VIS_Y1; y++) CHECK(gram_y(180, y) == VIS_Y1 - y, "180: row %d mirrored", y);
}

static void test_touch(void)
{
    int x = 100, y = 50;
    kr_touch(0, &x, &y);
    CHECK(x == 100 && y == 50, "0: unchanged");
    x = 100, y = 50;
    kr_touch(90, &x, &y);
    CHECK(x == 100 && y == 50, "90 unsupported: unchanged");
    for (int tx = KR_VIS_X0; tx <= VIS_X1; tx += 5) {
        for (int ty = 0; ty <= VIS_Y1; ty += 5) {
            int px = tx, py = ty;
            kr_touch(180, &px, &py);
            CHECK(gram_x(180, px) == tx && gram_y(180, py) == ty,
                  "180: finger on GRAM %d,%d hits the pixel drawn there (%d,%d)", tx, ty, px, py);
        }
    }
    x = 236, y = 233;
    kr_touch(180, &x, &y);
    CHECK(x == 241 && y == 232, "180: centre %d,%d", x, y);
    x = 0, y = 0;
    kr_touch(180, &x, &y);
    CHECK(x == KR_FRAME_W - 1 && y == KR_FRAME_H - 1, "180: corner clamps into the frame: %d,%d", x, y);
    x = 600, y = -5;
    kr_touch(0, &x, &y);
    CHECK(x == KR_FRAME_W - 1 && y == 0, "0: out of range clamps: %d,%d", x, y);
}

int main(void)
{
    test_supported();
    test_panel();
    test_coverage();
    test_image_rotated();
    test_touch();
    if (failures) {
        printf("rotation: %d failure(s)\n", failures);
        return 1;
    }
    printf("rotation: all tests passed\n");
    return 0;
}
