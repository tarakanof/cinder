#pragma once

#include "app_mod.h"

/* CONFIG_CINDER_ROOM only: the J2 room task, presence into dim and the checkin room block (docs/features.md, room sensors). */
extern const app_mod_t ROOM_MOD;
/* The room air for the bot's room moments (room_air_now on the j2 snapshot), any task; NULL without CONFIG_CINDER_ROOM. */
extern bool (*const ROOM_MOD_AIR)(int *aqi);
