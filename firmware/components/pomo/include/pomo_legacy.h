#pragma once

#include <stdbool.h>

#include "pomo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy /v1/pomodoro/ state or action reply. False, out untouched, without a string "phase". */
bool pomo_legacy_parse(const char *body, pomo_state_t *out);

#ifdef __cplusplus
}
#endif
