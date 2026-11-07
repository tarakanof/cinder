#include "room_mod.h"

#include <stddef.h>

#include "sdkconfig.h"

#if CONFIG_CINDER_ROOM

#include "ember_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "j2.h"
#include "room_face.h"
#include "room_report.h"

static const char *TAG = "room";

void room_mod_get(room_t *out) { j2_room_get(out); }

uint32_t room_mod_found(void) { return j2_room_found(); }

static bool room_start(const knob_settings_t *ks)
{
    if (!ks->room_enabled) return false;
    j2_pins_t pins;
    j2_pins_default(&pins);
    j2_room_on_presence(ember_client_presence);
    esp_err_t err = j2_room_start(&pins);
    if (err != ESP_OK) {
        j2_room_on_presence(NULL);
        ESP_LOGW(TAG, "room task not started: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "room task started (SDA %d, SCL %d)", pins.sda, pins.scl);
    return true;
}

static void room_stop(void)
{
    j2_room_stop();
    j2_room_on_presence(NULL);
    ESP_LOGI(TAG, "room task stopped");
}

static bool room_settings(const knob_settings_t *ks) { return ks->room_enabled; }

static void room_checkin(dev_checkin_t *c)
{
    room_t r;
    room_mod_get(&r);
    c->has_room = room_report(&r, esp_timer_get_time() / 1000, &c->room);
}

static void room_diag(dev_diag_t *d)
{
    int free_b = j2_room_stack_free();
    if (free_b >= 0) dev_diag_add_task(d, "room", (uint32_t)free_b);
}

static bool room_air(int *aqi)
{
    room_t r;
    room_mod_get(&r);
    return room_air_now(&r, esp_timer_get_time() / 1000, aqi);
}

bool (*const ROOM_MOD_AIR)(int *aqi) = room_air;

const app_mod_t ROOM_MOD = {
    .name = "room",
    .start = room_start,
    .stop = room_stop,
    .checkin = room_checkin,
    .settings = room_settings,
    .diag = room_diag,
};

#else

bool (*const ROOM_MOD_AIR)(int *aqi) = NULL;

#endif
