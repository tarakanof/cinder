#pragma once
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t UBaseType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdPASS 1
#define pdFAIL 0
