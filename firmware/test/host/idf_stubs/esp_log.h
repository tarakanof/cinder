#pragma once

#include <stdio.h>

typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO } esp_log_level_t;

static inline void esp_log_level_set(const char *tag, esp_log_level_t level)
{
    (void)tag;
    (void)level;
}

#define ESP_LOG_HOST(tag, ...) do { (void)(tag); if (0) printf(__VA_ARGS__); } while (0)
#define ESP_LOGE ESP_LOG_HOST
#define ESP_LOGW ESP_LOG_HOST
#define ESP_LOGI ESP_LOG_HOST
#define ESP_LOGD ESP_LOG_HOST
