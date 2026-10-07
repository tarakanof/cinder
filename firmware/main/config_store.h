/* Secrets are never logged; readers get copies. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cfg.h"
#include "esp_err.h"
#include "knob_settings.h"

void config_store_init(void);

/* Any task; ~420 bytes: use a static buffer on small stacks. */
void config_store_get(cfg_t *out);

bool config_store_provisioned(void);
bool config_store_has_device(void);
bool config_store_has_ember_url(void);
uint32_t config_store_token_gen(void);
esp_err_t config_store_set_token(const char *token);
void config_store_ember_url(char *out, size_t cap);
void config_store_ssid(char *out, size_t cap);
/* Empty unless paired (device id and token). */
void config_store_token(char *out, size_t cap);

void config_store_hw_id(char out[13]);
void config_store_short_id(char out[7]);

esp_err_t config_store_save(const cfg_t *cfg);

void config_store_settings(knob_settings_t *out);
uint32_t config_store_settings_gen(void);
uint32_t config_store_settings_version(void);
int config_store_poll_ms(void);
esp_err_t config_store_apply_settings(const char *json, uint32_t ver);

/* Setter writes NVS only on change: call from a task, not the event loop. */
int config_store_wifi_channel(void);
void config_store_set_wifi_channel(int channel);

esp_err_t config_store_clear_settings(void);

/* Wi-Fi driver state and the reboot are the caller's. */
esp_err_t config_store_erase(void);
