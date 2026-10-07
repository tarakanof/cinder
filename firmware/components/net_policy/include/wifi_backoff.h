#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_BACKOFF_FIRST_MS 1000
#define WIFI_BACKOFF_MAX_MS 30000
#define WIFI_RETRY_FAST_MS 250

/* Disconnect reasons use the esp_wifi_types.h numbering. */
typedef enum {
    WIFI_DISC_OTHER = 0,
    WIFI_DISC_LEAVE,
    WIFI_DISC_NO_AP,
    WIFI_DISC_AUTH,
    WIFI_DISC_ASSOC,
    WIFI_DISC_LINK_LOST,
} wifi_disc_t;

wifi_disc_t wifi_disc_class(int reason);
const char *wifi_disc_name(wifi_disc_t c);

uint32_t wifi_backoff_ms(unsigned attempt, uint32_t rnd);

uint32_t wifi_reconnect_ms(unsigned attempt, int reason, uint32_t rnd);

bool wifi_scan_all_channels(int hint_channel, unsigned attempt);

#ifdef __cplusplus
}
#endif
