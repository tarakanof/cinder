#include "room_report.h"

#include <string.h>

bool room_fresh(int64_t at_ms, int64_t now_ms) { return at_ms > 0 && now_ms >= at_ms && now_ms - at_ms < ROOM_FRESH_MS; }

static bool fresh(int64_t at_ms, int64_t now_ms, uint32_t *age_s)
{
    if (!room_fresh(at_ms, now_ms)) return false;
    *age_s = (uint32_t)((now_ms - at_ms) / 1000);
    return true;
}

bool room_report(const room_t *r, int64_t now_ms, dev_room_t *out)
{
    memset(out, 0, sizeof *out);
    if (!r->has) return false;
    out->has_climate = (r->has & ROOM_HAS_CLIMATE) && fresh(r->climate_ms, now_ms, &out->climate_age_s);
    out->t_c = r->temp_c;
    out->rh = r->rh_pct;
    out->has_air = (r->has & ROOM_HAS_AIR) && fresh(r->air_ms, now_ms, &out->air_age_s);
    out->eco2 = r->eco2_ppm;
    out->tvoc = r->tvoc_ppb;
    out->aqi = r->aqi;
    out->air = ens160_validity_name(r->air_validity);
    out->has_light = (r->has & ROOM_HAS_LIGHT) && fresh(r->light_ms, now_ms, &out->light_age_s);
    out->lux = r->lux;
    out->has_presence = (r->has & ROOM_HAS_PRESENCE) && fresh(r->presence_ms, now_ms, &out->presence_age_s);
    out->present = r->present;
    return true;
}
