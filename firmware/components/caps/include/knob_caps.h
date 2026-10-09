#pragma once

#include "device_api.h"
#include "pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This firmware's checkin caps. ids is caller storage the caps point into; keep it as long as c. */
void knob_caps(dev_caps_t *c, const char *ids[PAGES_N]);

#ifdef __cplusplus
}
#endif
