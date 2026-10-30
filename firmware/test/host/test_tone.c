#include <stdio.h>
#include <stdlib.h>

#include "tone.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_length_and_level(void)
{
    tone_t t;
    tone_init(&t, 16000, 440, 500, 100);
    CHECK(t.total == 8000 && t.fade == 80, "500 ms at 16 kHz, 5 ms fades: %u %u", (unsigned)t.total, (unsigned)t.fade);
    int16_t buf[300];
    size_t got = 0, n;
    int peak = 0, crossings = 0, prev = 0;
    int16_t first = 1, last = 1;
    bool have_first = false;
    while ((n = tone_fill(&t, buf, sizeof buf / sizeof buf[0])) > 0) {
        for (size_t i = 0; i < n; i++) {
            if (!have_first) {
                first = buf[i];
                have_first = true;
            }
            if (abs(buf[i]) > peak) peak = abs(buf[i]);
            if ((prev < 0 && buf[i] >= 0) || (prev > 0 && buf[i] <= 0)) crossings++;
            prev = buf[i];
            last = buf[i];
        }
        got += n;
    }
    CHECK(got == 8000 && tone_done(&t), "all samples written once: %zu", got);
    CHECK(peak <= TONE_AMP_MAX && peak > TONE_AMP_MAX * 99 / 100, "peak at the cap: %d", peak);
    CHECK(first == 0 && last == 0, "fades start and end at 0: %d %d", first, last);
    CHECK(crossings >= 438 && crossings <= 442, "440 Hz over 0.5 s: ~440 zero crossings: %d", crossings);
    CHECK(tone_fill(&t, buf, 10) == 0, "nothing after the end");
}

static void test_volume(void)
{
    tone_t t;
    tone_init(&t, 16000, 1000, 100, 25);
    int16_t buf[1600];
    size_t n = tone_fill(&t, buf, 1600);
    int peak = 0;
    for (size_t i = 0; i < n; i++)
        if (abs(buf[i]) > peak) peak = abs(buf[i]);
    CHECK(peak <= TONE_AMP_MAX / 4 + 1 && peak > TONE_AMP_MAX / 4 - 50, "25 %% volume: %d", peak);
    tone_init(&t, 16000, 1000, 100, 250);
    CHECK(t.amp == TONE_AMP_MAX, "volume clamped to 100 %%");
    tone_init(&t, 16000, 1000, 0, 50);
    CHECK(tone_fill(&t, buf, 10) == 0 && tone_done(&t), "0 ms is empty");
    tone_init(&t, 16000, 1000, 3, 50);
    CHECK(t.fade * 2 <= t.total, "short tone: fades fit");
    tone_init(&t, 0, 1000, 100, 50);
    CHECK(tone_fill(&t, buf, 10) == 0, "rate 0 is empty");
}

int main(void)
{
    test_length_and_level();
    test_volume();
    if (failures) {
        printf("test_tone: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_tone: ok (length, fades, frequency, volume cap)\n");
    return 0;
}
