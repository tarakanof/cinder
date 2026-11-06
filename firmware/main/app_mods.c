#include "app_mods.h"

#include "room_mod.h"
#include "sdkconfig.h"

static const app_mod_t *const MODS[] = {
#if CONFIG_CINDER_ROOM
    &ROOM_MOD,
#endif
    NULL,
};

app_mods_t APP_MODS = {.mods = MODS, .n = (int)(sizeof MODS / sizeof MODS[0]) - 1};
