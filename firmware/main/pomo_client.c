#include "pomo_client.h"

#include <assert.h>
#include <string.h>

#include "cJSON.h"
#include "config_store.h"
#include "diag.h"
#include "ember_client.h"
#include "http_conn.h"
#include "legacy_task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "pomo";

#define POLL_RUNNING_MS 1000
#define POLL_IDLE_MS config_store_poll_ms()
#define RESP_MAX 512
#define ACTION_NOTE_S 4.0

static QueueHandle_t s_queue;
static legacy_task_t s_lt;
static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static pomo_snapshot_t s_snap;
static pomo_srv_clock_t s_srv;

double pomo_client_now(void) { return esp_timer_get_time() / 1e6; }

static bool parse_state(const char *body, pomo_state_t *out)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) return false;
    const cJSON *phase = cJSON_GetObjectItemCaseSensitive(root, "phase");
    bool ok = cJSON_IsString(phase);
    if (ok) {
        const cJSON *rem = cJSON_GetObjectItemCaseSensitive(root, "remaining_sec");
        const cJSON *plan = cJSON_GetObjectItemCaseSensitive(root, "planned_sec");
        const cJSON *round = cJSON_GetObjectItemCaseSensitive(root, "round");
        *out = (pomo_state_t){
            .phase = pomo_phase_from_wire(phase->valuestring),
            .running = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "running")),
            .paused = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "paused")),
            .remaining_sec = cJSON_IsNumber(rem) ? rem->valueint : 0,
            .planned_sec = cJSON_IsNumber(plan) ? plan->valueint : 0,
            .round = cJSON_IsNumber(round) ? round->valueint : 0,
        };
    }
    cJSON_Delete(root);
    return ok;
}

static http_conn_t *s_conn;

static int http_do(http_conn_t *conn, const char *url, bool post, const char *body, char *buf, int cap)
{
    /* Actions are not idempotent (a second skip skips two phases): no blind retry. */
    return http_conn_req(conn, url, post ? (body ? body : "") : NULL, !post, buf, cap, NULL, NULL);
}

static void publish_state(const pomo_state_t *s, double now)
{
    taskENTER_CRITICAL(&s_lock);
    pomo_clock_sync(&s_snap.clock, s, now);
    s_snap.online = true;
    s_snap.disabled = false;
    taskEXIT_CRITICAL(&s_lock);
}

static void publish_link(bool online, bool disabled)
{
    taskENTER_CRITICAL(&s_lock);
    s_snap.online = online;
    s_snap.disabled = disabled;
    taskEXIT_CRITICAL(&s_lock);
}

static void publish_action(int err)
{
    taskENTER_CRITICAL(&s_lock);
    s_snap.action_error = err;
    s_snap.action_at = pomo_client_now();
    taskEXIT_CRITICAL(&s_lock);
}

static const char *const PHASE_NAMES[] = {"idle", "focus", "short_break", "long_break", "unknown"};
static const char *const MODE_NAMES[] = {"idle", "running", "paused", "parked"};

#define OFFLINE_AFTER_FAILS 3
static int s_fails;

static void poll_failed(void)
{
    if (++s_fails >= OFFLINE_AFTER_FAILS) publish_link(false, false);
}

static bool poll_once(http_conn_t *conn, char *buf, int cap)
{
    char url[CFG_URL_MAX + 32];
    config_store_ember_url(url, sizeof url);
    strlcat(url, "/v1/pomodoro/state", sizeof url);
    int status = http_do(conn, url, false, NULL, buf, cap);
    pomo_state_t s;
    if (status == 200 && parse_state(buf, &s)) {
        s_fails = 0;
        publish_state(&s, pomo_client_now());
        return true;
    }
    static int last_status = 200;
    if (status != -1 && status != last_status) {
        if (status != 200) ESP_LOGW(TAG, "GET state -> HTTP %d", status);
        last_status = status;
    }
    if (status == 404) {
        s_fails = 0;
        publish_link(true, true);
    } else {
        poll_failed();
    }
    return false;
}

void pomo_client_run_action(pomo_input_t in, http_conn_t *conn, char *buf, int cap)
{
    pomo_snapshot_t snap;
    bool have = pomo_client_get(&snap);
    if (!have && poll_once(conn, buf, cap)) have = pomo_client_get(&snap);
    if (!have) {
        ESP_LOGW(TAG, "action dropped: no Pomodoro state from Ember");
        publish_action(-1);
        return;
    }
    pomo_state_t s = pomo_clock_at(&snap.clock, pomo_client_now());
    pomo_action_t a = pomo_action_for(&s, in);
    const char *path = pomo_action_path(a);
    if (!path) return;
    if (!config_store_has_device()) {
        ESP_LOGW(TAG, "%s: not paired with Ember", path);
        publish_action(401);
        return;
    }
    if (ember_client_link() == EMBER_LINK_UNAUTHORIZED) {
        ESP_LOGW(TAG, "%s: not paired with Ember", path);
        publish_action(401);
        return;
    }
    char url[CFG_URL_MAX + 32];
    config_store_ember_url(url, sizeof url);
    strlcat(url, "/v1/pomodoro/", sizeof url);
    strlcat(url, path, sizeof url);
    int status = http_do(conn, url, true, a == POMO_ACT_START ? "{\"phase\":\"focus\"}" : NULL, buf, cap);
    pomo_state_t ns;
    if (status == 200 && parse_state(buf, &ns)) {
        publish_state(&ns, pomo_client_now());
        publish_action(0);
        ESP_LOGI(TAG, "%s -> %s %s, %d s left", path, PHASE_NAMES[ns.phase], MODE_NAMES[pomo_mode(&ns)],
                 ns.remaining_sec);
    } else {
        ESP_LOGW(TAG, "%s -> HTTP %d", path, status);
        if (status == 401) ember_client_report_unauthorized();
        publish_action(status == 200 ? -1 : status);
    }
}

void pomo_client_log_state(void)
{
    static pomo_mode_t last_mode = (pomo_mode_t)-1;
    static pomo_phase_t last_phase = (pomo_phase_t)-1;
    static bool stack_logged;
    pomo_snapshot_t snap;
    if (!pomo_client_get(&snap) || (pomo_mode(&snap.clock.state) == last_mode && snap.clock.state.phase == last_phase))
        return;
    last_mode = pomo_mode(&snap.clock.state);
    last_phase = snap.clock.state.phase;
    pomo_state_t now = pomo_clock_at(&snap.clock, pomo_client_now());
    ESP_LOGI(TAG, "state %s %s, %d/%d s, round %d", PHASE_NAMES[last_phase], MODE_NAMES[last_mode], now.remaining_sec,
             now.planned_sec, now.round);
    if (!stack_logged) {
        stack_logged = true;
        ESP_LOGI(TAG, "%s task stack headroom %u B", pcTaskGetName(NULL), (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

static void client_task(void *arg)
{
    (void)arg;
    static EXT_RAM_BSS_ATTR char buf[RESP_MAX];
    for (;;) {
        if (!lt_on(&s_lt)) {
            http_conn_free(s_conn);
            diag_note_stack("pomo", -1);
            ESP_LOGI(TAG, "legacy poll task stopping (Ember's view is back)");
            if (!lt_park(&s_lt)) continue;
            for (;;) vTaskSuspend(NULL);
        }
        diag_note_stack("pomo", (int)uxTaskGetStackHighWaterMark(NULL));
        pomo_snapshot_t snap;
        bool have = pomo_client_get(&snap);
        pomo_mode_t mode = have ? pomo_mode(&snap.clock.state) : POMO_MODE_IDLE;
        int wait_ms = have && mode == POMO_MODE_RUNNING ? POLL_RUNNING_MS : POLL_IDLE_MS;

        pomo_input_t in;
        bool got = xQueueReceive(s_queue, &in, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
        if (!lt_on(&s_lt)) {
            if (got) xQueueSendToFront(s_queue, &in, 0);
            continue;
        }
        if (got) {
            if (ember_client_online()) pomo_client_run_action(in, s_conn, buf, RESP_MAX);
            else publish_action(-1);
        } else if (ember_client_online()) {
            poll_once(s_conn, buf, RESP_MAX);
        } else {
            poll_failed();
        }
        pomo_client_log_state();
    }
}

void pomo_client_init(void)
{
    if (s_queue) return;
    pomo_clock_init(&s_snap.clock);
    pomo_srv_clock_init(&s_srv);
    s_queue = xQueueCreate(4, sizeof(pomo_input_t));
    assert(s_queue);
    lt_init(&s_lt);
}

void pomo_client_legacy(bool on)
{
    if (!s_queue) return;
    int64_t now_ms = esp_timer_get_time() / 1000;
    int act = lt_want(&s_lt, on, now_ms);
    if (act & LT_REAP) {
        vTaskDeleteWithCaps(s_task);
        s_task = NULL;
    }
    if (!(act & LT_CREATE)) return;
    if (!s_conn) {
        s_conn = heap_caps_calloc(1, sizeof *s_conn, MALLOC_CAP_SPIRAM);
        if (s_conn) http_conn_init(s_conn, "pomo");
    }
    bool ok = s_conn && xTaskCreatePinnedToCoreWithCaps(client_task, "pomo", 5632, NULL, 3, &s_task, 1,
                                                        MALLOC_CAP_SPIRAM) == pdPASS;
    uint32_t retry_ms = lt_created(&s_lt, ok, now_ms);
    if (ok) return;
    publish_link(false, false);
    ESP_LOGE(TAG, "no memory for the Pomodoro poll task; retry in %u s", (unsigned)(retry_ms / 1000));
}

bool pomo_client_next_action(uint32_t wait_ms, pomo_input_t *in)
{
    if (!s_queue) {
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
        return false;
    }
    return xQueueReceive(s_queue, in, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void pomo_client_feed(const pomo_state_t *s, bool counting, long long ends_at, long long server_now, double sent,
                      double received)
{
    s_fails = 0;
    if (server_now > 0) pomo_srv_clock_note(&s_srv, server_now, sent, received);
    if (!s) {
        publish_link(true, true);
        return;
    }
    double now = pomo_client_now();
    taskENTER_CRITICAL(&s_lock);
    if (counting && s_srv.valid) pomo_clock_sync_end(&s_snap.clock, s, ends_at, pomo_srv_clock_offset(&s_srv), now);
    else if (!counting) pomo_clock_sync(&s_snap.clock, s, now);
    s_snap.online = s_snap.clock.valid;
    s_snap.disabled = false;
    taskEXIT_CRITICAL(&s_lock);
}

void pomo_client_feed_failed(void) { poll_failed(); }

bool pomo_client_srv_offset(double *offset)
{
    if (!s_srv.valid) return false;
    *offset = pomo_srv_clock_offset(&s_srv);
    return true;
}

void pomo_client_note_clock(long long server_now, double sent, double received)
{
    if (server_now > 0) pomo_srv_clock_note(&s_srv, server_now, sent, received);
}

int pomo_client_view_poll_ms(int idle_ms)
{
    pomo_snapshot_t snap;
    if (!pomo_client_get(&snap) || pomo_mode(&snap.clock.state) != POMO_MODE_RUNNING) return idle_ms;
    int left = pomo_clock_at(&snap.clock, pomo_client_now()).remaining_sec;
    return left * 1000 <= idle_ms + POLL_RUNNING_MS ? POLL_RUNNING_MS : idle_ms;
}

void pomo_client_action(pomo_input_t in)
{
    if (s_queue && xQueueSend(s_queue, &in, 0) == pdTRUE) ember_client_wake();
}

bool pomo_client_get(pomo_snapshot_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_snap;
    taskEXIT_CRITICAL(&s_lock);
    return out->clock.valid && out->online && !out->disabled;
}

const char *pomo_client_note(const pomo_snapshot_t *snap, double now)
{
    if (snap->action_error && now - snap->action_at < ACTION_NOTE_S) {
        switch (snap->action_error) {
        case 401: return "NOT PAIRED";
        case 404: return "POMODORO OFF";
        case 429: return "SLOW DOWN";
        case -1: return "NO CONNECTION";
        default: return "EMBER ERROR";
        }
    }
    if (ember_client_link() == EMBER_LINK_UNAUTHORIZED || !config_store_has_device()) return "NOT PAIRED";
    if (snap->disabled) return "POMODORO OFF";
    if (!snap->online) return "OFFLINE";
    return NULL;
}
