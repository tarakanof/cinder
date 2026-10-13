#pragma once

#include "freertos/FreeRTOS.h"
#include "refr_stats.h"

void diag_attach_refr(refr_stats_t *s, portMUX_TYPE *mux);
