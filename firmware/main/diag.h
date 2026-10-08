/* The ember task collects at each checkin; no logging here, the frame note runs in the LVGL task. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_api.h"
#include "esp_err.h"
#include "esp_partition.h"
#include "knob_settings.h"

/* Once at boot, after NVS init: reset reason, boot count, core dump. */
void diag_boot(void);
/* Samples the minimum largest internal block; any task. */
void diag_track(void);
/* Ember task. */
void diag_fill(dev_diag_t *d);
/* Ember task: the heap and stack fields of the OTA health gate. */
void diag_health(ota_health_in_t *h);
/* Ember task. id: the IDF CRC-32 stored in the dump's last 4 bytes; size in bytes. */
bool diag_crash_id(uint32_t *id, uint32_t *size);
/* Ember task. The whole dump, read-only flash mapping; release with esp_partition_munmap. */
const uint8_t *diag_crash_map(esp_partition_mmap_handle_t *h);
/* Ember task. Erases the dump partition and the NVS crash record; no crash in later checkins. */
esp_err_t diag_crash_erase(void);

/* Ember task. */
void diag_set_level(ks_diag_t level);

/* Microseconds; LVGL task. */
void diag_note_frame(int64_t us);
/* ms; any task. */
void diag_note_request(bool ok, uint32_t ms);

bool diag_due(int stats_interval_ms, int period_ms, bool live);

bool diag_sample(dev_stats_t *st);
void diag_commit(void);
