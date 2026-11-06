#include <stdio.h>
#include <string.h>

#include "room_report.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static room_t snapshot(int64_t now)
{
    return (room_t){.has = ROOM_HAS_CLIMATE | ROOM_HAS_AIR | ROOM_HAS_LIGHT | ROOM_HAS_PRESENCE,
                    .temp_c = 23.4f, .rh_pct = 41.2f, .climate_ms = now - 4200,
                    .aqi = 2, .tvoc_ppb = 85, .eco2_ppm = 612, .air_validity = ENS160_WARM_UP, .air_ms = now - 1000,
                    .lux = 312, .light_ms = now, .present = true, .presence_ms = now - 59999};
}

static void test_report(void)
{
    const int64_t now = 1000000;
    dev_room_t out;
    room_t r = {0};
    CHECK(!room_report(&r, now, &out) && !out.has_climate && !out.has_presence, "no chip found: no block");

    r = snapshot(now);
    CHECK(room_report(&r, now, &out), "chips found: a block");
    CHECK(out.has_climate && out.t_c == 23.4f && out.rh == 41.2f && out.climate_age_s == 4, "climate, age in whole seconds");
    CHECK(out.has_air && out.eco2 == 612 && out.tvoc == 85 && out.aqi == 2 && strcmp(out.air, "warm_up") == 0 && out.air_age_s == 1,
          "air with its validity");
    CHECK(out.has_light && out.lux == 312 && out.light_age_s == 0, "light");
    CHECK(out.has_presence && out.present && out.presence_age_s == 59, "presence just under 60 s");

    r.presence_ms = now - ROOM_FRESH_MS;
    r.climate_ms = 0;
    room_report(&r, now, &out);
    CHECK(!out.has_presence && !out.has_climate && out.has_air && out.has_light, "60 s old or never read: left out");

    r = snapshot(now);
    r.has = ROOM_HAS_LIGHT;
    CHECK(room_report(&r, now, &out) && out.has_light && !out.has_climate && !out.has_air && !out.has_presence,
          "a reading without its chip in has: left out");
    r = snapshot(now);
    r.has = ROOM_HAS_PRESSURE;
    CHECK(room_report(&r, now, &out) && !out.has_climate && !out.has_air && !out.has_light && !out.has_presence,
          "only the BMP280: a block with nothing in it (Ember has no pressure key)");
    r = snapshot(now);
    r.light_ms = now + 5;
    room_report(&r, now, &out);
    CHECK(!out.has_light, "a reading from the future: left out");

    r = snapshot(now);
    char body[1024];
    dev_checkin_t c = {.fw = "1", .has_room = room_report(&r, now, &(dev_room_t){0})};
    room_report(&r, now, &c.room);
    CHECK(dev_checkin_body(&c, NULL, body, sizeof body) > 0 &&
              strstr(body, "\"room\":{\"age_s\":{\"air\":1,\"climate\":4,\"light\":0,\"presence\":59},\"air\":\"warm_up\",\"aqi\":2,"
                           "\"eco2\":612,\"lux\":312.0,\"present\":true,\"rh\":41.2,\"t_c\":23.4,\"tvoc\":85}"),
          "the fake snapshot through the checkin body: %s", body);
}

int main(void)
{
    test_report();
    if (failures) {
        printf("room: %d failure(s)\n", failures);
        return 1;
    }
    printf("room: all tests passed\n");
    return 0;
}
