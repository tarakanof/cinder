#pragma once
#include "FreeRTOS.h"
/* test_scene.c records these; vTaskSuspend jumps out of the task body. */
BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                                           TaskHandle_t *handle, BaseType_t core, unsigned caps);
void vTaskDelay(uint32_t ticks);
void vTaskSuspend(TaskHandle_t task);
void vTaskDeleteWithCaps(TaskHandle_t task);
