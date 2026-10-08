#pragma once

#include <stdbool.h>

#include "weather_face.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Empties every "hourly":[...] array in place, nesting and strings respected; a truncated array is left as is. */
void wx_legacy_drop_hourly(char *body);

/* Legacy /v1/weather/state body. False only when it is not JSON; o is zeroed first. */
bool wx_legacy_parse(const char *body, wx_obs_t *o);

#ifdef __cplusplus
}
#endif
