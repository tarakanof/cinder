#include "day_client.h"

#include <math.h>
#include <string.h>

#include "config_store.h"
#include "ember_client.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pomo_client.h"

static const char *TAG = "day";

#define QUEUE 4
#define ANS_MAX 512

typedef struct {
    day_act_t a;
    double at;
} day_queued_t;

typedef struct {
    char url[CFG_URL_MAX + 40];
    char body[64];
    char ans[ANS_MAX];
} day_io_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static EXT_RAM_BSS_ATTR day_snapshot_t s_snap;
static EXT_RAM_BSS_ATTR day_queued_t s_q[QUEUE];
static EXT_RAM_BSS_ATTR day_io_t s_io;

void day_client_init(void)
{
    taskENTER_CRITICAL(&s_lock);
    memset(&s_snap, 0, sizeof s_snap);
    taskEXIT_CRITICAL(&s_lock);
}

void day_client_feed(const day_block_t *b, bool has_utc_offset, int utc_offset)
{
    double srv = 0;
    bool have_srv = pomo_client_srv_offset(&srv);
    double now = pomo_client_now();
    taskENTER_CRITICAL(&s_lock);
    if (b && (!s_snap.has || memcmp(&s_snap.b, b, sizeof *b) != 0)) {
        s_snap.b = *b;
        s_snap.fed_at = now;
    }
    s_snap.fed = true;
    s_snap.has = b != NULL;
    s_snap.have_srv = have_srv;
    s_snap.srv_offset = srv;
    s_snap.has_utc_offset = has_utc_offset;
    s_snap.utc_offset = has_utc_offset ? utc_offset : 0;
    int off;
    if (b && have_srv && day_offset_from_block(b, (long long)floor(now + srv), &off)) {
        s_snap.has_day_offset = true;
        s_snap.day_offset_min = off;
    }
    s_snap.gen++;
    taskEXIT_CRITICAL(&s_lock);
}

static int expire_locked(double now)
{
    int k = 0, dropped = 0;
    for (int i = 0; i < s_snap.queued; i++) {
        if (now - s_q[i].at > DAY_ACT_TTL_S) {
            dropped++;
            continue;
        }
        s_q[k++] = s_q[i];
    }
    s_snap.queued = k;
    if (dropped) {
        s_snap.status = -1;
        s_snap.status_at = now;
    }
    return dropped;
}

void day_client_act(const day_act_t *a)
{
    const char *path = day_act_path(a->kind);
    if (!path) return;
    double now = pomo_client_now();
    if (ember_client_offline()) {
        taskENTER_CRITICAL(&s_lock);
        s_snap.status = DAY_ACTION_OFFLINE;
        s_snap.status_at = now;
        taskEXIT_CRITICAL(&s_lock);
        ESP_LOGW(TAG, "%s dropped: offline", path);
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    day_queued_t *last = s_snap.queued ? &s_q[s_snap.queued - 1] : NULL;
    if (a->kind == DAY_ACT_STOP_AT && last && last->a.kind == DAY_ACT_STOP_AT) {
        *last = (day_queued_t){.a = *a, .at = now};
    } else {
        if (s_snap.queued == QUEUE) {
            memmove(&s_q[0], &s_q[1], sizeof s_q[0] * (QUEUE - 1));
            s_snap.queued--;
        }
        s_q[s_snap.queued++] = (day_queued_t){.a = *a, .at = now};
    }
    taskEXIT_CRITICAL(&s_lock);
    ember_client_wake();
}

bool day_client_pending(void)
{
    double now = pomo_client_now();
    taskENTER_CRITICAL(&s_lock);
    expire_locked(now);
    bool n = s_snap.queued > 0;
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

static bool pop(day_queued_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    bool got = s_snap.queued > 0;
    if (got) {
        *out = s_q[0];
        memmove(&s_q[0], &s_q[1], sizeof s_q[0] * (QUEUE - 1));
        s_snap.queued--;
        s_snap.sending = true;
    }
    taskEXIT_CRITICAL(&s_lock);
    return got;
}

static void done(int status)
{
    double now = pomo_client_now();
    taskENTER_CRITICAL(&s_lock);
    s_snap.sending = false;
    s_snap.status = status;
    s_snap.status_at = now;
    if (status == 0) s_snap.done_gen = s_snap.gen;
    taskEXIT_CRITICAL(&s_lock);
}

static int put_read(void *ctx, int off, const char **chunk)
{
    const char *body = ctx;
    int len = (int)strlen(body);
    if (off >= len) return 0;
    *chunk = body + off;
    return len - off;
}

void day_client_run(http_conn_t *conn)
{
    day_queued_t q;
    while (pop(&q)) {
        const char *path = day_act_path(q.a.kind);
        double age = pomo_client_now() - q.at;
        if (age > DAY_ACT_TTL_S) {
            ESP_LOGW(TAG, "%s dropped: %.0f s old", path, age);
            done(-1);
            continue;
        }
        if (!config_store_has_device() || ember_client_link() == EMBER_LINK_UNAUTHORIZED) {
            ESP_LOGW(TAG, "%s: not paired with Ember", path);
            done(401);
            continue;
        }
        int len = day_act_body(&q.a, s_io.body, sizeof s_io.body);
        if (len < 0) {
            done(-1);
            continue;
        }
        config_store_ember_url(s_io.url, sizeof s_io.url);
        strlcat(s_io.url, "/v1/day/", sizeof s_io.url);
        strlcat(s_io.url, path, sizeof s_io.url);
        bool put = day_act_put(q.a.kind);
        http_req_opts_t o = {.idempotent = true, .auth = true};
        if (put) {
            o.stream_len = len;
            o.read = put_read;
            o.read_ctx = s_io.body;
        } else {
            o.post_body = s_io.body;
        }
        int64_t t0 = esp_timer_get_time();
        int status = http_conn_req_opts(conn, s_io.url, &o, s_io.ans, sizeof s_io.ans);
        ESP_LOGI(TAG, "%s %s %s -> HTTP %d in %lld ms", put ? "PUT" : "POST", path, s_io.body, status,
                 (long long)((esp_timer_get_time() - t0) / 1000));
        if (status == 401) ember_client_report_unauthorized();
        done(status >= 200 && status < 300 ? 0 : status);
    }
}

void day_client_get(day_snapshot_t *out)
{
    double now = pomo_client_now();
    taskENTER_CRITICAL(&s_lock);
    int dropped = expire_locked(now);
    *out = s_snap;
    taskEXIT_CRITICAL(&s_lock);
    if (dropped) ESP_LOGW(TAG, "%d action(s) dropped: not sent within %.0f s", dropped, DAY_ACT_TTL_S);
}

int day_client_now_min(const day_snapshot_t *s, double now, bool *exact)
{
    *exact = s->have_srv && s->has_day_offset;
    if (!s->have_srv || (!s->has_day_offset && !s->has_utc_offset)) return DAY_NONE;
    long long m = (long long)floor((now + s->srv_offset) / 60.0) +
                  (s->has_day_offset ? s->day_offset_min : s->utc_offset / 60);
    return (int)(((m % DAY_MINUTES) + DAY_MINUTES) % DAY_MINUTES);
}

bool day_client_busy(const day_snapshot_t *s, double now)
{
    if (s->queued || s->sending) return true;
    return s->status == 0 && s->status_at > 0 && s->done_gen == s->gen && now - s->status_at < DAY_BUSY_S;
}

const char *day_client_note(const day_snapshot_t *s, double now)
{
    if (s->status && now - s->status_at < DAY_NOTE_S) {
        switch (s->status) {
        case 401: return "not paired";
        case 404: return "day clock off";
        case 409: return "changed: try again";
        case 429: return "slow down";
        case -1: return "no connection";
        case DAY_ACTION_OFFLINE: return "offline: not sent";
        default: return "Ember error";
        }
    }
    if (ember_client_link() == EMBER_LINK_UNAUTHORIZED || !config_store_has_device()) return "not paired";
    if (ember_client_offline()) return "offline";
    return NULL;
}
