#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_api.h"
#include "sens_room.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROOM_FRESH_MS 60000

/* A reading taken at at_ms (0: never) is under ROOM_FRESH_MS old at now_ms. */
bool room_fresh(int64_t at_ms, int64_t now_ms);
/* The checkin room block from a j2 snapshot at now_ms (the snapshot's esp_timer ms): false, and no block, when no chip was found. A source is in only while its reading is under ROOM_FRESH_MS old. */
bool room_report(const room_t *r, int64_t now_ms, dev_room_t *out);

#ifdef __cplusplus
}
#endif
