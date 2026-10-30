#include "controls_client.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "config_store.h"
#include "ember_client.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link_state.h"

static const char *TAG = "controls";

#define BURST_LINGER_MS 700
#define BURST_MAX_MS 5000

typedef struct {
    char url[CFG_URL_MAX + 32];
    char body[CTL_BODY_MAX];
    char ans[160];
    char target[CTL_TARGET_MAX];
} ctl_io_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static EXT_RAM_BSS_ATTR ctl_queue_t s_q;
static EXT_RAM_BSS_ATTR ctl_state_t s_state;
static EXT_RAM_BSS_ATTR ctl_io_t s_io;
static EXT_RAM_BSS_ATTR uint32_t s_boot;
static EXT_RAM_BSS_ATTR int64_t s_hold_until_ms;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void publish_locked(ctl_res_t res, const char *target)
{
    s_state.res = res;
    s_state.at = esp_timer_get_time() / 1e6;
    if (target && target[0]) strlcpy(s_state.target, target, sizeof s_state.target);
}

static void publish(ctl_res_t res, const char *target)
{
    taskENTER_CRITICAL(&s_lock);
    publish_locked(res, target);
    taskEXIT_CRITICAL(&s_lock);
}

void controls_client_send(ctl_gesture_t g, int steps)
{
    uint32_t link = ember_client_link_word();
    if (ember_client_offline()) {
        publish(CTL_RES_OFFLINE, NULL);
        ESP_LOGW(TAG, "%s dropped: offline", ctl_gesture_name(g));
        return;
    }
    int64_t now = now_ms();
    taskENTER_CRITICAL(&s_lock);
    ctl_queue_add(&s_q, g, steps, now, link);
    taskEXIT_CRITICAL(&s_lock);
    ember_client_wake();
}

static void expire_locked(void)
{
    if (ctl_queue_expire(&s_q, now_ms())) publish_locked(CTL_RES_NO_CONN, NULL);
}

void controls_client_get(ctl_state_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    expire_locked();
    *out = s_state;
    taskEXIT_CRITICAL(&s_lock);
}

bool controls_client_pending(void)
{
    taskENTER_CRITICAL(&s_lock);
    bool n = s_q.n > 0;
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

static bool take(ctl_queued_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    expire_locked();
    bool got = ctl_queue_pop(&s_q, out);
    taskEXIT_CRITICAL(&s_lock);
    return got;
}

static void retry_hdr(const char *key, const char *value, void *ctx)
{
    if (strcasecmp(key, "Retry-After") == 0) *(int *)ctx = ctl_retry_ms(value);
}

static void send_one(http_conn_t *conn, const char *base, const ctl_queued_t *q)
{
    const char *name = ctl_gesture_name(q->gesture);
    if (!link_press_ok(q->link, ember_client_link_word())) {
        ESP_LOGW(TAG, "%s dropped: offline since the gesture", name);
        publish(CTL_RES_OFFLINE, NULL);
        return;
    }
    if (ember_client_link() == EMBER_LINK_UNAUTHORIZED || !config_store_has_device()) {
        publish(CTL_RES_NOT_PAIRED, NULL);
        return;
    }
    if (now_ms() < s_hold_until_ms) {
        publish(CTL_RES_TOO_FAST, NULL);
        return;
    }
    if (ctl_body(q->gesture, q->steps, s_io.body, sizeof s_io.body) < 0) return;
    if (!s_boot) s_boot = esp_random() | 1;
    char key[CTL_KEY_MAX];
    ctl_key(s_boot, q->id, key, sizeof key);
    snprintf(s_io.url, sizeof s_io.url, "%s/v1/controls/action", base);
    int retry_ms = 1000;
    const http_req_opts_t o = {.post_body = s_io.body, .idempotent = true, .idem_key = key, .on_header = retry_hdr, .hdr_ctx = &retry_ms};
    int64_t t0 = now_ms();
    int status = http_conn_req_opts(conn, s_io.url, &o, s_io.ans, sizeof s_io.ans);
    ESP_LOGI(TAG, "%s %d -> HTTP %d in %lld ms", name, q->steps, status, (long long)(now_ms() - t0));
    if (status == 429) s_hold_until_ms = now_ms() + retry_ms;
    s_io.target[0] = 0;
    ctl_res_t res = ctl_reply(status, s_io.ans, s_io.target, sizeof s_io.target);
    publish(res, s_io.target);
}

void controls_client_run(http_conn_t *conn, const char *base)
{
    ctl_queued_t q;
    int64_t start = now_ms(), last_ms = 0;
    bool last_turn = false;
    for (;;) {
        int64_t now = now_ms();
        if (now - start >= BURST_MAX_MS) return;
        if (!take(&q)) {
            if (!last_turn || now - last_ms >= BURST_LINGER_MS) return;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        send_one(conn, base, &q);
        last_ms = now_ms();
        last_turn = q.gesture == CTL_TURN;
    }
}
