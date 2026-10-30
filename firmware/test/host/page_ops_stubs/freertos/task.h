#pragma once
#include <stdint.h>
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, void *handle, int core);
void vTaskDelay(uint32_t ticks);
