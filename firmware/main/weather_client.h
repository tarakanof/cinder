#pragma once

#include <stdbool.h>

#include "weather_face.h"

/* Call once at boot. */
void weather_client_init(void);

/* Ember task. */
void weather_client_legacy(bool on);

/* Ember task. server_now: the server's Unix time now, 0 unknown. */
void weather_client_feed(const wx_obs_t *obs, double server_now);

/* Any task. */
bool weather_client_get(wx_obs_t *out);
