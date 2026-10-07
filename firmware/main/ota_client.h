#pragma once

#include <stdbool.h>

#include "device_api.h"
#include "http_conn.h"
#include "ota_policy.h"

/* Once in app_main after NVS init, before the ember task starts. */
void ota_client_boot(void);

/* Any task. */
void ota_client_note_frame(void);
void ota_client_note_view_ok(void);
void ota_client_note_input(void);
bool ota_client_checkin_blocked(void);
/* True while the running image is pending verification: hold planned reboots. */
bool ota_client_verifying(void);
/* CINDER1 ota_fault (CONFIG_CINDER_OTA_TEST): "net" or "sha" for the next download; false otherwise. */
bool ota_client_fault(const char *fault);

/* Callbacks run on the ember task. */
typedef struct {
    http_conn_t *conn;
    const char *base;
    bool online;
    bool paired;
    void (*alive)(void);
    bool (*pomo_active)(void);
    bool (*fresh_view)(void);
    void (*checkin)(void);
    void (*checkin_soon)(void);
} ota_ctx_t;

/* Ember task: the checkin's ota object, filled before each checkin. */
void ota_client_report(ota_report_t *out);
const char *ota_client_build(void);
/* Ember task, after every checkin; r is NULL unless status is 200. */
void ota_client_checkin_done(int status, const dev_checkin_result_t *r, const ota_ctx_t *ctx);
/* Ember task, once per loop pass: rollback timer, start or finish an update. */
void ota_client_service(const ota_ctx_t *ctx);
