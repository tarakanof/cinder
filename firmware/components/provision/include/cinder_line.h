#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_PREFIX "CINDER1 "
#define CL_LINE_MAX 1024
#define CL_URL_MAX 128
#define CL_TOKEN_MAX 64
#define CL_DEVICE_ID_MAX 32
#define CL_NAME_MAX 32

typedef enum { CL_OP_INFO, CL_OP_SET_EMBER, CL_OP_STATUS, CL_OP_RESET, CL_OP_REBOOT, CL_OP_DIAG_OVERRIDE,
              CL_OP_SNAPSHOT ,
              CL_OP_CHASE ,
              CL_OP_INPUT ,
              CL_OP_OTA_FAULT } cl_op_t;
typedef enum {
    CL_INPUT_TURN,
    CL_INPUT_PUSH,
    CL_INPUT_LONG,
    CL_INPUT_TOUCH,
    CL_INPUT_PAGE,
    CL_INPUT_SWIPE_UP,
    CL_INPUT_SWIPE_DOWN,
    CL_INPUT_COUNT
} cl_input_t;

extern const char *const CL_INPUT_NAMES[CL_INPUT_COUNT];
typedef enum { CL_SCOPE_FACTORY, CL_SCOPE_EMBER, CL_SCOPE_WIFI } cl_scope_t;

typedef enum {
    CL_OK = 0,
    CL_E_BAD_JSON,
    CL_E_UNKNOWN_OP,
    CL_E_BAD_URL,
    CL_E_TOO_LONG,
    CL_E_BAD_VALUE,
    CL_E_BUSY,
    CL_E_FAILED,
    CL_E_NOT_WORKING,
} cl_err_t;

typedef struct {
    bool has_id;
    long id;
    cl_op_t op;
    char url[CL_URL_MAX + 1];
    char device_id[CL_DEVICE_ID_MAX + 1];
    char token[CL_TOKEN_MAX + 1];
    char name[CL_NAME_MAX + 1];
    cl_scope_t scope;
    int stats_s;
    int live_s;
    int chase_style;
    int chase_fps;
    int chase_laps;
    cl_input_t input;
    int input_n;
    char fault[8];
} cl_req_t;

#define CL_DIAG_STATS_S_MIN 5
#define CL_DIAG_STATS_S_MAX 3600
#define CL_DIAG_LIVE_S_MIN 1
#define CL_DIAG_LIVE_S_MAX 60
#define CL_CHASE_FPS_MIN 10
#define CL_CHASE_FPS_MAX 60
#define CL_CHASE_LAPS_MAX 20

cl_err_t cl_parse(const char *json, cl_req_t *out);

const char *cl_err_name(cl_err_t e);

size_t cl_reply_ok(bool has_id, long id, char *out, size_t cap);
size_t cl_reply_error(bool has_id, long id, cl_err_t e, char *out, size_t cap);

typedef struct {
    const char *fw;
    const char *hw_id;
    const char *device_id;
    bool wifi_configured;
    bool ember_configured;
} cl_info_t;
size_t cl_reply_info(long id, const cl_info_t *info, char *out, size_t cap);

typedef struct {
    const char *wifi_state;
    const char *ssid;
    const char *ip;
    bool has_rssi;
    int rssi;
    const char *ember_state;
    long last_checkin_s;
    long config_version;
    uint32_t internal_free, internal_largest, psram_free;
    bool diag_override;
    int diag_stats_s, diag_live_s;
} cl_status_t;
size_t cl_reply_status(long id, const cl_status_t *st, char *out, size_t cap);

typedef struct {
    const char *diagnostics;
    int checkin_s;
    int stats_s, live_s;
    bool override;
    uint32_t requests, rx_bytes, tx_bytes;
    int64_t uptime_ms;
} cl_diag_t;
size_t cl_reply_diag(long id, const cl_diag_t *d, char *out, size_t cap);

size_t cl_event_boot(const char *fw, bool provisioned, char *out, size_t cap);
size_t cl_event_ember(const char *state, char *out, size_t cap);
size_t cl_event_wifi(const char *state, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
