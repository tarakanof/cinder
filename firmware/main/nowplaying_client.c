#include "nowplaying_client.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "ember_client.h"
#include "config_store.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pomo_client.h"
#include "rom/tjpgd.h"

static const char *TAG = "np";

#define JPEG_MAX (96 * 1024)
#define FACE_DISK_R 188.0f   /* the ring band r 194..202 must stay black: np_ring_render composites on black */

typedef struct {
    np_snapshot_t snap;
    uint32_t gen;
    np_art_set_t pending;
    bool has_pending;
    bool built_valid;
    char built[NP_ARTV_MAX + 1];
    np_info_t last;
    bool last_have;
    int fails;
    char fail_version[NP_ARTV_MAX + 1];
    int64_t retry_at_ms;
    char url[160];
} np_shared_t;

static np_shared_t *S;

#define CMD_QUEUE 6
typedef struct {
    np_cmd_t cmd;
    int64_t at_ms;
    uint32_t id;
    char source[NP_SOURCE_MAX + 1];
    char track_id[NP_TRACK_ID_MAX + 1];
} np_queued_t;
typedef struct {
    np_queued_t q[CMD_QUEUE];
    int n;
    uint32_t next_id;
    uint32_t boot;
    np_ctl_result_t result;
    char body[320];
} np_cmds_t;
static np_cmds_t *C;
static SemaphoreHandle_t s_lock;
static atomic_bool s_visible;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void np_client_init(void)
{
    S = heap_caps_calloc(1, sizeof *S, MALLOC_CAP_SPIRAM);
    C = heap_caps_calloc(1, sizeof *C, MALLOC_CAP_SPIRAM);
    s_lock = xSemaphoreCreateMutex();
    assert(S && C && s_lock);
}

void np_art_set_free(np_art_set_t *s)
{
    free(s->backdrop);
    free(s->album);
    free(s->artist);
    memset(s, 0, sizeof *s);
}

void np_client_feed(const np_info_t *np)
{
    if (!S) return;
    double now = pomo_client_now(), offset = 0;
    bool have_off = pomo_client_srv_offset(&offset);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    np_snapshot_t *sn = &S->snap;
    bool changed = sn->have != (np != NULL);
    if (np) {
        bool same = S->last_have && np_same_track(&S->last, np);
        bool playing = np->state == NP_PLAYING;
        long long pos = have_off ? np_position_ms(np, offset, now) : np->position_ms;
        np_anchor_t before = sn->anchor;
        np_anchor_sync(&sn->anchor, same, playing, pos, np->duration_ms, now);
        changed |= memcmp(&before, &sn->anchor, sizeof before) != 0 || !same ||
                    strcmp(S->last.art_version, np->art_version) != 0 || S->last.album_art != np->album_art ||
                    S->last.artist_art != np->artist_art || S->last.volume != np->volume ||
                    strcmp(S->last.track_id, np->track_id) != 0;
        if (!same || !S->last_have)
            ESP_LOGI(TAG, "%s, %lld s, art %s", np->state == NP_PLAYING ? "playing" : np->state == NP_PAUSED ? "paused" : "nothing playing",
                     np->duration_ms / 1000, np->art_version[0] ? np->art_version : "none");
        sn->info = *np;
        S->last = *np;
    } else {
        memset(&sn->info, 0, sizeof sn->info);
        memset(&sn->anchor, 0, sizeof sn->anchor);
    }
    sn->have = np != NULL;
    S->last_have = np != NULL;
    if (changed) S->gen++;
    xSemaphoreGive(s_lock);
}

bool np_client_get(np_snapshot_t *out, uint32_t *gen)
{
    if (!S) return false;
    bool fresh = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (S->gen != *gen) {
        *out = S->snap;
        *gen = S->gen;
        fresh = true;
    }
    xSemaphoreGive(s_lock);
    return fresh;
}

void np_client_forget_art(void)
{
    if (!S) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (S->has_pending) np_art_set_free(&S->pending);
    S->has_pending = false;
    S->built_valid = false;
    xSemaphoreGive(s_lock);
}

void np_client_set_visible(bool on)
{
    if (!S || atomic_load(&s_visible) == on) return;
    atomic_store(&s_visible, on);
    if (!on) np_client_forget_art();
    else ember_client_wake();
}

bool np_client_take_art(np_art_set_t *out)
{
    if (!S) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool got = S->has_pending;
    if (got) {
        *out = S->pending;
        memset(&S->pending, 0, sizeof S->pending);
        S->has_pending = false;
    }
    xSemaphoreGive(s_lock);
    return got;
}

static bool due(np_art_plan_t *plan)
{
    if (!atomic_load(&s_visible) || !S->last_have) return false;
    np_art_plan(&S->last, plan);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool built = S->built_valid && strcmp(S->built, plan->version) == 0;
    xSemaphoreGive(s_lock);
    if (built) return false;
    if (S->fails && strcmp(S->fail_version, plan->version) == 0 && now_ms() < S->retry_at_ms) return false;
    return true;
}

bool np_client_pending(void)
{
    if (!S) return false;
    if (np_client_cmds_pending()) return true;
    np_art_plan_t plan;
    return due(&plan);
}

void np_client_control(const np_cmd_t *cmd, const char *source, const char *track_id)
{
    if (!S || cmd->kind == NP_CMD_NONE) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    np_queued_t *last = C->n ? &C->q[C->n - 1] : NULL;
    if (cmd->kind == NP_CMD_VOLUME && last && last->cmd.kind == NP_CMD_VOLUME &&
        strcmp(last->track_id, track_id ? track_id : "") == 0) {
        int d = last->cmd.delta + cmd->delta;
        last->cmd.delta = d < -100 ? -100 : d > 100 ? 100 : d;
        last->at_ms = now_ms();
    } else {
        if (C->n == CMD_QUEUE) {
            memmove(&C->q[0], &C->q[1], sizeof C->q[0] * (CMD_QUEUE - 1));
            C->n--;
        }
        np_queued_t *q = &C->q[C->n++];
        *q = (np_queued_t){.cmd = *cmd, .at_ms = now_ms(), .id = ++C->next_id};
        strlcpy(q->source, source ? source : "", sizeof q->source);
        strlcpy(q->track_id, track_id ? track_id : "", sizeof q->track_id);
    }
    xSemaphoreGive(s_lock);
    ember_client_wake();
}

bool np_client_visible(void) { return atomic_load(&s_visible); }

bool np_client_cmds_pending(void)
{
    if (!S) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool n = C->n > 0;
    xSemaphoreGive(s_lock);
    return n;
}

void np_client_control_result(np_ctl_result_t *out)
{
    if (!S) {
        memset(out, 0, sizeof *out);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = C->result;
    xSemaphoreGive(s_lock);
}

static bool cmd_pop(np_queued_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool got = C->n > 0;
    if (got) {
        *out = C->q[0];
        memmove(&C->q[0], &C->q[1], sizeof C->q[0] * (CMD_QUEUE - 1));
        C->n--;
    }
    xSemaphoreGive(s_lock);
    return got;
}

#define BURST_LINGER_MS 700

void np_client_run_controls(http_conn_t *conn, const char *base)
{
    np_queued_t q;
    int64_t last_ms = 0;
    bool last_volume = false;
    for (;;) {
        if (!cmd_pop(&q)) {
            if (!last_volume || now_ms() - last_ms >= BURST_LINGER_MS) return;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        const char *action = np_cmd_action(q.cmd.kind);
        if (now_ms() - q.at_ms > NP_CMD_TTL_MS) {
            ESP_LOGW(TAG, "control %s dropped: %lld ms old", action, (long long)(now_ms() - q.at_ms));
            continue;
        }
        if (ember_client_link() == EMBER_LINK_UNAUTHORIZED || !config_store_has_device()) {
            ESP_LOGW(TAG, "control %s: not paired with Ember", action);
            continue;
        }
        if (np_cmd_body(&q.cmd, q.source, q.track_id, C->body, sizeof C->body) < 0) continue;
        if (!C->boot) C->boot = esp_random() | 1;
        char key[24], ans[160];
        snprintf(key, sizeof key, "%08" PRIx32 "-%" PRIu32, C->boot, q.id);
        snprintf(S->url, sizeof S->url, "%s/v1/nowplaying/control", base);
        const http_req_opts_t o = {.post_body = C->body, .idempotent = true, .idem_key = key};
        int64_t t0 = now_ms();
        int status = http_conn_req_opts(conn, S->url, &o, ans, sizeof ans);
        ESP_LOGI(TAG, "control %s%s%d -> HTTP %d in %lld ms", action, q.cmd.kind == NP_CMD_VOLUME ? " " : "",
                 q.cmd.kind == NP_CMD_VOLUME ? q.cmd.delta : 0, status, (long long)(now_ms() - t0));
        /* A 401 is an Ember without #280 (owner-token catch-all), not a revoked token. */
        xSemaphoreTake(s_lock, portMAX_DELAY);
        C->result.status = status == 200 || status == 202 ? 0 : status;
        C->result.seq++;
        xSemaphoreGive(s_lock);
        last_ms = now_ms();
        last_volume = q.cmd.kind == NP_CMD_VOLUME;
    }
}

#define TJPGD_WORK 3100
typedef struct {
    const uint8_t *src;
    size_t len, pos;
    uint16_t *dst;
    int px;
} jd_io_t;

static UINT jd_in(JDEC *jd, BYTE *buf, UINT n)
{
    jd_io_t *io = jd->device;
    if (n > io->len - io->pos) n = io->len - io->pos;
    if (buf) memcpy(buf, io->src + io->pos, n);
    io->pos += n;
    return n;
}

static UINT jd_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jd_io_t *io = jd->device;
    const uint8_t *s = bitmap;
    for (int y = r->top; y <= r->bottom; y++) {
        uint16_t *d = io->dst + (size_t)y * io->px + r->left;
        for (int x = r->left; x <= r->right; x++, s += 3)
            *d++ = (uint16_t)(((s[0] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[2] >> 3));
    }
    return 1;
}

/* 466 is not a multiple of the 16 px MCU: TJpgDec clips the last MCU, so no padding. */
static void *decode(const uint8_t *jpg, int len, int px, bool alpha, int64_t *us)
{
    int64_t t0 = esp_timer_get_time();
    void *work = heap_caps_malloc(TJPGD_WORK, MALLOC_CAP_SPIRAM);
    if (!work) work = heap_caps_malloc(TJPGD_WORK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t plane = (size_t)px * px * 2;
    uint8_t *out = heap_caps_malloc(alpha ? plane + (size_t)px * px : plane, MALLOC_CAP_SPIRAM);
    jd_io_t io = {.src = jpg, .len = (size_t)len, .dst = (uint16_t *)out, .px = px};
    JDEC jd;
    JRESULT r = work && out ? jd_prepare(&jd, jd_in, work, TJPGD_WORK, &io) : JDR_MEM1;
    if (r == JDR_OK && ((int)jd.width != px || (int)jd.height != px)) {
        ESP_LOGW(TAG, "picture is %ux%u, want %d", jd.width, jd.height, px);
        r = JDR_PAR;
    }
    if (r == JDR_OK) r = jd_decomp(&jd, jd_out, 0);
    free(work);
    if (r != JDR_OK) {
        free(out);
        ESP_LOGW(TAG, "JPEG decode failed (%d)", (int)r);
        return NULL;
    }
    if (alpha) np_circle_alpha(out + plane, px);
    *us += esp_timer_get_time() - t0;
    return out;
}

void np_client_service(http_conn_t *conn, const char *base)
{
    np_art_plan_t plan;
    if (!S) return;
    np_client_run_controls(conn, base);
    if (!due(&plan)) return;
    np_art_set_t set = {0};
    strcpy(set.version, plan.version);
    int64_t t0 = esp_timer_get_time(), dec_us = 0;
    int bytes = 0;
    bool failed = false;
    uint8_t *jpg = plan.version[0] ? heap_caps_malloc(JPEG_MAX, MALLOC_CAP_SPIRAM) : NULL;
    if (plan.version[0] && !jpg) failed = true;
    for (int k = 0; k < NP_ART_KINDS && !failed; k++) {
        if (!plan.want[k] || !np_art_url(base, (np_art_kind_t)k, plan.version, S->url, sizeof S->url)) continue;
        int len = 0;
        const http_req_opts_t o = {.idempotent = true, .body_len = &len};
        int status = http_conn_req_opts(conn, S->url, &o, (char *)jpg, JPEG_MAX);
        if (status == 404) continue;
        if (status != 200) {
            ESP_LOGW(TAG, "art %s: HTTP %d", np_art_kind_name((np_art_kind_t)k), status);
            failed = true;
            break;
        }
        bytes += len;
        if (len >= JPEG_MAX) {
            ESP_LOGW(TAG, "art %s: %d B, over %d", np_art_kind_name((np_art_kind_t)k), len, JPEG_MAX);
            continue;
        }
        int px = np_art_px((np_art_kind_t)k);
        void *img = decode(jpg, len, px, k != NP_ART_BACKDROP, &dec_us);
        if (k == NP_ART_BACKDROP && img) {
            int64_t m0 = esp_timer_get_time();
            np_face_mask(img, px, px, px / 2.0f, px / 2.0f, FACE_DISK_R);
            dec_us += esp_timer_get_time() - m0;
        }
        if (k == NP_ART_BACKDROP) set.backdrop = img;
        else if (k == NP_ART_ALBUM) set.album = img;
        else set.artist = img;
    }
    free(jpg);
    if (failed) {
        np_art_set_free(&set);
        if (strcmp(S->fail_version, plan.version) != 0) S->fails = 0;
        strcpy(S->fail_version, plan.version);
        S->fails++;
        S->retry_at_ms = now_ms() + np_art_retry_ms(S->fails);
        return;
    }
    S->fails = 0;
    if (plan.version[0])
        ESP_LOGI(TAG, "art %s: %d B in %lld ms (decode %lld ms): backdrop %s, album %s, artist %s", plan.version,
                 bytes, (esp_timer_get_time() - t0) / 1000, dec_us / 1000, set.backdrop ? "yes" : "no",
                 set.album ? "yes" : "no", set.artist ? "yes" : "no");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (atomic_load(&s_visible)) {
        if (S->has_pending) np_art_set_free(&S->pending);
        S->pending = set;
        S->has_pending = true;
        S->built_valid = true;
        strcpy(S->built, plan.version);
        S->gen++;
    } else {
        np_art_set_free(&set);
    }
    xSemaphoreGive(s_lock);
}
