#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEV_TOKEN_MAX 64

typedef struct {
    const char *fw;
    const char *fw_build;
    const ota_report_t *ota;
    const char *ip;
    int rssi;
    uint32_t heap_internal_free, heap_internal_largest;
    int64_t uptime_s;
    uint32_t config_version;
    int link_mhz;
    bool link_fallback;
    const struct dev_wifi *wifi;
    const struct dev_diag *diag;
} dev_checkin_t;

typedef struct dev_wifi {
    int channel;
    bool has_bssid;
    uint8_t bssid[6];
    uint32_t disconnects;
    int last_reason;
    bool has_rssi_min;
    int rssi_min;
} dev_wifi_t;

#define DEV_DIAG_MAX_TASKS 12
#define DEV_TASK_NAME_MAX 16
typedef struct dev_diag {
    const char *reset_reason;
    uint32_t boots;
    uint32_t heap_internal_min, heap_largest_min;
    int n_tasks;
    struct {
        char name[DEV_TASK_NAME_MAX + 1];
        uint32_t stack_free; /* bytes, minimum since boot */
    } tasks[DEV_DIAG_MAX_TASKS];
    bool has_crash;
    const char *crash_reason;
    uint32_t crash_pc;
    char crash_task[DEV_TASK_NAME_MAX + 1];
    bool has_crash_id;
    uint32_t crash_id, crash_size;
    char crash_elf[OTA_BUILD_HEX + 1];
} dev_diag_t;

/* A dump is only written by a panic or watchdog path; any other reset reason seen with a new dump gives "unknown". */
const char *dev_crash_reason_name(int reset_reason);

/* Collects the first name from esp_task_wdt_print_triggered_tasks messages; ISR-safe (no allocation, no libc stdio). */
typedef struct {
    int state;
    char name[DEV_TASK_NAME_MAX + 1];
} dev_twdt_capture_t;
void dev_twdt_capture_msg(dev_twdt_capture_t *c, const char *msg);

/* crash.task: the watchdog culprit for task_wdt ("twdt" if none was recorded), else the dump's task. */
const char *dev_crash_task(const char *reason, const char *wdt_culprit, const char *dump_task);

#define DEV_STATS_MAX_CPU 8
typedef struct {
    bool no_window;
    uint32_t period_ms;
    int n_cpu;
    float cpu_pct[DEV_STATS_MAX_CPU];
    bool has_heap_min;
    uint32_t heap_internal_min;
    bool has_psram;
    uint32_t psram_free, psram_min, psram_largest;
    bool has_temp;
    float temp_c;
    const char *reset_reason;
    bool has_req;
    uint32_t req_ok, req_fail;
    float req_ms_avg;
    uint32_t req_ms_max;
    bool has_frames;
    float fps, frame_ms_avg;
    uint32_t frame_ms_max;
} dev_stats_t;

size_t dev_checkin_body(const dev_checkin_t *c, const dev_stats_t *stats, char *out, size_t cap);

const char *dev_reset_reason_name(int reason);
bool dev_window_ok(int64_t period_us);
float dev_cpu_pct(uint64_t idle_delta_us, uint64_t period_us);

#define DEV_LIVE_MAX_MS (10 * 60 * 1000)
int64_t dev_live_deadline_ms(long long live_until, long long server_now, int64_t now_ms);

typedef struct {
    bool ok;
    uint32_t config_version;
    char *config;                 /* malloc'd compact JSON of "config", or NULL; caller frees */
    bool has_new_token;
    char new_token[DEV_TOKEN_MAX + 1];
    long long diag_live_until;   /* server Unix s; 0 = absent */
    bool has_coredump_wanted, has_coredump_ack;
    uint32_t coredump_wanted, coredump_ack;
    bool has_ota;
    ota_offer_t ota;
} dev_checkin_result_t;

#define DEV_OTA_MAX_SIZE (4u << 20)
void dev_checkin_parse(const char *json, dev_checkin_result_t *out);
void dev_checkin_result_free(dev_checkin_result_t *r);

/* Checkin schedule: all times in ms on the caller's monotonic clock. */
#define DEV_CHECKIN_PERIOD_MS 60000
#define DEV_CHECKIN_BACKOFF_MAX_MS 300000
#define DEV_CHECKIN_MIN_GAP_MS 2000
#define DEV_CHECKIN_LIVE_MS 5000

typedef struct {
    int64_t next_ms;
    int64_t last_ms;
    int backoff_ms;
    bool have_epoch;
    char epoch[24];
    int64_t live_until_ms;
    int period_ms;
    int live_ms;
} dev_sched_t;

int dev_checkin_period_ms(int stats_interval_ms, bool diag_on);
bool dev_stats_due(int64_t window_ms, int stats_interval_ms, int period_ms, bool live, bool first);

void dev_sched_init(dev_sched_t *s, int64_t now_ms);
bool dev_sched_due(const dev_sched_t *s, int64_t now_ms);
void dev_sched_epoch(dev_sched_t *s, const char *epoch, int64_t now_ms);
void dev_sched_epoch_reset(dev_sched_t *s);
typedef enum { DEV_CHECKIN_OK, DEV_CHECKIN_UNAUTHORIZED, DEV_CHECKIN_FAILED } dev_checkin_outcome_t;
void dev_sched_done(dev_sched_t *s, dev_checkin_outcome_t o, int64_t now_ms);
void dev_sched_intervals(dev_sched_t *s, int period_ms, int live_ms, int64_t now_ms);
void dev_sched_live(dev_sched_t *s, int64_t deadline_ms, int64_t now_ms);
void dev_sched_now(dev_sched_t *s, int64_t now_ms);

typedef enum { DEV_LINK_OFF, DEV_LINK_CONNECTING, DEV_LINK_OK, DEV_LINK_UNREACHABLE, DEV_LINK_UNAUTHORIZED } dev_link_t;

dev_link_t dev_link_combine(dev_link_t poll, bool has_device, bool checked_in, bool unauthorized);

#ifdef __cplusplus
}
#endif
