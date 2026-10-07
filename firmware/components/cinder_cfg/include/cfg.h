#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_NAMESPACE "cinder"

#define CFG_SSID_MAX 32
#define CFG_PASS_MAX 64
#define CFG_URL_MAX 128
#define CFG_DEV_ID_MAX 32
#define CFG_TOKEN_MAX 128
#define CFG_NAME_MAX 32
#define CFG_SETTINGS_MAX 1024

#define CFG_KEY_SETTINGS "settings"
#define CFG_KEY_SETTINGS_VER "settings_ver"

typedef struct {
    char wifi_ssid[CFG_SSID_MAX + 1];
    char wifi_pass[CFG_PASS_MAX + 1];
    char ember_url[CFG_URL_MAX + 1];
    char dev_id[CFG_DEV_ID_MAX + 1];
    char dev_tok[CFG_TOKEN_MAX + 1];
    char name[CFG_NAME_MAX + 1];
} cfg_t;

typedef enum {
    CFG_OK = 0,
    CFG_ERR_UNKNOWN_KEY,
    CFG_ERR_TOO_LONG,
    CFG_ERR_BAD_VALUE,
} cfg_err_t;

typedef struct {
    const char *key;
    size_t offset;
    size_t size;
    bool secret;
} cfg_field_t;

extern const cfg_field_t CFG_FIELDS[];
extern const size_t CFG_FIELD_COUNT;

char *cfg_field_ptr(cfg_t *cfg, const cfg_field_t *f);
const char *cfg_field_cptr(const cfg_t *cfg, const cfg_field_t *f);

cfg_err_t cfg_set(cfg_t *cfg, const char *key, const char *value);

bool cfg_url_valid(const char *url);
void cfg_url_normalize(char *url);

bool cfg_provisioned(const cfg_t *cfg);

bool cfg_seed(cfg_t *out, const char *ssid, const char *pass, const char *url, char *rejected, size_t rejected_cap);

/* A token counts only with a device id: a token without one is the legacy master token. */
bool cfg_paired(const cfg_t *cfg);

typedef enum { CFG_READ_OK, CFG_READ_NOT_FOUND, CFG_READ_ERROR } cfg_read_t;
/* Only a definite "no dev_id key" drops the token; any other read error keeps it. */
bool cfg_drop_token(bool has_token, cfg_read_t dev_id);

typedef enum {
    CFG_NVS_EMPTY,
    CFG_NVS_HAS_KEYS,
    CFG_NVS_ERROR,
} cfg_nvs_state_t;

bool cfg_should_seed(bool seed_enabled, cfg_nvs_state_t st, const cfg_t *stored, const char *seed_ssid);

void cfg_copy_raw(uint8_t *dst, size_t cap, const char *src);

void cfg_hw_id(const uint8_t mac[6], char out[13]);
void cfg_short_id(const uint8_t mac[6], char out[7]);

/* Secrets (Wi-Fi password, device token) are never logged. */
void cfg_describe(const cfg_t *cfg, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
