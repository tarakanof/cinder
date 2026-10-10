#pragma once

typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG } esp_log_level_t;

static inline void esp_log_level_set(const char *tag, esp_log_level_t level)
{
    (void)tag;
    (void)level;
}

void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#define ESP_LOGE(tag, ...) esp_log_host(ESP_LOG_ERROR, tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) esp_log_host(ESP_LOG_WARN, tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) esp_log_host(ESP_LOG_INFO, tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) esp_log_host(ESP_LOG_DEBUG, tag, __VA_ARGS__)
