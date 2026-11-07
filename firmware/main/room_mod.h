#pragma once

#include <stdint.h>

#include "app_mod.h"
#include "sens_room.h"

/* CONFIG_CINDER_ROOM only: the J2 room task, presence into dim and the checkin room block (docs/features.md, room sensors). */
extern const app_mod_t ROOM_MOD;
/* The room air for the bot's room moments (room_air_now on the j2 snapshot), any task; NULL without CONFIG_CINDER_ROOM. */
extern bool (*const ROOM_MOD_AIR)(int *aqi);
/* CONFIG_CINDER_ROOM only, any task: the room task's snapshot and the ROOM_HAS_* chips it found. The app reads the J2 sensors only through this module. */
void room_mod_get(room_t *out);
uint32_t room_mod_found(void);
