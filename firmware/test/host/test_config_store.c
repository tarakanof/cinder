#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef enum { T_NONE, T_STR, T_BLOB, T_U8, T_U32 } ntype_t;
typedef struct {
    char key[24];
    ntype_t type;
    unsigned char data[CFG_SETTINGS_MAX + 8];
    size_t len;
} entry_t;

#define MAX_ENTRIES 24
static entry_t E[MAX_ENTRIES];
static bool ns_exists;
static int writes, commits;

const char *esp_err_to_name(esp_err_t err) { return err == ESP_ERR_INVALID_STATE ? "ESP_ERR_INVALID_STATE" : err == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)level;
    (void)tag;
    (void)fmt;
}

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }

static entry_t *find(const char *key)
{
    for (int i = 0; i < MAX_ENTRIES; i++)
        if (E[i].type != T_NONE && strcmp(E[i].key, key) == 0) return &E[i];
    return NULL;
}

static esp_err_t put(const char *key, ntype_t type, const void *v, size_t len)
{
    entry_t *e = find(key);
    for (int i = 0; !e && i < MAX_ENTRIES; i++)
        if (E[i].type == T_NONE) e = &E[i];
    if (!e || len > sizeof e->data) return ESP_FAIL;
    snprintf(e->key, sizeof e->key, "%s", key);
    e->type = type;
    memcpy(e->data, v, len);
    e->len = len;
    writes++;
    return ESP_OK;
}

static esp_err_t get(const char *key, ntype_t type, void *out, size_t *len)
{
    entry_t *e = find(key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->type != type) return ESP_FAIL;
    if (out) {
        if (*len < e->len) return ESP_ERR_NVS_INVALID_LENGTH;
        memcpy(out, e->data, e->len);
    }
    *len = e->len;
    return ESP_OK;
}

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    if (strcmp(ns, CFG_NAMESPACE) != 0) return ESP_FAIL;
    if (mode == NVS_READONLY && !ns_exists) return ESP_ERR_NVS_NOT_FOUND;
    if (mode == NVS_READWRITE) ns_exists = true;
    *h = mode;
    return ESP_OK;
}

void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    commits++;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    (void)h;
    entry_t *e = find(key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    e->type = T_NONE;
    writes++;
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t h)
{
    (void)h;
    for (int i = 0; i < MAX_ENTRIES; i++) E[i].type = T_NONE;
    writes++;
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len)
{
    (void)h;
    return get(key, T_STR, out, len);
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *v)
{
    (void)h;
    return put(key, T_STR, v, strlen(v) + 1);
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    (void)h;
    return get(key, T_BLOB, out, len);
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t len)
{
    (void)h;
    return put(key, T_BLOB, v, len);
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    (void)h;
    size_t n = 1;
    return get(key, T_U8, out, &n);
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v)
{
    (void)h;
    return put(key, T_U8, &v, 1);
}
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *out)
{
    (void)h;
    size_t n = sizeof *out;
    return get(key, T_U32, out, &n);
}
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t v)
{
    (void)h;
    return put(key, T_U32, &v, sizeof v);
}

static const char *stored_str(const char *key)
{
    entry_t *e = find(key);
    return e && e->type == T_STR ? (const char *)e->data : NULL;
}

static int entries(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ENTRIES; i++) n += E[i].type != T_NONE;
    return n;
}

static esp_err_t edit_name(cfg_t *c, void *ctx) { return cfg_set(c, "name", ctx) == CFG_OK ? ESP_OK : ESP_ERR_INVALID_ARG; }
static esp_err_t edit_refuse(cfg_t *c, void *ctx)
{
    (void)ctx;
    cfg_set(c, "name", "never-saved");
    return ESP_FAIL;
}

static const char *SETTINGS = "{\"poll_ms\":3000}";

static void test_edit(void)
{
    cfg_t c;
    config_store_get(&c);
    CHECK(!config_store_provisioned() && c.name[0] == 0, "starts empty");
    CHECK(config_store_update(edit_name, "Desk") == ESP_OK, "an edit saves");
    config_store_get(&c);
    CHECK(strcmp(c.name, "Desk") == 0, "the edit is published (%s)", c.name);
    CHECK(stored_str("name") && strcmp(stored_str("name"), "Desk") == 0, "the edit reaches NVS");
    int w = writes;
    CHECK(config_store_update(edit_refuse, NULL) == ESP_FAIL, "a failing edit returns its error");
    config_store_get(&c);
    CHECK(strcmp(c.name, "Desk") == 0 && writes == w, "a failing edit saves and publishes nothing");

    CHECK(config_store_set_token("ekd_TEST") == ESP_OK, "set_token");
    CHECK(config_store_apply_settings(SETTINGS, 7) == ESP_OK, "apply_settings");
    CHECK(config_store_poll_ms() == 3000 && config_store_settings_version() == 7, "the settings are published (%d, %u)",
          config_store_poll_ms(), (unsigned)config_store_settings_version());
    CHECK(find(CFG_KEY_SETTINGS) && find(CFG_KEY_SETTINGS_VER), "the settings blob and version reach NVS");
    CHECK(config_store_apply_settings("not json", 8) == ESP_ERR_INVALID_ARG && config_store_settings_version() == 7,
          "a bad settings blob is refused and keeps the old one");
    config_store_set_wifi_channel(6);
    CHECK(config_store_wifi_channel() == 6, "the Wi-Fi channel is saved");
    config_store_set_wifi_channel(99);
    CHECK(config_store_wifi_channel() == 6, "a bad channel is ignored");
    CHECK(config_store_clear_settings() == ESP_OK && !find(CFG_KEY_SETTINGS) && !find(CFG_KEY_SETTINGS_VER), "clear_settings drops the blob");
    CHECK(config_store_apply_settings(SETTINGS, 9) == ESP_OK, "settings applied again");
    config_store_get(&c);
    CHECK(strcmp(c.name, "Desk") == 0 && strcmp(c.dev_tok, "ekd_TEST") == 0, "settings writes keep the config fields");
}

static void test_erase(void)
{
    CHECK(config_store_erase() == ESP_OK, "erase");
    cfg_t c;
    config_store_get(&c);
    CHECK(!c.name[0] && !c.dev_tok[0] && !c.dev_id[0] && !c.wifi_ssid[0] && !c.wifi_pass[0] && !c.ember_url[0],
          "the erase publishes an empty config");
    CHECK(!config_store_provisioned() && !config_store_has_device() && !config_store_has_ember_url(), "nothing is provisioned or paired after the erase");
    char tok[CFG_TOKEN_MAX + 1] = "x";
    config_store_token(tok, sizeof tok);
    CHECK(tok[0] == 0, "no token is handed out after the erase");
    CHECK(entries() == 0, "NVS is empty after the erase (%d entries)", entries());

    int w = writes, cm = commits;
    CHECK(config_store_update(edit_name, "After") == ESP_ERR_INVALID_STATE, "update is refused after the erase");
    CHECK(config_store_set_token("ekd_AFTER") == ESP_ERR_INVALID_STATE, "set_token is refused after the erase");
    CHECK(config_store_pair(config_store_token_gen(), "", "knob-2", "ekd_AFTER", "n") == ESP_ERR_INVALID_STATE, "pair is refused after the erase");
    CHECK(config_store_apply_settings(SETTINGS, 10) == ESP_ERR_INVALID_STATE, "apply_settings is refused after the erase");
    CHECK(config_store_settings_version() != 10, "the refused settings are not published (v%u)", (unsigned)config_store_settings_version());
    config_store_set_wifi_channel(11);
    CHECK(config_store_wifi_channel() == 0, "set_wifi_channel writes nothing after the erase (%d)", config_store_wifi_channel());
    CHECK(config_store_clear_settings() == ESP_ERR_INVALID_STATE, "clear_settings is refused after the erase");
    CHECK(writes == w && commits == cm && entries() == 0, "no NVS write or commit after the erase (%d writes, %d commits, %d entries)",
          writes - w, commits - cm, entries());
    config_store_get(&c);
    CHECK(!c.name[0] && !c.dev_tok[0], "the config stays empty");
}

int main(void)
{
    config_store_init();
    test_edit();
    test_erase();
    if (failures) {
        printf("config_store: %d failure(s)\n", failures);
        return 1;
    }
    printf("config_store: ok\n");
    return 0;
}
