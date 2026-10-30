#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device_api.h"
#include "dim.h"
#include "knob_settings.h"

/* Owner of the panel brightness: idle clock, fade and level logic, stepped from the LVGL frame (docs/features.md, brightness owner). */

/* level on the 0-255 scale; call once after the BSP init. */
void dim_ctl_enable(uint8_t current_level);
/* LVGL task, every frame, also while the screen is off or frozen. No fade runs before the first dim_ctl_settings. */
void dim_ctl_frame(void);

/* Any task. A knob or touch input: true when the panel was off, so the input only wakes it and must be dropped (docs/features.md, idle dim and off). */
bool dim_ctl_input(dim_wake_t src);
/* Any task. A wake source that is not input: the mood turning active, the OTA face, a demo. */
void dim_ctl_wake(dim_wake_t src);
/* Any task. The idle rules have the panel off: the LVGL task skips rendering. */
bool dim_ctl_off(void);

/* Any task. Copies what the dimmer reads from ks; the caller then re-applies with dim_ctl_apply. */
void dim_ctl_settings(const knob_settings_t *ks);
/* Any task. The requested level, 0-255, from a view, the legacy poll or the manual setting; re-applies. */
void dim_ctl_want(int level);
/* Any task. Quiet hours cap the level from the next dim_ctl_apply. */
void dim_ctl_quiet(bool dim);
/* Any task. mood is the knob's current one, -1 offline or no data; dim_mood_holds decides whether it holds the panel awake. */
void dim_ctl_mood(int mood);
/* Any task. Re-reads the idle clock and applies the level. Logs, so never from an esp_timer callback. */
void dim_ctl_apply(void);

/* Any task. Fills the checkin's diag.idle. */
void dim_ctl_diag(dev_diag_t *d);
