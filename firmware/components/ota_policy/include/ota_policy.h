#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_URL_PREFIX "/v1/devices/self/firmware/"
#define OTA_VERSION_MAX 31
#define OTA_SHA_HEX 64
#define OTA_BUILD_HEX 8
#define OTA_ERROR_MAX 24
#define OTA_HEADER_LEN 288
#define OTA_CHIP_ESP32S3 9
#define OTA_PROJECT "cinder"

#define OTA_ATTEMPTS_PER_BOOT 3
#define OTA_RESUMES_MAX 3
#define OTA_ATTEMPT_MS (5 * 60 * 1000)
#define OTA_VALID_AFTER_MS (60 * 1000)
#define OTA_ROLLBACK_MS (30 * 60 * 1000)
#define OTA_ROLLBACK_POMO_MS (60 * 60 * 1000)
#define OTA_AUTO_IDLE_MS (10 * 60 * 1000)
#define OTA_MARK_TRIES 3
#define OTA_BOOTLOADER_ROLLBACK_VER 2

typedef struct {
    uint32_t attempt;
    bool is_auto;
    bool retry;
    char build[OTA_BUILD_HEX + 1];
    char sha256[OTA_SHA_HEX + 1];
    uint32_t size;
    char version[OTA_VERSION_MAX + 1];
} ota_offer_t;

bool ota_semver_valid(const char *s);
bool ota_hex_valid(const char *s, size_t len);
/* False for a malformed offer; url must be OTA_URL_PREFIX + version, size 288..slot_size. */
bool ota_offer_parse(const cJSON *o, uint32_t slot_size, ota_offer_t *out);

typedef enum { OTA_RES_NONE, OTA_RES_OK, OTA_RES_FAILED, OTA_RES_ROLLED_BACK } ota_result_t;
typedef enum { OTA_ATT_NONE, OTA_ATT_DL, OTA_ATT_BOOT, OTA_ATT_READY } ota_att_state_t;

typedef struct {
    ota_result_t result;
    uint32_t attempt;
    char error[OTA_ERROR_MAX + 1];
    char version[OTA_VERSION_MAX + 1];
} ota_last_t;

/* Persisted in NVS namespace "ota", one key per field; a new field must be optional for older images. */
typedef struct {
    ota_att_state_t att_state;
    uint32_t att_attempt;
    char att_sha[OTA_SHA_HEX + 1];
    char att_ver[OTA_VERSION_MAX + 1];
    char att_build[OTA_BUILD_HEX + 1];
    ota_last_t last;
    char bad[OTA_SHA_HEX + 1];
} ota_rec_t;

const char *ota_result_name(ota_result_t r);
void ota_rec_start(ota_rec_t *r, const ota_offer_t *o);
void ota_rec_fail(ota_rec_t *r, const char *error, bool mark_bad);
/* Downloaded and verified, boot slot not set yet (waiting out a Pomodoro). */
void ota_rec_ready(ota_rec_t *r);
/* Saved before esp_ota_set_boot_partition, so a power cut after the switch still reports ok. */
void ota_rec_rebooting(ota_rec_t *r);
/* True when the record changed (one NVS write per refused attempt). */
bool ota_rec_refuse(ota_rec_t *r, const ota_offer_t *o);
/* retry:true from Ember clears the bad-image guard for that image; true when the record changed. */
bool ota_rec_retry(ota_rec_t *r, const ota_offer_t *o);
void ota_rec_valid(ota_rec_t *r);
/* The new image gives up: the old image finds this record after the bootloader switched back. */
bool ota_rec_rollback(ota_rec_t *r, const char *error);

typedef struct {
    bool pending_verify;
    const char *running_build;
    const char *running_ver;
    bool has_invalid;
    const char *invalid_build;
    const char *invalid_ver;
} ota_boot_in_t;
typedef enum { OTA_BOOT_NOTHING, OTA_BOOT_VERIFY, OTA_BOOT_OK, OTA_BOOT_ROLLED_BACK, OTA_BOOT_INTERRUPTED } ota_boot_t;
ota_boot_t ota_rec_boot(ota_rec_t *r, const ota_boot_in_t *in);

/* Per-boot RAM state of the start gate; times in ms on the caller's monotonic clock. */
typedef struct {
    char sha[OTA_SHA_HEX + 1];
    int started;
    int fails;
    int64_t next_ms;
    uint32_t retry_attempt;
    uint32_t attempt;
} ota_gate_t;

typedef struct {
    bool rollback;
    bool image_valid;
    bool online;
    bool paired;
    bool pomo_active;
    int64_t now_ms;
    int64_t last_input_ms;
} ota_env_t;

typedef enum {
    OTA_GO,
    OTA_WAIT_NOT_READY,
    OTA_WAIT_POMODORO,
    OTA_WAIT_INPUT,
    OTA_WAIT_BACKOFF,
    OTA_REFUSE_BAD,
    OTA_REFUSE_CAP,
} ota_decision_t;

const char *ota_decision_name(ota_decision_t d);
ota_decision_t ota_start_decide(ota_gate_t *g, const ota_rec_t *rec, const ota_offer_t *o, const ota_env_t *e);
void ota_gate_started(ota_gate_t *g);
void ota_gate_failed(ota_gate_t *g, int64_t now_ms);
int ota_backoff_ms(int fails);

bool ota_resumable(int status);
/* Wait before resume n (0-based); -1 when no resume is left. */
int ota_resume_delay_ms(int resumes_used);
typedef enum { OTA_RESP_CONTINUE, OTA_RESP_RESTART, OTA_RESP_FAIL } ota_resp_t;
/* error: OTA_ERROR_MAX + 1 bytes, set on OTA_RESP_FAIL. */
ota_resp_t ota_resp_check(int status, uint32_t requested_from, bool has_range, uint32_t range_start,
                          int64_t content_length, uint32_t size, char *error);
bool ota_content_range_start(const char *v, uint32_t *start);
void ota_http_error(int status, char *out);

int ota_idf_major(const char *idf_ver);
/* NULL when the first OTA_HEADER_LEN bytes match the offer (build NULL: not checked); else the error name ("desc"). */
const char *ota_header_check(const uint8_t *b, size_t n, const char *version, const char *build, const char *boot_idf_ver);

int ota_pct(uint32_t written, uint32_t size);

#define OTA_HEALTH_MIN_FRAMES 20
#define OTA_HEALTH_LOOP_MS (30 * 1000)
#define OTA_HEALTH_MIN_TOUCH_OK 1
#define OTA_HEALTH_HEAP_MIN (40 * 1024)
#define OTA_HEALTH_LARGEST_MIN (15 * 1024)
#define OTA_HEALTH_STACK_LVGL 1024
#define OTA_HEALTH_STACK_MIN 512
#define OTA_HEALTH_TASKS_MAX 12

typedef enum { OTA_LINK_PENDING, OTA_LINK_OK, OTA_LINK_FAIL } ota_link_t;
/* One completed link check: failed = confirmed failure (boot: check + 2 of 3 rechecks; soak: 2 in a row). */
ota_link_t ota_link_result(bool failed, bool fast_clock);

typedef struct {
    const char *name;
    uint32_t stack_free;
} ota_task_stack_t;

/* Ages in ms, -1 when the event never happened; heap and stack values are bytes, minimum since boot. */
typedef struct {
    ota_link_t link;
    uint32_t frames;
    int64_t loop_age_ms;
    uint32_t touch_ok;
    bool input_seen;
    uint32_t heap_internal_min;
    uint32_t heap_largest_min;
    int n_tasks;
    ota_task_stack_t tasks[OTA_HEALTH_TASKS_MAX];
} ota_health_in_t;

typedef enum { OTA_HEALTH_PENDING, OTA_HEALTH_PASS, OTA_HEALTH_FAIL } ota_health_state_t;
typedef struct {
    ota_health_state_t state;
    const char *reason;
} ota_health_t;
/* reason: the first check not passed (NULL on PASS); FAIL only for a link failure at the fallback clock. */
ota_health_t ota_health_check(const ota_health_in_t *in);

typedef struct {
    int64_t uptime_ms;
    bool checkin_ok;
    bool frame;
    bool view_ok;
    ota_health_state_t health;
} ota_valid_in_t;
bool ota_valid_ready(const ota_valid_in_t *in);
bool ota_rollback_due(int64_t uptime_ms, bool pomo_active, int64_t limit_ms);
/* The rollback reason now, or NULL to keep waiting; due: ota_rollback_due; checkin_seen: any 200 checkin this boot. */
const char *ota_verify_rollback(bool due, bool checkin_seen, const ota_health_t *h);
/* True when the rollback check needs the health input now (deadline due or a hard link fail). */
bool ota_verify_needs_health(bool due, ota_link_t link);

typedef enum { OTA_OVERRIDE_OK, OTA_OVERRIDE_NOT_PENDING, OTA_OVERRIDE_NO_CHECKIN, OTA_OVERRIDE_NOT_READY } ota_override_t;
/* CINDER1 ota_valid: v->checkin_ok is any 200 checkin this boot; v->health is ignored. */
ota_override_t ota_override_check(bool verifying, const ota_valid_in_t *v);
bool ota_rollback_capable(bool app_rollback, uint32_t bootloader_ver);

typedef struct {
    int fails;
    int64_t next_ms;
} ota_backoff_t;
bool ota_backoff_due(const ota_backoff_t *b, int64_t now_ms);
void ota_backoff_failed(ota_backoff_t *b, int64_t now_ms);

typedef enum { OTA_REBOOT_NONE, OTA_REBOOT_RESTART, OTA_REBOOT_FALLBACK } ota_reboot_kind_t;
typedef struct {
    ota_reboot_kind_t held;
} ota_reboot_gate_t;
/* The reboot to run now; NONE while the image is pending verification (held, a fallback outranks a restart). */
ota_reboot_kind_t ota_reboot_request(ota_reboot_gate_t *g, ota_reboot_kind_t k, bool verifying);
/* The held reboot, once, after the image is valid. */
ota_reboot_kind_t ota_reboot_release(ota_reboot_gate_t *g, bool verifying);

typedef struct {
    bool rollback;
    int slot;
    const char *image;
    const char *phase;
    const ota_last_t *last;
} ota_report_t;
/* esp_ota_img_states_t value; ok false when the running slot has no otadata entry. */
const char *ota_image_name(bool ok, uint32_t state);
size_t ota_report_json(const ota_report_t *r, char *out, size_t cap);

void ota_build_hex(const uint8_t *elf_sha, char out[OTA_BUILD_HEX + 1]);

#ifdef __cplusplus
}
#endif
