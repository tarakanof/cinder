#pragma once
#include <stdint.h>

/* One thread; ticks are 1 ms of the test's clock. */
typedef int portMUX_TYPE;
typedef uint32_t TickType_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m) ((void)(m))
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
