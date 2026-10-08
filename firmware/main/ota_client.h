#pragma once

#include <stdbool.h>

#include "device_api.h"
#include "http_conn.h"
#include "ota_policy.h"

/* Once in app_main after NVS init, before the ember task starts. */
void ota_client_boot(void);

/* Any task. */
void ota_client_note_frame(void);
/* LVGL task, every frame_cb tick (liveness of the LVGL loop). */
void ota_client_note_loop(void);
/* Link task: OK for a completed check, FAIL for a confirmed failure at the 40 MHz clock (sticky). */
void ota_client_note_link(ota_link_t r);
/* LVGL task: true while the no_render OTA test fault freezes the screen. */
bool ota_client_render_frozen(void);
void ota_client_note_view_ok(void);
void ota_client_note_input(void);
bool ota_client_checkin_blocked(void);
/* True while the running image is pending verification: hold planned reboots. */
bool ota_client_verifying(void);
/* CINDER1 ota_fault (CONFIG_CINDER_OTA_TEST): "net" or "sha" for the next download; false otherwise. */
bool ota_client_fault(const char *fault);

typedef enum { OTA_MARK_OK, OTA_MARK_NOT_PENDING, OTA_MARK_NO_CHECKIN, OTA_MARK_NOT_READY, OTA_MARK_BUSY, OTA_MARK_FAILED } ota_mark_t;
/* CINDER1 ota_valid, USB task: the ember task marks the pending image valid (health checks skipped); blocks up to 30 s. */
ota_mark_t ota_client_mark_valid(void);

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
