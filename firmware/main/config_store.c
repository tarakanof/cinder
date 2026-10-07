#include "config_store.h"

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";

static cfg_t *s_cfg;
static knob_settings_t *s_ks;
static uint32_t s_ks_ver;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool s_has_device, s_has_url;
static atomic_uint s_token_gen, s_ks_gen;
static atomic_int s_poll_ms = 2000;

static void publish(const cfg_t *c)
{
    taskENTER_CRITICAL(&s_lock);
    bool tok_changed = strcmp(s_cfg->dev_tok, c->dev_tok) != 0;
    *s_cfg = *c;
    taskEXIT_CRITICAL(&s_lock);
    atomic_store(&s_has_device, cfg_paired(c));
    atomic_store(&s_has_url, c->ember_url[0] != 0);
    if (tok_changed) atomic_fetch_add(&s_token_gen, 1);
}

static void publish_settings(const knob_settings_t *ks, uint32_t ver)
{
    taskENTER_CRITICAL(&s_lock);
    *s_ks = *ks;
    s_ks_ver = ver;
    taskEXIT_CRITICAL(&s_lock);
    atomic_store(&s_poll_ms, ks->poll_ms);
    atomic_fetch_add(&s_ks_gen, 1);
}

#ifdef CONFIG_CINDER_DEV_SEED
static const char DEV_SEED_MARKER[] __attribute__((used)) = "CINDER-DEV-SEED-BUILD";

/* Only ESP_ERR_NVS_NOT_FOUND means empty: any other error must not lead to a seed that overwrites settings. */
static cfg_nvs_state_t namespace_state(void)
{
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, CFG_NAMESPACE, NVS_TYPE_ANY, &it);
    nvs_release_iterator(it);
    if (err == ESP_OK) return CFG_NVS_HAS_KEYS;
    if (err == ESP_ERR_NVS_NOT_FOUND) return CFG_NVS_EMPTY;
    ESP_LOGE(TAG, "reading namespace %s failed: %s (no seed)", CFG_NAMESPACE, esp_err_to_name(err));
    return CFG_NVS_ERROR;
}
#endif

static void load(cfg_t *c)
{
    memset(c, 0, sizeof *c);
    nvs_handle_t h;
    if (nvs_open(CFG_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    for (size_t i = 0; i < CFG_FIELD_COUNT; i++) {
        const cfg_field_t *f = &CFG_FIELDS[i];
        size_t len = f->size;
        char *dst = cfg_field_ptr(c, f);
        esp_err_t err = nvs_get_str(h, f->key, dst, &len);
        if (err != ESP_OK) {
            dst[0] = 0;
            if (err != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG, "%s: %s (ignored)", f->key, esp_err_to_name(err));
            continue;
        }
        char tmp[CFG_TOKEN_MAX + 1];
        strlcpy(tmp, dst, sizeof tmp);
        dst[0] = 0;
        if (cfg_set(c, f->key, tmp) != CFG_OK) ESP_LOGW(TAG, "%s: invalid value in NVS (ignored)", f->key);
        memset(tmp, 0, sizeof tmp);
    }
    nvs_close(h);
}

#define KEY_WIFI_CH "wifi_ch"

static cfg_read_t dev_id_read(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return CFG_READ_NOT_FOUND;
    if (err != ESP_OK) return CFG_READ_ERROR;
    size_t len = 0;
    err = nvs_get_str(h, "dev_id", NULL, &len);
    nvs_close(h);
    return err == ESP_OK ? CFG_READ_OK : err == ESP_ERR_NVS_NOT_FOUND ? CFG_READ_NOT_FOUND : CFG_READ_ERROR;
}

static esp_err_t erase_token(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, "dev_tok");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t config_store_save(const cfg_t *cfg)
{
    taskENTER_CRITICAL(&s_lock);
    bool new_ssid = strcmp(s_cfg->wifi_ssid, cfg->wifi_ssid) != 0;
    taskEXIT_CRITICAL(&s_lock);
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (new_ssid) {
        err = nvs_erase_key(h, KEY_WIFI_CH);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    for (size_t i = 0; i < CFG_FIELD_COUNT && err == ESP_OK; i++) {
        const cfg_field_t *f = &CFG_FIELDS[i];
        const char *v = cfg_field_cptr(cfg, f);
        if (v[0]) {
            err = nvs_set_str(h, f->key, v);
        } else {
            err = nvs_erase_key(h, f->key);
            if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        }
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) publish(cfg);
    return err;
}

static esp_err_t get_settings_blob(char *buf, size_t cap, uint32_t *ver);
static esp_err_t set_settings_blob(const char *json, uint32_t ver);

static void load_settings(void)
{
    char *json = heap_caps_malloc(CFG_SETTINGS_MAX + 1, MALLOC_CAP_SPIRAM);
    knob_settings_t *ks = heap_caps_malloc(sizeof *ks, MALLOC_CAP_SPIRAM);
    assert(json && ks);
    uint32_t ver = 0;
    esp_err_t err = get_settings_blob(json, CFG_SETTINGS_MAX + 1, &ver);
    if (err != ESP_OK || !knob_settings_parse(json, ks)) {
        if (err != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG, "knob settings: %s; defaults", esp_err_to_name(err));
        knob_settings_defaults(ks);
        int poll = CONFIG_CINDER_EMBER_POLL_MS;
        ks->poll_ms = poll < 1000 ? 1000 : poll > 10000 ? 10000 : poll;
        ver = 0;
    } else {
        ESP_LOGI(TAG, "knob settings v%" PRIu32 " from NVS", ver);
    }
    publish_settings(ks, ver);
    free(json);
    free(ks);
}

void config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition unusable (%s): erasing it", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_cfg = heap_caps_calloc(1, sizeof *s_cfg, MALLOC_CAP_SPIRAM);
    s_ks = heap_caps_calloc(1, sizeof *s_ks, MALLOC_CAP_SPIRAM);
    cfg_t *cp = heap_caps_calloc(1, sizeof *cp, MALLOC_CAP_SPIRAM);
    assert(s_cfg && s_ks && cp);
    cfg_t *c = cp;
#ifdef CONFIG_CINDER_DEV_SEED
    (void)*(volatile const char *)DEV_SEED_MARKER;
    ESP_LOGW(TAG, "dev seed build: Wi-Fi and Ember URL compiled in");
    cfg_nvs_state_t st = namespace_state();
    load(c);
    if (cfg_should_seed(true, st, c, CONFIG_CINDER_WIFI_SSID)) {
        char rejected[64];
        if (!cfg_seed(c, CONFIG_CINDER_WIFI_SSID, CONFIG_CINDER_WIFI_PASSWORD, CONFIG_CINDER_EMBER_URL, rejected,
                      sizeof rejected)) {
            ESP_LOGE(TAG, "dev seed: invalid %s in the build config; not seeded (setup face)", rejected);
        } else {
            if (rejected[0]) ESP_LOGE(TAG, "dev seed: invalid %s in the build config; left empty", rejected);
            err = config_store_save(c);
            char line[200];
            cfg_describe(c, line, sizeof line);
            if (err == ESP_OK)
                ESP_LOGI(TAG, "NVS %s: seeded from the build config (%s)",
                         st == CFG_NVS_EMPTY ? "empty" : "without Wi-Fi SSID", line);
            else ESP_LOGE(TAG, "seeding NVS failed: %s", esp_err_to_name(err));
        }
    }
#endif
    load(c);
    publish(c);
    if (cfg_drop_token(c->dev_tok[0] != 0, dev_id_read())) {
        err = erase_token();
        if (err == ESP_OK) {
            memset(c->dev_tok, 0, sizeof c->dev_tok);
            publish(c);
            ESP_LOGW(TAG, "token without a device id removed from NVS: not paired");
        } else {
            ESP_LOGE(TAG, "removing the token without a device id failed: %s", esp_err_to_name(err));
        }
    }
    char line[200];
    cfg_describe(c, line, sizeof line);
    ESP_LOGI(TAG, "%s: %s", cfg_provisioned(c) ? "provisioned" : "not provisioned", line);
    memset(c, 0, sizeof *c);
    free(cp);
    load_settings();
}

void config_store_get(cfg_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = *s_cfg;
    taskEXIT_CRITICAL(&s_lock);
}

bool config_store_provisioned(void)
{
    taskENTER_CRITICAL(&s_lock);
    bool p = cfg_provisioned(s_cfg);
    taskEXIT_CRITICAL(&s_lock);
    return p;
}

bool config_store_has_device(void) { return atomic_load(&s_has_device); }
bool config_store_has_ember_url(void) { return atomic_load(&s_has_url); }
uint32_t config_store_token_gen(void) { return atomic_load(&s_token_gen); }

esp_err_t config_store_set_token(const char *token)
{
    cfg_t *c = heap_caps_malloc(sizeof *c, MALLOC_CAP_SPIRAM);
    if (!c) return ESP_ERR_NO_MEM;
    config_store_get(c);
    esp_err_t err = cfg_set(c, "dev_tok", token) == CFG_OK ? config_store_save(c) : ESP_ERR_INVALID_ARG;
    memset(c, 0, sizeof *c);
    free(c);
    return err;
}

void config_store_settings(knob_settings_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = *s_ks;
    taskEXIT_CRITICAL(&s_lock);
}

uint32_t config_store_settings_gen(void) { return atomic_load(&s_ks_gen); }
int config_store_poll_ms(void) { return atomic_load(&s_poll_ms); }

uint32_t config_store_settings_version(void)
{
    taskENTER_CRITICAL(&s_lock);
    uint32_t v = s_ks_ver;
    taskEXIT_CRITICAL(&s_lock);
    return v;
}

esp_err_t config_store_apply_settings(const char *json, uint32_t ver)
{
    knob_settings_t *ks = heap_caps_malloc(sizeof *ks, MALLOC_CAP_SPIRAM);
    if (!ks) return ESP_ERR_NO_MEM;
    esp_err_t err = knob_settings_parse(json, ks) ? set_settings_blob(json, ver) : ESP_ERR_INVALID_ARG;
    if (err == ESP_OK) publish_settings(ks, ver);
    free(ks);
    return err;
}

void config_store_ember_url(char *out, size_t cap)
{
    taskENTER_CRITICAL(&s_lock);
    strlcpy(out, s_cfg->ember_url, cap);
    taskEXIT_CRITICAL(&s_lock);
}

void config_store_ssid(char *out, size_t cap)
{
    taskENTER_CRITICAL(&s_lock);
    strlcpy(out, s_cfg->wifi_ssid, cap);
    taskEXIT_CRITICAL(&s_lock);
}

void config_store_token(char *out, size_t cap)
{
    taskENTER_CRITICAL(&s_lock);
    strlcpy(out, cfg_paired(s_cfg) ? s_cfg->dev_tok : "", cap);
    taskEXIT_CRITICAL(&s_lock);
}

static void base_mac(uint8_t mac[6])
{
    if (esp_efuse_mac_get_default(mac) != ESP_OK) memset(mac, 0, 6);
}

void config_store_hw_id(char out[13])
{
    uint8_t mac[6];
    base_mac(mac);
    cfg_hw_id(mac, out);
}

void config_store_short_id(char out[7])
{
    uint8_t mac[6];
    base_mac(mac);
    cfg_short_id(mac, out);
}

static esp_err_t get_settings_blob(char *buf, size_t cap, uint32_t *ver)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len = cap;
    err = nvs_get_blob(h, CFG_KEY_SETTINGS, buf, &len);
    if (err == ESP_OK) {
        if (len >= cap) err = ESP_ERR_NVS_INVALID_LENGTH;
        else buf[len] = 0;
    }
    if (err == ESP_OK && ver && nvs_get_u32(h, CFG_KEY_SETTINGS_VER, ver) != ESP_OK) *ver = 0;
    nvs_close(h);
    return err;
}

static esp_err_t set_settings_blob(const char *json, uint32_t ver)
{
    size_t len = strlen(json);
    if (len > CFG_SETTINGS_MAX) return ESP_ERR_INVALID_SIZE;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, CFG_KEY_SETTINGS, json, len);
    if (err == ESP_OK) err = nvs_set_u32(h, CFG_KEY_SETTINGS_VER, ver);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

int config_store_wifi_channel(void)
{
    nvs_handle_t h;
    uint8_t ch = 0;
    if (nvs_open(CFG_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;
    if (nvs_get_u8(h, KEY_WIFI_CH, &ch) != ESP_OK) ch = 0;
    nvs_close(h);
    return ch;
}

void config_store_set_wifi_channel(int channel)
{
    if (channel < 1 || channel > 14 || channel == config_store_wifi_channel()) return;
    nvs_handle_t h;
    if (nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, KEY_WIFI_CH, (uint8_t)channel) == ESP_OK && nvs_commit(h) == ESP_OK)
        ESP_LOGI(TAG, "Wi-Fi channel hint -> %d", channel);
    nvs_close(h);
}

esp_err_t config_store_clear_settings(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    const char *keys[] = {CFG_KEY_SETTINGS, CFG_KEY_SETTINGS_VER};
    for (size_t i = 0; i < 2 && (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND); i++) err = nvs_erase_key(h, keys[i]);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t config_store_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
