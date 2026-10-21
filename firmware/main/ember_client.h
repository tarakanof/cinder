#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bot_behavior.h"
#include "demo.h"
#include "dim.h"
#include "device_api.h"
#include "ember_host.h"
#include "link_state.h"

/* Call before the display init (internal RAM); not provisioned: scan-only radio. */
void ember_client_start(void);
/* Any task. */
void ember_client_wake(void);

void ember_client_forget_wifi(void);

bool ember_client_mood(bot_mood_t *out);

bool ember_client_quiet(void);

/* Any task. A knob or touch input: true when the panel was off, so the input only wakes it and must be dropped (docs/features.md, idle dim and off). */
bool ember_client_idle_input(dim_wake_t src);
/* Any task. A wake source that is not input: the mood turning to waiting or error, the OTA face, a demo. */
void ember_client_idle_wake(dim_wake_t src);
/* Any task. The idle rules have the panel off: the LVGL task skips rendering. */
bool ember_client_idle_off(void);

bool ember_client_online(void);

/* Any task. link_state word of Ember's link (level and OFFLINE generation), for stamping presses. */
uint32_t ember_client_link_word(void);
/* Any task. OFFLINE with an Ember URL configured; false when Ember is not set up. */
bool ember_client_offline(void);

typedef enum {
    EMBER_LINK_OFF,
    EMBER_LINK_CONNECTING,
    EMBER_LINK_OK,
    EMBER_LINK_UNREACHABLE,
    EMBER_LINK_UNAUTHORIZED,
} ember_link_t;
ember_link_t ember_client_link(void);

typedef enum { EMBER_VIEW_OK, EMBER_VIEW_UPDATE_EMBER, EMBER_VIEW_UPDATE_KNOB } ember_view_compat_t;
/* Any task. The last view's major was below (update Ember) or above (update the knob) the range this firmware parses. */
ember_view_compat_t ember_client_view_compat(void);
void ember_client_report_unauthorized(void);
/* Seconds since the last successful checkin; -1 none. */
long ember_client_last_checkin_s(void);
/* Overrides in seconds, RAM only; -1 leaves one as is, 0 reverts to the knob settings. */
void ember_client_diag_override(int stats_s, int live_s);
void ember_client_diag_intervals(int *stats_s, int *live_s, bool *override, int *diagnostics, int *checkin_s);
const char *ember_link_name(ember_link_t l);

void ember_client_ip(char *out, size_t cap);
bool ember_client_rssi(int *out);
void ember_client_wifi(dev_wifi_t *out);
unsigned ember_client_beacon_timeouts(void);
bool ember_client_wifi_started(void);
int ember_client_join_failures(void);

/* Any task. */
void ember_client_host(ember_host_info_t *out);

/* gen moves on every demo start and end; view_on (quiet: its quiet) lags the end until the real view is back. */
typedef struct {
    bool playing;
    unsigned gen;
    bool view_on;
    bool quiet;
} ember_demo_status_t;

/* Any task. */
void ember_client_demo_status(ember_demo_status_t *out);
/* LVGL task: the scene to start and its gen, once per demo, after its view is applied. */
bool ember_client_demo_take(demo_block_t *out, unsigned *gen);
/* LVGL task, every frame: ends a demo past its time (true when it did). */
bool ember_client_demo_tick(double t);
/* LVGL task: true when a demo was playing; it ends and the input is consumed. */
bool ember_client_demo_input(void);
/* LVGL task, every frame: the page on screen (a PAGES id) and on weather the resolved face; a change is reported after SHOWN_DEBOUNCE_MS. */
void ember_client_shown(const char *page, int face);
/* LVGL task: the act of the scene with this gen has played. */
void ember_client_demo_act_done(unsigned gen);

/* level on the 0-255 scale; call once after the BSP init. */
void ember_client_dim_enable(uint8_t current_level);
