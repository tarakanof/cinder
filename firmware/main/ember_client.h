#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bot_behavior.h"
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

/* level on the 0-255 scale; call once after the BSP init. */
void ember_client_dim_enable(uint8_t current_level);
