#include "page_ops.h"
#include "page_stubs.h"

const char *page_stub_last;
int page_stub_on;

#define STUB_SHOW(name) \
    void name(bool on) { page_stub_last = #name; page_stub_on = on; }
#define STUB_INPUT(name) \
    void name(const page_input_t *in, double t) { (void)in; (void)t; page_stub_last = #name; }
#define STUB_FRAME(name) \
    void name(const page_frame_t *f) { (void)f; page_stub_last = #name; }

STUB_SHOW(page_bot_show)
STUB_INPUT(page_bot_input)
STUB_FRAME(page_bot_frame)
STUB_SHOW(page_pomo_show)
STUB_INPUT(page_pomo_input)
STUB_FRAME(page_pomo_frame)
STUB_SHOW(page_weather_show)
STUB_SHOW(page_np_show)
STUB_INPUT(page_np_input)
STUB_FRAME(page_np_frame)
