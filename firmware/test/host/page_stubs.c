#include "page_stubs.h"

#include <stdio.h>
#include <string.h>

#include "page_ops.h"

char page_log[512];
page_input_t page_stub_in;
int page_stub_inputs;

void page_log_reset(void)
{
    page_log[0] = 0;
    memset(&page_stub_in, 0, sizeof page_stub_in);
    page_stub_inputs = 0;
}

static void note(const char *what)
{
    size_t n = strlen(page_log);
    snprintf(page_log + n, sizeof page_log - n, "%s ", what);
}

#define STUB_PAGE(name, label)                                                           \
    void page_##name##_show(bool on) { note(on ? label "+" : label "-"); }               \
    void page_##name##_input(const page_input_t *in, double t)                           \
    {                                                                                    \
        (void)t;                                                                         \
        page_stub_in = *in;                                                              \
        page_stub_inputs++;                                                              \
        note(label ".in");                                                               \
    }                                                                                    \
    void page_##name##_frame(const page_frame_t *f)                                      \
    {                                                                                    \
        (void)f;                                                                         \
        note(label ".frame");                                                            \
    }

STUB_PAGE(bot, "bot")
STUB_PAGE(pomo, "pomo")
STUB_PAGE(np, "np")

void page_weather_show(bool on) { note(on ? "weather+" : "weather-"); }
