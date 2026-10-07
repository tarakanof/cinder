#include "cfg.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define F(k, m, s) {k, offsetof(cfg_t, m), sizeof(((cfg_t *)0)->m), s}
const cfg_field_t CFG_FIELDS[] = {
    F("wifi_pass", wifi_pass, true),
    F("ember_url", ember_url, false),
    F("dev_id", dev_id, false),
    F("dev_tok", dev_tok, true),
    F("name", name, false),
    F("wifi_ssid", wifi_ssid, false),
};
#undef F
const size_t CFG_FIELD_COUNT = sizeof CFG_FIELDS / sizeof CFG_FIELDS[0];

char *cfg_field_ptr(cfg_t *cfg, const cfg_field_t *f) { return (char *)cfg + f->offset; }
const char *cfg_field_cptr(const cfg_t *cfg, const cfg_field_t *f) { return (const char *)cfg + f->offset; }

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static bool is_alnum(char c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool cfg_url_valid(const char *url)
{
    if (!url) return false;
    size_t len = strlen(url);
    if (len > CFG_URL_MAX) return false;
    if (strncasecmp(url, "http://", 7) != 0) return false;
    const char *p = url + 7;
    const char *host = p;
    while (is_alnum(*p) || *p == '.' || *p == '-') p++;
    if (p == host || *host == '.' || *host == '-') return false;
    if (*p == ':') {
        p++;
        long port = 0;
        const char *d = p;
        while (is_digit(*p) && p - d < 6) port = port * 10 + (*p++ - '0');
        if (p == d || port < 1 || port > 65535) return false;
    }
    if (*p == '/') p++;
    return *p == 0;
}

void cfg_url_normalize(char *url)
{
    size_t n = strlen(url);
    for (char *c = url; *c; c++)
        if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    if (n && url[n - 1] == '/') url[n - 1] = 0;
}

static bool printable(const char *v, bool allow_space)
{
    for (; *v; v++) {
        unsigned char c = (unsigned char)*v;
        if (c < 0x20 || c == 0x7f || (!allow_space && c == ' ')) return false;
    }
    return true;
}

static bool pass_valid(const char *v)
{
    size_t n = strlen(v);
    if (n == 0) return true;
    if (n == 64) {
        for (size_t i = 0; i < n; i++)
            if (!is_hex(v[i])) return false;
        return true;
    }
    if (n < 8 || n > 63) return false;
    for (; *v; v++)
        if ((unsigned char)*v < 0x20 || (unsigned char)*v > 0x7e) return false;
    return true;
}

static bool dev_id_valid(const char *v)
{
    for (; *v; v++)
        if (!is_alnum(*v) && *v != '-' && *v != '_') return false;
    return true;
}

static cfg_err_t check(const char *key, const char *v)
{
    if (strcmp(key, "wifi_ssid") == 0) return CFG_OK;
    if (strcmp(key, "wifi_pass") == 0) return pass_valid(v) ? CFG_OK : CFG_ERR_BAD_VALUE;
    if (strcmp(key, "ember_url") == 0) return cfg_url_valid(v) ? CFG_OK : CFG_ERR_BAD_VALUE;
    if (strcmp(key, "dev_id") == 0) return dev_id_valid(v) ? CFG_OK : CFG_ERR_BAD_VALUE;
    if (strcmp(key, "dev_tok") == 0) return printable(v, false) ? CFG_OK : CFG_ERR_BAD_VALUE;
    if (strcmp(key, "name") == 0) return printable(v, true) ? CFG_OK : CFG_ERR_BAD_VALUE;
    return CFG_ERR_UNKNOWN_KEY;
}

cfg_err_t cfg_set(cfg_t *cfg, const char *key, const char *value)
{
    const cfg_field_t *f = NULL;
    for (size_t i = 0; key && i < CFG_FIELD_COUNT; i++)
        if (strcmp(CFG_FIELDS[i].key, key) == 0) f = &CFG_FIELDS[i];
    if (!f) return CFG_ERR_UNKNOWN_KEY;
    if (!value) value = "";
    size_t n = strlen(value);
    if (n >= f->size) return CFG_ERR_TOO_LONG;
    if (n > 0) {
        cfg_err_t e = check(key, value);
        if (e != CFG_OK) return e;
    }
    char *dst = cfg_field_ptr(cfg, f);
    memcpy(dst, value, n + 1);
    if (strcmp(key, "ember_url") == 0 && n > 0) cfg_url_normalize(dst);
    return CFG_OK;
}

bool cfg_provisioned(const cfg_t *cfg) { return cfg && cfg->wifi_ssid[0] != 0; }

static void reject(char *rejected, size_t cap, const char *key)
{
    if (!rejected || !cap) return;
    if (rejected[0]) strlcat(rejected, ",", cap);
    strlcat(rejected, key, cap);
}

bool cfg_seed(cfg_t *out, const char *ssid, const char *pass, const char *url, char *rejected, size_t rejected_cap)
{
    memset(out, 0, sizeof *out);
    if (rejected && rejected_cap) rejected[0] = 0;
    if (!ssid || !ssid[0]) return false;
    bool ok = true;
    if (cfg_set(out, "wifi_ssid", ssid) != CFG_OK) {
        reject(rejected, rejected_cap, "wifi_ssid");
        ok = false;
    }
    if (cfg_set(out, "wifi_pass", pass) != CFG_OK) {
        reject(rejected, rejected_cap, "wifi_pass");
        ok = false;
    }
    if (!ok) {
        memset(out, 0, sizeof *out);
        return false;
    }
    if (cfg_set(out, "ember_url", url) != CFG_OK) reject(rejected, rejected_cap, "ember_url");
    return true;
}

bool cfg_paired(const cfg_t *cfg) { return cfg && cfg->dev_id[0] != 0 && cfg->dev_tok[0] != 0; }

bool cfg_drop_token(bool has_token, cfg_read_t dev_id) { return has_token && dev_id == CFG_READ_NOT_FOUND; }

bool cfg_should_seed(bool seed_enabled, cfg_nvs_state_t st, const cfg_t *stored, const char *seed_ssid)
{
    if (!seed_enabled || !seed_ssid || !seed_ssid[0]) return false;
    if (st == CFG_NVS_EMPTY) return true;
    if (st == CFG_NVS_HAS_KEYS) return !cfg_provisioned(stored);
    return false;
}

void cfg_copy_raw(uint8_t *dst, size_t cap, const char *src)
{
    size_t n = src ? strnlen(src, cap) : 0;
    if (n) memcpy(dst, src, n);
    if (n < cap) memset(dst + n, 0, cap - n);
}

void cfg_hw_id(const uint8_t mac[6], char out[13])
{
    snprintf(out, 13, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void cfg_short_id(const uint8_t mac[6], char out[7])
{
    snprintf(out, 7, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void cfg_describe(const cfg_t *cfg, char *out, size_t cap)
{
    snprintf(out, cap, "ssid \"%s\", password %s, ember %s, token %s, device %s", cfg->wifi_ssid,
             cfg->wifi_pass[0] ? "set" : "none", cfg->ember_url[0] ? cfg->ember_url : "(none)",
             cfg->dev_tok[0] ? "set" : "none", cfg->dev_id[0] ? cfg->dev_id : "(none)");
}
