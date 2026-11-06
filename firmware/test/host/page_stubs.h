#pragma once

#include "pages.h"

extern char page_log[512];
extern page_input_t page_stub_in;
extern int page_stub_inputs;
extern bool page_stub_room_visible;

void page_log_reset(void);
