#pragma once
#include <stdio.h>
#include <stdlib.h>
static inline void esp_system_abort(const char *why) { fprintf(stderr, "abort: %s\n", why); abort(); }
