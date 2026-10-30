#include "timing.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static span_stats_t s_stats;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void timing_add(int span, int64_t us)
{
    taskENTER_CRITICAL(&s_mux);
    span_stats_add(&s_stats, span, us);
    taskEXIT_CRITICAL(&s_mux);
}

void timing_read(span_stats_t *out, bool reset)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_mux);
    *out = s_stats;
    if (reset) span_stats_init(&s_stats, now);
    taskEXIT_CRITICAL(&s_mux);
}
