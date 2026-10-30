#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sens_ens160.h"

#define ROOM_HAS_CLIMATE (1u << 0)
#define ROOM_HAS_AIR (1u << 1)
#define ROOM_HAS_LIGHT (1u << 2)
#define ROOM_HAS_PRESENCE (1u << 3)
#define ROOM_HAS_PRESSURE (1u << 4)

/* One snapshot of the room sensors. *_ms fields are esp_timer milliseconds of the last good read (0 = never). */
typedef struct {
    uint32_t has;
    float temp_c;
    float rh_pct;
    int64_t climate_ms;
    uint8_t aqi;
    uint16_t tvoc_ppb;
    uint16_t eco2_ppm;
    ens160_validity_t air_validity;
    int64_t air_ms;
    float lux;
    int64_t light_ms;
    bool present;
    bool present_raw;
    int64_t presence_ms;
    int64_t presence_changed_ms;
    uint32_t presence_flips;
    float press_hpa;
    int64_t pressure_ms;
    uint32_t errors;
} room_t;

typedef struct {
    uint32_t on_ms;
    uint32_t off_ms;
    bool present;
    bool raw;
    int64_t raw_since_ms;
} room_presence_t;

/* Debounce for the radar OUT level: present after raw high for on_ms, absent after raw low for off_ms. */
void room_presence_init(room_presence_t *p, uint32_t on_ms, uint32_t off_ms, int64_t now_ms);
/* Returns true when the debounced state changed. */
bool room_presence_feed(room_presence_t *p, bool raw, int64_t now_ms);

typedef struct {
    float temp_c;
    float rh_pct;
    int64_t at_ms;
    bool sent;
} room_comp_t;

#define ROOM_COMP_TEMP_STEP 0.5f
#define ROOM_COMP_RH_STEP 2.0f
#define ROOM_COMP_MAX_AGE_MS 600000

/* Whether the ENS160 TEMP_IN / RH_IN should be rewritten for this climate reading. */
bool room_comp_due(const room_comp_t *c, float temp_c, float rh_pct, int64_t now_ms);
void room_comp_sent(room_comp_t *c, float temp_c, float rh_pct, int64_t now_ms);
