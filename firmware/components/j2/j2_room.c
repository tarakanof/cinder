#include "j2.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "sens_pcf8574.h"

#define TICK_MS 200
#define LIGHT_MS 1000
#define AIR_MS 2000
#define CLIMATE_MS 10000
#define PRESSURE_MS 30000
#define DETECT_MS 30000
#define STACK 4096

static const char *TAG = "room";

typedef struct {
    j2_pins_t pins;
    uint8_t ens, bh, bmp, pcf;
    bool aht;
    bmp280_calib_t cal;
    room_presence_t presence;
    room_comp_t comp;
    int64_t next_light, next_air, next_climate, next_pressure, next_detect;
} room_ctx_t;

static TaskHandle_t s_task;
static room_ctx_t *s_ctx;
static atomic_bool s_stop;
static atomic_bool s_done;
static atomic_uint s_found;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static room_t s_room;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void publish(void (*edit)(room_t *, const void *), const void *arg)
{
    taskENTER_CRITICAL(&s_mux);
    edit(&s_room, arg);
    taskEXIT_CRITICAL(&s_mux);
}

static void add_error(room_t *r, const void *arg)
{
    (void)arg;
    r->errors++;
}

static void set_has(room_t *r, const void *arg) { r->has = *(const uint32_t *)arg; }

static uint32_t found_mask(const room_ctx_t *c)
{
    return (c->aht ? ROOM_HAS_CLIMATE : 0) | (c->ens ? ROOM_HAS_AIR : 0) | (c->bh ? ROOM_HAS_LIGHT : 0) | (c->pcf ? ROOM_HAS_PRESENCE : 0) |
           (c->bmp ? ROOM_HAS_PRESSURE : 0);
}

static void detect(room_ctx_t *c)
{
    if (!c->aht && j2_i2c_probe(AHT_ADDR) == ESP_OK && j2_aht_init() == ESP_OK) c->aht = true;
    if (!c->ens && j2_ens160_find(&c->ens) == ESP_OK && j2_ens160_start(c->ens) != ESP_OK) c->ens = 0;
    if (!c->bh && j2_bh1750_find(&c->bh) == ESP_OK && j2_bh1750_start(c->bh) != ESP_OK) c->bh = 0;
    if (!c->bmp && j2_bmp280_find(&c->bmp, &c->cal) != ESP_OK) c->bmp = 0;
    const uint8_t taken[3] = {c->ens, c->bh, c->bmp};
    if (!c->pcf && j2_pcf_find(c->pins.pcf_addr, taken, 3, &c->pcf) != ESP_OK) c->pcf = 0;
    uint32_t has = found_mask(c);
    if (has != atomic_exchange(&s_found, has))
        ESP_LOGI(TAG, "found:%s%s%s%s%s (ens160 %02x, bh1750 %02x, pcf %02x, bmp280 %02x)", c->aht ? " aht" : "", c->ens ? " ens160" : "",
                 c->bh ? " bh1750" : "", c->pcf ? " pcf8574" : "", c->bmp ? " bmp280" : "", c->ens, c->bh, c->pcf, c->bmp);
    publish(set_has, &has);
}

typedef struct {
    bool raw;
    bool changed;
    bool present;
    int64_t at;
} presence_up_t;

static void set_presence(room_t *r, const void *arg)
{
    const presence_up_t *u = arg;
    r->present_raw = u->raw;
    r->present = u->present;
    r->presence_ms = u->at;
    if (u->changed) {
        r->presence_changed_ms = u->at;
        r->presence_flips++;
    }
}

static void poll_presence(room_ctx_t *c, int64_t t)
{
    uint8_t port;
    if (j2_pcf_read(c->pcf, &port) != ESP_OK) {
        publish(add_error, NULL);
        return;
    }
    bool raw = pcf8574_bit(port, c->pins.radar_bit);
    bool changed = room_presence_feed(&c->presence, raw, t);
    publish(set_presence, &(presence_up_t){raw, changed, c->presence.present, t});
}

typedef struct {
    float lux;
    int64_t at;
} light_up_t;

static void set_light(room_t *r, const void *arg)
{
    const light_up_t *u = arg;
    r->lux = u->lux;
    r->light_ms = u->at;
}

typedef struct {
    ens160_status_t st;
    ens160_data_t d;
    bool fresh;
    int64_t at;
} air_up_t;

static void set_air(room_t *r, const void *arg)
{
    const air_up_t *u = arg;
    r->air_validity = u->st.validity;
    if (!u->fresh) return;
    r->aqi = u->d.aqi;
    r->tvoc_ppb = u->d.tvoc_ppb;
    r->eco2_ppm = u->d.eco2_ppm;
    r->air_ms = u->at;
}

typedef struct {
    aht_reading_t a;
    int64_t at;
} climate_up_t;

static void set_climate(room_t *r, const void *arg)
{
    const climate_up_t *u = arg;
    r->temp_c = u->a.temp_c;
    r->rh_pct = u->a.rh_pct;
    r->climate_ms = u->at;
}

typedef struct {
    float hpa;
    int64_t at;
} pressure_up_t;

static void set_pressure(room_t *r, const void *arg)
{
    const pressure_up_t *u = arg;
    r->press_hpa = u->hpa;
    r->pressure_ms = u->at;
}

static void poll(room_ctx_t *c, int64_t t)
{
    if (c->pcf) poll_presence(c, t);
    if (c->bh && t >= c->next_light) {
        c->next_light = t + LIGHT_MS;
        float lux;
        if (j2_bh1750_read(c->bh, &lux) == ESP_OK)
            publish(set_light, &(light_up_t){lux, t});
        else
            publish(add_error, NULL);
    }
    if (c->aht && t >= c->next_climate) {
        c->next_climate = t + CLIMATE_MS;
        aht_reading_t a;
        if (j2_aht_read(&a) == ESP_OK) {
            int64_t at = now_ms();
            publish(set_climate, &(climate_up_t){a, at});
            if (c->ens && room_comp_due(&c->comp, a.temp_c, a.rh_pct, at) && j2_ens160_comp(c->ens, a.temp_c, a.rh_pct) == ESP_OK)
                room_comp_sent(&c->comp, a.temp_c, a.rh_pct, at);
        } else {
            publish(add_error, NULL);
        }
    }
    if (c->ens && t >= c->next_air) {
        c->next_air = t + AIR_MS;
        air_up_t u = {.at = t};
        if (j2_ens160_read(c->ens, &u.st, &u.d, &u.fresh) == ESP_OK)
            publish(set_air, &u);
        else
            publish(add_error, NULL);
    }
    if (c->bmp && t >= c->next_pressure) {
        c->next_pressure = t + PRESSURE_MS;
        bmp280_reading_t b;
        if (j2_bmp280_read(c->bmp, &c->cal, &b) == ESP_OK)
            publish(set_pressure, &(pressure_up_t){(float)b.press_pa / 100.0f, now_ms()});
        else
            publish(add_error, NULL);
    }
    if (found_mask(c) != (ROOM_HAS_CLIMATE | ROOM_HAS_AIR | ROOM_HAS_LIGHT | ROOM_HAS_PRESENCE | ROOM_HAS_PRESSURE) && t >= c->next_detect) {
        c->next_detect = t + DETECT_MS;
        detect(c);
    }
}

static bool bus_up(room_ctx_t *c)
{
    if (j2_i2c_is_open()) return true;
    esp_err_t err = j2_i2c_open(c->pins.sda, c->pins.scl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no I2C bus on SDA %d / SCL %d: %s", c->pins.sda, c->pins.scl, esp_err_to_name(err));
        return false;
    }
    detect(c);
    c->next_detect = now_ms() + DETECT_MS;
    return true;
}

static void room_task(void *arg)
{
    room_ctx_t *c = arg;
    vTaskDelay(pdMS_TO_TICKS(AHT_POWER_ON_MS));
    room_presence_init(&c->presence, 0, c->pins.presence_off_ms, now_ms());
    int64_t retry = 0;
    while (!atomic_load(&s_stop)) {
        int64_t t = now_ms();
        if (t >= retry && !bus_up(c)) retry = t + DETECT_MS;
        if (j2_i2c_is_open()) poll(c, t);
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
    j2_i2c_close();
    atomic_store(&s_found, 0);
    atomic_store(&s_done, true);
    vTaskSuspend(NULL);
}

esp_err_t j2_room_start(const j2_pins_t *pins)
{
    if (s_task) return ESP_ERR_INVALID_STATE;
    if (pins->sda < 0 || pins->scl < 0 || !j2_pins_valid(pins)) return ESP_ERR_INVALID_ARG;
    room_ctx_t *c = heap_caps_calloc(1, sizeof *c, MALLOC_CAP_SPIRAM);
    if (!c) return ESP_ERR_NO_MEM;
    c->pins = *pins;
    taskENTER_CRITICAL(&s_mux);
    memset(&s_room, 0, sizeof s_room);
    taskEXIT_CRITICAL(&s_mux);
    atomic_store(&s_stop, false);
    atomic_store(&s_done, false);
    if (xTaskCreatePinnedToCoreWithCaps(room_task, "room", STACK, c, 2, &s_task, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        heap_caps_free(c);
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_ctx = c;
    return ESP_OK;
}

void j2_room_stop(void)
{
    if (!s_task) return;
    atomic_store(&s_stop, true);
    while (!atomic_load(&s_done)) vTaskDelay(pdMS_TO_TICKS(20));
    vTaskDeleteWithCaps(s_task);
    s_task = NULL;
    heap_caps_free(s_ctx);
    s_ctx = NULL;
}

bool j2_room_running(void) { return s_task != NULL; }

void j2_room_get(room_t *out)
{
    taskENTER_CRITICAL(&s_mux);
    *out = s_room;
    taskEXIT_CRITICAL(&s_mux);
}

uint32_t j2_room_found(void) { return atomic_load(&s_found); }
