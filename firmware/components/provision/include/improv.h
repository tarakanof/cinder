#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMPROV_HEADER "IMPROV"
#define IMPROV_HEADER_LEN 6
#define IMPROV_VERSION 0x01
#define IMPROV_PREFIX_LEN 9
#define IMPROV_FRAME_MAX (IMPROV_PREFIX_LEN + 255 + 1 + 1)

enum { IMPROV_TYPE_STATE = 0x01, IMPROV_TYPE_ERROR = 0x02, IMPROV_TYPE_RPC = 0x03, IMPROV_TYPE_RESULT = 0x04 };
enum { IMPROV_STATE_READY = 0x02, IMPROV_STATE_PROVISIONING = 0x03, IMPROV_STATE_PROVISIONED = 0x04 };
enum {
    IMPROV_ERR_NONE = 0x00,
    IMPROV_ERR_INVALID_RPC = 0x01,
    IMPROV_ERR_UNKNOWN_CMD = 0x02,
    IMPROV_ERR_UNABLE_TO_CONNECT = 0x03,
    IMPROV_ERR_BAD_HOSTNAME = 0x05,
    IMPROV_ERR_UNKNOWN = 0xFF,
};
enum {
    IMPROV_CMD_WIFI = 0x01,
    IMPROV_CMD_STATE = 0x02,
    IMPROV_CMD_INFO = 0x03,
    IMPROV_CMD_SCAN = 0x04,
    IMPROV_CMD_HOSTNAME = 0x05,
    IMPROV_CMD_NAME = 0x06,
};

uint8_t improv_checksum(const uint8_t *b, size_t n);

size_t improv_frame(uint8_t type, const uint8_t *data, size_t len, uint8_t *out, size_t cap);
size_t improv_state(uint8_t state, uint8_t *out, size_t cap);
size_t improv_error(uint8_t err, uint8_t *out, size_t cap);
size_t improv_result(uint8_t cmd, const char *const *strs, size_t n, uint8_t *out, size_t cap);

typedef enum {
    IMPROV_OK = 0,
    IMPROV_E_SHORT,
    IMPROV_E_HEADER,
    IMPROV_E_VERSION,
    IMPROV_E_LENGTH,
    IMPROV_E_CHECKSUM,
    IMPROV_E_STRINGS,
} improv_err_t;

typedef struct {
    uint8_t type;
    const uint8_t *data;
    uint8_t len;
} improv_pkt_t;

improv_err_t improv_parse(const uint8_t *frame, size_t len, improv_pkt_t *out);

#define IMPROV_RPC_MAX_STRINGS 4
typedef struct {
    uint8_t cmd;
    uint8_t n;
    const uint8_t *str[IMPROV_RPC_MAX_STRINGS];
    uint8_t len[IMPROV_RPC_MAX_STRINGS];
} improv_rpc_t;

improv_err_t improv_parse_rpc(const improv_pkt_t *pkt, improv_rpc_t *out);

bool improv_rpc_string(const improv_rpc_t *rpc, int i, char *dst, size_t cap);

#define PROV_LINE_PREFIX "CINDER1 "
#define PROV_LINE_PREFIX_LEN 8

typedef enum {
    PROV_RX_NONE = 0,
    PROV_RX_IMPROV,
    PROV_RX_LINE,
    PROV_RX_LINE_TOO_LONG,
} prov_rx_ev_t;

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    uint8_t mode;
    bool cinder;
} prov_rx_t;

void prov_rx_init(prov_rx_t *rx, uint8_t *buf, size_t cap);
prov_rx_ev_t prov_rx_feed(prov_rx_t *rx, uint8_t byte, const uint8_t **out, size_t *out_len);

void prov_rx_idle(prov_rx_t *rx);

bool improv_join_failure_reason(int reason);

#define IMPROV_WEAK_RSSI (-80)
#define IMPROV_WEAK_PER_FAIL 2
typedef struct {
    int strong, weak;
} improv_join_t;
void improv_join_reset(improv_join_t *j);
void improv_join_note(improv_join_t *j, int reason, int rssi);   /* rssi in dBm, 0 = unknown */
int improv_join_failures(const improv_join_t *j);

#ifdef __cplusplus
}
#endif
