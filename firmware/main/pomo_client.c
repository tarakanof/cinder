#include "pomo_client.h"

#include <assert.h>
#include <string.h>

#include "config_store.h"
#include "diag.h"
#include "ember_client.h"
#include "http_conn.h"
#include "legacy_task.h"
#include "pomo_legacy.h"
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
static EXT_RAM_BSS_ATTR link_state_t s_src;
static uint32_t s_src_word;

double pomo_client_now(void) { return esp_timer_get_time() / 1e6; }

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
    s_snap.disabled = false;
    taskEXIT_CRITICAL(&s_lock);
}

static void publish_disabled(void)
{
    taskENTER_CRITICAL(&s_lock);
    s_snap.disabled = true;
    taskEXIT_CRITICAL(&s_lock);
}

static void publish_forget(void)
{
    taskENTER_CRITICAL(&s_lock);
    pomo_clock_init(&s_snap.clock);
    taskEXIT_CRITICAL(&s_lock);
}

static void publish_action(int err)
{
    taskENTER_CRITICAL(&s_lock);
    s_snap.action_error = err;
    s_snap.action_at = pomo_client_now();
    taskEXIT_CRITICAL(&s_lock);
}

static uint32_t src_word(void)
{
    taskENTER_CRITICAL(&s_lock);
    uint32_t w = s_src_word;
    taskEXIT_CRITICAL(&s_lock);
    return w;
}

static void src_note(link_outcome_t o)
{
    taskENTER_CRITICAL(&s_lock);
    link_level_t was = s_src.level;
    bool changed = link_state_note(&s_src, o, esp_timer_get_time() / 1000);
    s_src_word = link_state_word(&s_src);
    link_level_t now = s_src.level;
    taskEXIT_CRITICAL(&s_lock);
    if (!changed || (now != LINK_OFFLINE && was != LINK_OFFLINE)) return;
    ESP_LOGW(TAG, "Pomodoro data %s", now == LINK_OFFLINE ? "stale: showing the offline estimate" : "fresh again");
    if (now == LINK_OFFLINE) pomo_client_drop_presses();
}

void pomo_client_source(bool ok) { src_note(ok ? LINK_OK : LINK_FAIL); }

void pomo_client_drop_presses(void)
{
    pomo_press_t p;
    int n = 0;
    while (s_queue && xQueueReceive(s_queue, &p, 0) == pdTRUE) n++;
    if (!n) return;
    ESP_LOGW(TAG, "%d push(es) dropped: Pomodoro offline", n);
    publish_action(POMO_ACTION_OFFLINE);
}

static const char *const PHASE_NAMES[] = {"idle", "focus", "short_break", "long_break", "unknown"};
static const char *const MODE_NAMES[] = {"idle", "running", "paused", "parked"};

static bool poll_once(http_conn_t *conn, char *buf, int cap)
{
    char url[CFG_URL_MAX + 32];
    config_store_ember_url(url, sizeof url);
    strlcat(url, "/v1/pomodoro/state", sizeof url);
    int status = http_do(conn, url, false, NULL, buf, cap);
    pomo_state_t s;
    bool parsed = status == 200 && pomo_legacy_parse(buf, &s);
    if (parsed) publish_state(&s, pomo_client_now());
    else if (status == 404) publish_disabled();
    src_note(pomo_poll_counts(status, parsed));
    if (parsed) return true;
    static int last_status = 200;
    if (status != -1 && status != last_status) {
        if (status != 200) ESP_LOGW(TAG, "GET state -> HTTP %d", status);
        last_status = status;
    }
    return false;
}

void pomo_client_run_action(pomo_press_t press, http_conn_t *conn, char *buf, int cap)
{
    if (!pomo_press_ok(&press, ember_client_link_word(), src_word())) {
        ESP_LOGW(TAG, "push dropped: Pomodoro offline since the press");
        publish_action(POMO_ACTION_OFFLINE);
        return;
    }
    pomo_input_t in = press.in;
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
    if (status == 200 && pomo_legacy_parse(buf, &ns)) {
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
            if (!lt_park(&s_lt)) continue;
            diag_note_stack("pomo", -1);
            for (;;) vTaskSuspend(NULL);
        }
        diag_note_stack("pomo", (int)uxTaskGetStackHighWaterMark(NULL));
        pomo_snapshot_t snap;
        bool have = pomo_client_get(&snap);
        pomo_mode_t mode = have ? pomo_mode(&snap.clock.state) : POMO_MODE_IDLE;
        int wait_ms = have && mode == POMO_MODE_RUNNING ? POLL_RUNNING_MS : POLL_IDLE_MS;

        pomo_press_t in;
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
        }
        pomo_client_log_state();
        diag_note_stack("pomo", (int)uxTaskGetStackHighWaterMark(NULL));
    }
}

void pomo_client_init(void)
{
    if (s_queue) return;
    pomo_clock_init(&s_snap.clock);
    pomo_srv_clock_init(&s_srv);
    link_state_init(&s_src, esp_timer_get_time() / 1000);
    s_src_word = link_state_word(&s_src);
    s_queue = xQueueCreate(4, sizeof(pomo_press_t));
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
        diag_note_stack("pomo", -1);
        ESP_LOGI(TAG, "legacy poll task removed");
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
    publish_forget();
    ESP_LOGE(TAG, "no memory for the Pomodoro poll task; retry in %u s", (unsigned)(retry_ms / 1000));
}

bool pomo_client_next_action(uint32_t wait_ms, pomo_press_t *in)
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
    if (server_now > 0) pomo_srv_clock_note(&s_srv, server_now, sent, received);
    if (!s) {
        publish_disabled();
        return;
    }
    double now = pomo_client_now();
    taskENTER_CRITICAL(&s_lock);
    if (counting && s_srv.valid) pomo_clock_sync_end(&s_snap.clock, s, ends_at, pomo_srv_clock_offset(&s_srv), now);
    else if (!counting) pomo_clock_sync(&s_snap.clock, s, now);
    s_snap.disabled = false;
    taskEXIT_CRITICAL(&s_lock);
}

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
    pomo_press_t p = {.in = in, .link = ember_client_link_word(), .src = src_word()};
    if (ember_client_offline() || link_word_offline(p.src)) {
        publish_action(POMO_ACTION_OFFLINE);
        return;
    }
    if (s_queue && xQueueSend(s_queue, &p, 0) == pdTRUE) ember_client_wake();
}

bool pomo_client_get(pomo_snapshot_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_snap;
    taskEXIT_CRITICAL(&s_lock);
    out->online = !ember_client_offline() && !link_word_offline(src_word());
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
        case POMO_ACTION_OFFLINE: return "OFFLINE: NOT SENT";
        default: return "EMBER ERROR";
        }
    }
    if (ember_client_link() == EMBER_LINK_UNAUTHORIZED || !config_store_has_device()) return "NOT PAIRED";
    if (snap->disabled) return "POMODORO OFF";
    return NULL;
}
