#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLAIM_FEATURE "lan_pair"
#define CLAIM_KEY_BYTES 32
#define CLAIM_KEY_LEN 43
#define CLAIM_ID_MAX 32
#define CLAIM_CODE_LEN 6
#define CLAIM_BODY_MAX 160
#define CLAIM_IGNORED_MAX 3
#define CLAIM_EXPIRES_S_DEFAULT 600
#define CLAIM_EXPIRES_S_MAX 3600
#define CLAIM_POLL_S_DEFAULT 5
#define CLAIM_POLL_S_MIN 2
#define CLAIM_POLL_S_MAX 60
#define CLAIM_OLD_SERVER_MS (60 * 60 * 1000)
#define CLAIM_LIVE_MS (5 * 60 * 1000)
#define CLAIM_BACKOFF_MIN_MS 30000
#define CLAIM_BACKOFF_MAX_MS 600000
#define CLAIM_RETRY_AFTER_S_MAX 3600

typedef enum { CLAIM_IDLE, CLAIM_PROBE, CLAIM_OPEN, CLAIM_SHOW, CLAIM_INPUT, CLAIM_WAIT } claim_phase_t;
typedef enum { CLAIM_ACT_NONE, CLAIM_ACT_PROBE, CLAIM_ACT_OPEN, CLAIM_ACT_POLL } claim_act_t;
typedef enum { CLAIM_POLL_PENDING, CLAIM_POLL_APPROVED, CLAIM_POLL_GONE, CLAIM_POLL_FAILED } claim_poll_t;

/* Times in ms on the caller's monotonic clock. key is the secret: keep the struct in PSRAM, never in NVS or logs. */
typedef struct {
    claim_phase_t phase;
    claim_phase_t after_wait;
    bool has_key;
    char key[CLAIM_KEY_LEN + 1];
    char id[CLAIM_ID_MAX + 1];
    char code[CLAIM_CODE_LEN + 1];
    int64_t due_ms;
    int64_t expires_ms;
    int64_t deadline_ms;
    int poll_ms;
    int ignored;
    int backoff_ms;
} claim_t;

typedef struct {
    char claim_id[CLAIM_ID_MAX + 1];
    char code[CLAIM_CODE_LEN + 1];
    int expires_in_s;
    int poll_s;
} claim_reply_t;

typedef struct {
    char device_id[CFG_DEV_ID_MAX + 1];
    char name[CFG_NAME_MAX + 1];
    char token[CFG_TOKEN_MAX + 1];
} claim_approved_t;

/* Unpadded base64url of n bytes; false when it does not fit cap with its NUL. */
bool claim_b64url(const uint8_t *in, size_t n, char *out, size_t cap);

/* 0 when out is too small or a value is not JSON-safe. */
size_t claim_open_body(const char *hw_id, const char *fw, const char *key, char *out, size_t cap);
size_t claim_poll_body(const char *claim_id, const char *key, char *out, size_t cap);

/* GET /version: true when "features" lists CLAIM_FEATURE. */
bool claim_server_pairs(const char *json);
bool claim_reply_parse(const char *json, claim_reply_t *out);
/* 202 and 200 bodies; *approved filled only for "approved". Every string in the parsed tree is wiped before it is freed. */
claim_poll_t claim_poll_parse(const char *json, claim_approved_t *approved);

void claim_init(claim_t *c);
/* Drops the claim and wipes the key. */
void claim_reset(claim_t *c);
/* want: an Ember URL and no valid device token. false resets; true starts from idle with a probe. */
void claim_want(claim_t *c, bool want, int64_t now_ms);
/* A push, turn or touch: after CLAIM_IGNORED_MAX ignored codes the next claim waits for one. */
void claim_input(claim_t *c, int64_t now_ms);
/* The request due now, if any; a code past its expiry counts as ignored here. */
claim_act_t claim_next(claim_t *c, int64_t now_ms);
/* The key is made when needed and kept in RAM, so a re-post after a lost reply reuses it; a claim that ends (expiry, a poll 404) drops it. */
bool claim_needs_key(const claim_t *c);
void claim_set_key(claim_t *c, const uint8_t rnd[CLAIM_KEY_BYTES]);
/* Retry-After in delta seconds, clamped to 1..CLAIM_RETRY_AFTER_S_MAX; -1 when absent or not a number. */
int claim_retry_after_s(const char *hdr);
/* status -1: no answer. A lost claim reply re-posts the same key; 409 and 429 drop it and wait retry_after_s (-1: none). */
void claim_probed(claim_t *c, int status, const char *body, int64_t now_ms);
void claim_opened(claim_t *c, int status, const char *body, int retry_after_s, int64_t now_ms);
claim_poll_t claim_polled(claim_t *c, int status, const char *body, int retry_after_s, int64_t now_ms, claim_approved_t *approved);
/* After CLAIM_POLL_APPROVED: ok resets the claim, otherwise the next poll asks again. */
void claim_stored(claim_t *c, bool ok);
/* The 6-digit code while one is shown and not expired. */
bool claim_code(const claim_t *c, int64_t now_ms, char out[CLAIM_CODE_LEN + 1]);

#ifdef __cplusplus
}
#endif
