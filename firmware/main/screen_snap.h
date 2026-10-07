/* Snapshot is ~440 KB of PSRAM, held until screen_snap_release(); screen objects only (not lv_layer_top). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { SNAP_OK, SNAP_BUSY, SNAP_FAILED } snap_result_t;

/* LVGL task. */
void screen_snap_frame(void);
/* Any other task; on SNAP_OK the pixels stay valid until screen_snap_release(). */
snap_result_t screen_snap_take(uint32_t timeout_ms, const uint8_t **px, int *w, int *h, int *stride);
void screen_snap_release(void);
