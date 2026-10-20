#include "claim_client.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "claim.h"
#include "config_store.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char *TAG = "claim";

typedef struct {
    claim_t c;
    claim_approved_t ok;
    char body[CLAIM_BODY_MAX];
    char url[CFG_URL_MAX + 32];
    char retry_after[12];
    uint32_t gen;
    unsigned cancel;
    int last_status;
} claim_ctx_t;

static claim_ctx_t *C;
static atomic_int s_code = -1;
static atomic_uint s_cancel;
static atomic_bool s_input;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void wipe(void *p, size_t n)
{
    volatile uint8_t *b = p;
    while (n--) *b++ = 0;
}

static void on_header(const char *name, const char *value, void *ctx)
{
    if (strcasecmp(name, "Retry-After") == 0) strlcpy(ctx, value, sizeof C->retry_after);
}

static int post(http_conn_t *conn, const char *base, const char *path, bool idempotent, char *buf, int cap)
{
    snprintf(C->url, sizeof C->url, "%s%s", base, path);
    C->retry_after[0] = 0;
    const http_req_opts_t o = {
        .post_body = C->body, .idempotent = idempotent, .anon = true, .on_header = on_header, .hdr_ctx = C->retry_after};
    int status = http_conn_req_opts(conn, C->url, &o, buf, cap);
    wipe(C->body, sizeof C->body);
    return status;
}

static void note(int status, const char *what)
{
    if (status == C->last_status) return;
    C->last_status = status;
    if (status != 200 && status != 201 && status != 202) ESP_LOGW(TAG, "LAN pairing: %s -> HTTP %d", what, status);
}

static void phase_log(claim_phase_t was, int64_t now)
{
    const claim_t *c = &C->c;
    if (c->phase == was) return;
    switch (c->phase) {
    case CLAIM_SHOW: ESP_LOGI(TAG, "LAN pairing: claim %s open for %d s", c->id, (int)((c->expires_ms - now) / 1000)); break;
    case CLAIM_INPUT: ESP_LOGI(TAG, "LAN pairing: %d claims ended unused; the next waits for a push or turn", CLAIM_IGNORED_MAX); break;
    case CLAIM_WAIT:
        if (c->after_wait == CLAIM_PROBE && c->due_ms - now == CLAIM_OLD_SERVER_MS)
            ESP_LOGW(TAG, "LAN pairing: this Ember does not offer it; asking again in %d min", CLAIM_OLD_SERVER_MS / 60000);
        else ESP_LOGW(TAG, "LAN pairing: retry in %d s", (int)((c->due_ms - now) / 1000));
        break;
    case CLAIM_IDLE: ESP_LOGI(TAG, "LAN pairing: off"); break;
    default: break;
    }
}

static void probe(http_conn_t *conn, const char *base, char *buf, int cap, int64_t now)
{
    snprintf(C->url, sizeof C->url, "%s/version", base);
    const http_req_opts_t o = {.idempotent = true, .anon = true};
    int status = http_conn_req_opts(conn, C->url, &o, buf, cap);
    note(status, "GET /version");
    claim_probed(&C->c, status, status > 0 ? buf : NULL, now);
}

static void open_claim(http_conn_t *conn, const char *base, char *buf, int cap, int64_t now)
{
    if (claim_needs_key(&C->c)) {
        uint8_t rnd[CLAIM_KEY_BYTES];
        esp_fill_random(rnd, sizeof rnd);
        claim_set_key(&C->c, rnd);
        wipe(rnd, sizeof rnd);
    }
    char hw[13];
    config_store_hw_id(hw);
    int status = -1;
    if (claim_open_body(hw, esp_app_get_description()->version, C->c.key, C->body, sizeof C->body))
        status = post(conn, base, "/v1/devices/claim", false, buf, cap);
    note(status, "claim");
    claim_opened(&C->c, status, status > 0 ? buf : NULL, claim_retry_after_s(C->retry_after), now);
}

static bool poll_claim(http_conn_t *conn, const char *base, char *buf, int cap, int64_t now)
{
    int status = -1;
    if (claim_poll_body(C->c.id, C->c.key, C->body, sizeof C->body)) status = post(conn, base, "/v1/devices/claim/poll", true, buf, cap);
    note(status, "claim poll");
    claim_poll_t r = claim_polled(&C->c, status, status > 0 ? buf : NULL, claim_retry_after_s(C->retry_after), now, &C->ok);
    if (status > 0) wipe(buf, strnlen(buf, (size_t)cap));
    if (r == CLAIM_POLL_GONE) ESP_LOGI(TAG, "LAN pairing: the claim ended without an approval");
    if (r != CLAIM_POLL_APPROVED) return false;
    esp_err_t err = config_store_pair(C->gen, base, C->ok.device_id, C->ok.token, C->ok.name);
    bool stored = err == ESP_OK;
    if (stored) {
        ESP_LOGI(TAG, "LAN pairing: approved, paired as %s; checking in", C->ok.device_id);
        claim_stored(&C->c, true);
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "LAN pairing: approval dropped, USB set the pairing meanwhile");
        claim_reset(&C->c);
    } else {
        ESP_LOGE(TAG, "LAN pairing: storing the approval failed: %s; asking again", esp_err_to_name(err));
        claim_stored(&C->c, false);
    }
    wipe(&C->ok, sizeof C->ok);
    return stored;
}

bool claim_client_service(http_conn_t *conn, const char *base, bool online, bool unauthorized, char *buf, int cap)
{
    if (!C) {
        C = heap_caps_calloc(1, sizeof *C, MALLOC_CAP_SPIRAM);
        if (!C) return false;
        claim_init(&C->c);
        C->gen = config_store_token_gen();
        C->cancel = atomic_load(&s_cancel);
    }
    int64_t now = now_ms();
    uint32_t gen = config_store_token_gen();
    unsigned cancel = atomic_load(&s_cancel);
    if (gen != C->gen || cancel != C->cancel) {
        claim_phase_t was = C->c.phase;
        claim_reset(&C->c);
        phase_log(was, now);
        C->gen = gen;
        C->cancel = cancel;
    }
    claim_phase_t was = C->c.phase;
    claim_want(&C->c, config_store_has_ember_url() && (!config_store_has_device() || unauthorized), now);
    if (atomic_exchange(&s_input, false)) claim_input(&C->c, now);
    bool stored = false;
    claim_act_t act = claim_next(&C->c, now);
    switch (online ? act : CLAIM_ACT_NONE) {
    case CLAIM_ACT_PROBE: probe(conn, base, buf, cap, now); break;
    case CLAIM_ACT_OPEN: open_claim(conn, base, buf, cap, now); break;
    case CLAIM_ACT_POLL: stored = poll_claim(conn, base, buf, cap, now); break;
    default: break;
    }
    if (!stored) phase_log(was, now);
    char shown[CLAIM_CODE_LEN + 1];
    atomic_store(&s_code, claim_code(&C->c, now_ms(), shown) ? atoi(shown) : -1);
    return stored;
}

int claim_client_code(void) { return atomic_load(&s_code); }

void claim_client_input(void) { atomic_store(&s_input, true); }

void claim_client_cancel(void)
{
    atomic_fetch_add(&s_cancel, 1);
    atomic_store(&s_code, -1);
}
