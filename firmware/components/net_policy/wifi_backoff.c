#include "wifi_backoff.h"

static uint32_t jitter(uint32_t ms, uint32_t rnd)
{
    uint64_t span = ms / 2;
    return ms - ms / 4 + (uint32_t)(span ? ((uint64_t)rnd * span) >> 32 : 0);
}

uint32_t wifi_backoff_ms(unsigned attempt, uint32_t rnd)
{
    uint32_t ms = WIFI_BACKOFF_FIRST_MS;
    for (unsigned i = 0; i < attempt && ms < WIFI_BACKOFF_MAX_MS; i++) ms *= 2;
    if (ms < WIFI_BACKOFF_MAX_MS) {
        ms = jitter(ms, rnd);
        return ms < WIFI_BACKOFF_MAX_MS ? ms : WIFI_BACKOFF_MAX_MS;
    }
    uint32_t span = WIFI_BACKOFF_MAX_MS / 4;
    return WIFI_BACKOFF_MAX_MS - span + (uint32_t)(((uint64_t)rnd * span) >> 32);
}

wifi_disc_t wifi_disc_class(int r)
{
    switch (r) {
    case 8:
        return WIFI_DISC_LEAVE;
    case 201:
        return WIFI_DISC_NO_AP;
    case 2: case 15: case 202: case 204: case 210: case 211:
        return WIFI_DISC_AUTH;
    case 4: case 5: case 17: case 203: case 205:
        return WIFI_DISC_ASSOC;
    case 6: case 7: case 23: case 34: case 200:
        return WIFI_DISC_LINK_LOST;
    default:
        return WIFI_DISC_OTHER;
    }
}

const char *wifi_disc_name(wifi_disc_t c)
{
    static const char *const N[] = {"other", "leave", "no_ap", "auth", "assoc", "link_lost"};
    return (unsigned)c < sizeof N / sizeof N[0] ? N[c] : "other";
}

uint32_t wifi_reconnect_ms(unsigned attempt, int reason, uint32_t rnd)
{
    wifi_disc_t c = wifi_disc_class(reason);
    if (attempt == 0 && (c == WIFI_DISC_ASSOC || c == WIFI_DISC_LINK_LOST || c == WIFI_DISC_LEAVE || c == WIFI_DISC_NO_AP))
        return jitter(WIFI_RETRY_FAST_MS, rnd);
    return wifi_backoff_ms(attempt, rnd);
}

bool wifi_scan_all_channels(int hint_channel, unsigned attempt) { return hint_channel <= 0 || attempt >= 2; }
