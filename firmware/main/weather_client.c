#include "weather_client.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_store.h"
#include "diag.h"
#include "ember_client.h"
#include "esp_heap_caps.h"
#include "http_conn.h"
#include "legacy_task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "weather";

#define POLL_OK_MS (10 * 60 * 1000)
#define POLL_RETRY_MS (30 * 1000)
#define RESP_MAX (16 * 1024)

static SemaphoreHandle_t s_lock;
static wx_obs_t s_obs;
static bool s_have;
static int64_t s_obs_us;
static legacy_task_t s_lt;
typedef struct {
    char *buf;
    http_conn_t *conn;
} poll_ctx_t;

static TaskHandle_t s_task;
static poll_ctx_t *s_ctx;

static void publish(const wx_obs_t *obs)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (obs) s_obs = *obs;
    s_have = obs != NULL;
    s_obs_us = esp_timer_get_time();
    xSemaphoreGive(s_lock);
}

static bool wait_legacy(int ms)
{
    for (; ms > 0; ms -= 1000) {
        if (!lt_on(&s_lt)) return false;
        vTaskDelay(pdMS_TO_TICKS(ms < 1000 ? ms : 1000));
    }
    return true;
}

/* New connection per poll: the 10 min interval outlasts Ember's 120 s keep-alive. */
static int http_get(http_conn_t *conn, const char *url, char *buf, int cap)
{
    int status = http_conn_req(conn, url, NULL, true, buf, cap, NULL, NULL);
    http_conn_free(conn);
    if (status > 0 && status != 200) ESP_LOGW(TAG, "GET %s -> HTTP %d", url, status);
    return status == 200 ? (int)strlen(buf) : -1;
}

/* Empty the "hourly" arrays: ~150 small cJSON allocations would eat internal RAM. */
static void drop_hourly(char *body)
{
    static const char key[] = "\"hourly\":[";
    char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        char *open = p + sizeof key - 1, *close = strchr(open, ']');
        if (!close) return;
        memmove(open, close, strlen(close) + 1);
        p = open;
    }
}

static void copy_str(char *dst, size_t cap, const cJSON *item)
{
    dst[0] = 0;
    if (cJSON_IsString(item) && item->valuestring) strlcpy(dst, item->valuestring, cap);
}

static int minute_of(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? wx_minute_of_day(v->valuestring) : -1;
}

static bool parse(const char *body, wx_obs_t *o)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) return false;
    memset(o, 0, sizeof *o);
    o->enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "enabled"));
    copy_str(o->provider, sizeof o->provider, cJSON_GetObjectItemCaseSensitive(root, "provider"));
    o->now_min = minute_of(root, "generated_at");
    o->rise_min = o->set_min = -1;
    const cJSON *sun = cJSON_GetObjectItemCaseSensitive(root, "sun");
    if (cJSON_IsObject(sun)) {
        o->rise_min = minute_of(sun, "sunrise");
        o->set_min = minute_of(sun, "sunset");
    }
    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (cJSON_IsObject(cur)) {
        o->valid = true;
        o->stale = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cur, "stale"));
        o->severe = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cur, "severe"));
        copy_str(o->condition, sizeof o->condition, cJSON_GetObjectItemCaseSensitive(cur, "condition"));
        copy_str(o->code, sizeof o->code, cJSON_GetObjectItemCaseSensitive(cur, "condition_code"));
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(cur, "temp_c");
        if (cJSON_IsNumber(t)) {
            o->has_temp = true;
            o->temp_c = (float)t->valuedouble;
        }
    }
    cJSON_Delete(root);
    return true;
}

static void ctx_free(poll_ctx_t *c)
{
    if (!c) return;
    free(c->conn);
    free(c->buf);
    free(c);
}

static poll_ctx_t *ctx_new(void)
{
    poll_ctx_t *c = heap_caps_calloc(1, sizeof *c, MALLOC_CAP_SPIRAM);
    if (!c) return NULL;
    c->buf = heap_caps_malloc(RESP_MAX, MALLOC_CAP_SPIRAM);
    c->conn = heap_caps_calloc(1, sizeof *c->conn, MALLOC_CAP_SPIRAM);
    if (!c->buf || !c->conn) {
        ctx_free(c);
        return NULL;
    }
    http_conn_init(c->conn, "weather");
    return c;
}

static void poll_task(void *arg)
{
    poll_ctx_t *ctx = arg;
    char *buf = ctx->buf;
    http_conn_t *conn = ctx->conn;
    char url[CFG_URL_MAX + 32];
    config_store_ember_url(url, sizeof url);
    strlcat(url, "/v1/weather/state", sizeof url);
    static wx_obs_t obs;
    for (;;) {
        if (!lt_on(&s_lt)) {
            http_conn_free(conn);
            if (lt_park(&s_lt)) break;
            continue;
        }
        diag_note_stack("weather", (int)uxTaskGetStackHighWaterMark(NULL));
        if (!ember_client_online()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        bool ok = http_get(conn, url, buf, RESP_MAX) > 0;
        if (ok) {
            drop_hourly(buf);
            ok = parse(buf, &obs);
        }
        if (ok) {
            publish(&obs);
            wx_look_t l = wx_look_from_obs(&obs);
            ESP_LOGI(TAG, "%s %s/%s %.1f C -> %s int %d%s%s", obs.provider, obs.condition, obs.code, obs.temp_c,
                     wx_face_name(l.face), l.intensity, l.night ? " night" : "", l.still ? " still" : "");
        } else {
            if (fs_fails(&conn->streak) <= 1) ESP_LOGW(TAG, "poll failed; retry in %d s", POLL_RETRY_MS / 1000);
        }
        diag_note_stack("weather", (int)uxTaskGetStackHighWaterMark(NULL));
        wait_legacy(ok ? POLL_OK_MS : POLL_RETRY_MS);
    }
    diag_note_stack("weather", -1);
    for (;;) vTaskSuspend(NULL);
}

void weather_client_init(void)
{
    if (s_lock) return;
    s_lock = xSemaphoreCreateMutex();
    assert(s_lock);
    lt_init(&s_lt);
}

void weather_client_legacy(bool on)
{
    if (!s_lock) return;
    int64_t now_ms = esp_timer_get_time() / 1000;
    int act = lt_want(&s_lt, on, now_ms);
    if (act & LT_REAP) {
        vTaskDeleteWithCaps(s_task);
        s_task = NULL;
        ctx_free(s_ctx);
        s_ctx = NULL;
        diag_note_stack("weather", -1);
        ESP_LOGI(TAG, "legacy poll task removed");
    }
    if (!(act & LT_CREATE)) return;
    s_ctx = ctx_new();
    bool ok = s_ctx && xTaskCreatePinnedToCoreWithCaps(poll_task, "weather", 5120, s_ctx, 3, &s_task, 1,
                                                       MALLOC_CAP_SPIRAM) == pdPASS;
    if (!ok) {
        ctx_free(s_ctx);
        s_ctx = NULL;
    }
    uint32_t retry_ms = lt_created(&s_lt, ok, now_ms);
    if (!ok) ESP_LOGE(TAG, "no memory for the weather poll task; retry in %u s", (unsigned)(retry_ms / 1000));
}

void weather_client_feed(const wx_obs_t *obs)
{
    static wx_look_t last;
    static bool logged, had;
    if (!s_lock) return;
    publish(obs);
    if (!obs) {
        if (had || !logged) ESP_LOGI(TAG, "view: no weather (off or not fetched yet)");
        had = false;
        logged = true;
        return;
    }
    wx_look_t l = wx_look_from_obs(obs);
    if (!had || !wx_look_equal(&l, &last))
        ESP_LOGI(TAG, "view: %s %s/%s %.1f C -> %s int %d%s%s", obs->provider, obs->condition, obs->code, obs->temp_c,
                 wx_face_name(l.face), l.intensity, l.night ? " night" : "", l.still ? " still" : "");
    last = l;
    had = logged = true;
}

bool weather_client_get(wx_obs_t *out)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool have = s_have;
    if (have) {
        *out = s_obs;
        out->age_s = (esp_timer_get_time() - s_obs_us) / 1e6;
    }
    xSemaphoreGive(s_lock);
    return have;
}
