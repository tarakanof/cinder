#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "bsp_knob_15_md50et.h"
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hwt_line.h"
#include "hwt_ui.h"
#include "iot_button.h"
#include "iot_knob.h"
#include "j2.h"
#include "sens_ld2410.h"

#define RX_RING 1024
#define TX_RING 2048
#define KNOB_CLOCKWISE KNOB_LEFT

static const char *TAG = "hwtest";

typedef enum { IN_PUSH, IN_HOLD, IN_TURN_CW, IN_TURN_CCW } input_t;

static QueueHandle_t s_input;
static SemaphoreHandle_t s_tx_mu;
static j2_pins_t s_pins;
static int s_probe = -1;
static bool s_room_wanted = true;

static void send_json(cJSON *o)
{
    char *txt = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!txt) return;
    xSemaphoreTake(s_tx_mu, portMAX_DELAY);
    usb_serial_jtag_write_bytes(HWT_PREFIX, strlen(HWT_PREFIX), pdMS_TO_TICKS(100));
    usb_serial_jtag_write_bytes(txt, strlen(txt), pdMS_TO_TICKS(200));
    usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_tx_mu);
    cJSON_free(txt);
}

static int log_line(const char *fmt, va_list ap)
{
    char buf[256];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n <= 0) return n;
    size_t len = (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1;
    if (xSemaphoreTake(s_tx_mu, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    usb_serial_jtag_write_bytes(buf, len, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_tx_mu);
    return n;
}

static cJSON *reply(int id, bool ok, const char *error)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", id);
    cJSON_AddBoolToObject(o, "ok", ok);
    if (error) cJSON_AddStringToObject(o, "error", error);
    return o;
}

static cJSON *event(const char *name)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ev", name);
    return o;
}

static void pins_line(char *out, size_t cap)
{
    snprintf(out, cap, "SDA %d SCL %d", s_pins.sda, s_pins.scl);
}

static void add_pins(cJSON *o)
{
    cJSON *p = cJSON_AddObjectToObject(o, "pins");
    cJSON_AddNumberToObject(p, "sda", s_pins.sda);
    cJSON_AddNumberToObject(p, "scl", s_pins.scl);
    cJSON_AddNumberToObject(p, "bclk", s_pins.bclk);
    cJSON_AddNumberToObject(p, "lrc", s_pins.lrc);
    cJSON_AddNumberToObject(p, "din", s_pins.din);
    cJSON_AddNumberToObject(p, "radar_rx", s_pins.radar_rx);
    cJSON_AddNumberToObject(p, "radar_bit", s_pins.radar_bit);
    cJSON_AddNumberToObject(p, "pcf", s_pins.pcf_addr);
}

static void add_room(cJSON *o)
{
    room_t r;
    j2_room_get(&r);
    int64_t now = esp_timer_get_time() / 1000;
    cJSON *j = cJSON_AddObjectToObject(o, "room");
    cJSON_AddBoolToObject(j, "running", j2_room_running());
    cJSON_AddNumberToObject(j, "found", j2_room_found());
    cJSON_AddNumberToObject(j, "errors", r.errors);
    cJSON_AddNumberToObject(j, "presence_flips", r.presence_flips);
    if (r.climate_ms) {
        cJSON_AddNumberToObject(j, "temp_c", (int)(r.temp_c * 100) / 100.0);
        cJSON_AddNumberToObject(j, "rh_pct", (int)(r.rh_pct * 10) / 10.0);
        cJSON_AddNumberToObject(j, "climate_age_s", (double)(now - r.climate_ms) / 1000);
    }
    if (r.has & ROOM_HAS_AIR) {
        cJSON_AddStringToObject(j, "air_validity", ens160_validity_name(r.air_validity));
        if (r.air_ms) {
            cJSON_AddNumberToObject(j, "aqi", r.aqi);
            cJSON_AddNumberToObject(j, "tvoc_ppb", r.tvoc_ppb);
            cJSON_AddNumberToObject(j, "eco2_ppm", r.eco2_ppm);
            cJSON_AddNumberToObject(j, "air_age_s", (double)(now - r.air_ms) / 1000);
        }
    }
    if (r.light_ms) cJSON_AddNumberToObject(j, "lux", (int)(r.lux * 10) / 10.0);
    if (r.presence_ms) {
        cJSON_AddBoolToObject(j, "present", r.present);
        cJSON_AddBoolToObject(j, "present_raw", r.present_raw);
    }
    if (r.pressure_ms) cJSON_AddNumberToObject(j, "press_hpa", (int)(r.press_hpa * 10) / 10.0);
}

static void ui_room(void)
{
    char line[64];
    pins_line(line, sizeof line);
    hwt_ui_room(line);
}

static void room_restart(void)
{
    j2_room_stop();
    j2_i2c_close();
    if (s_room_wanted && s_pins.sda >= 0 && s_pins.scl >= 0) j2_room_start(&s_pins);
    ui_room();
}

static void probe_release_all(void)
{
    for (size_t i = 0; i < J2_PROBE_N; i++) j2_probe_float(J2_PROBE_GPIOS[i]);
}

static void probe_stop(void)
{
    if (s_probe < 0) return;
    probe_release_all();
    s_probe = -1;
    ESP_LOGI(TAG, "probe off");
    room_restart();
}

static bool probe_ok(int index) { return J2_PROBE_GPIOS[index] != s_pins.radar_rx; }

static int probe_next(int from, int step)
{
    int n = (int)J2_PROBE_N;
    for (int k = 1; k <= n; k++) {
        int i = ((from + step * k) % n + n) % n;
        if (probe_ok(i)) return i;
    }
    return -1;
}

static void probe_show(int index)
{
    if (s_probe < 0) {
        j2_room_stop();
        j2_i2c_close();
    }
    probe_release_all();
    s_probe = index;
    int gpio = J2_PROBE_GPIOS[index];
    j2_probe_drive(gpio, true);
    hwt_ui_probe(gpio, index, (int)J2_PROBE_N);
    ESP_LOGI(TAG, "probe: GPIO%d high (%d/%d)", gpio, index + 1, (int)J2_PROBE_N);
    cJSON *e = event("probe");
    cJSON_AddNumberToObject(e, "gpio", gpio);
    cJSON_AddNumberToObject(e, "index", index);
    send_json(e);
}

static void handle_input(input_t in)
{
    switch (in) {
    case IN_PUSH: {
        int next = probe_next(s_probe, 1);
        if (next < 0 || next <= s_probe)
            probe_stop();
        else
            probe_show(next);
        break;
    }
    case IN_HOLD:
        probe_stop();
        break;
    case IN_TURN_CW:
    case IN_TURN_CCW:
        if (s_probe >= 0) {
            int next = probe_next(s_probe, in == IN_TURN_CW ? 1 : -1);
            if (next >= 0) probe_show(next);
        }
        break;
    }
}

static bool pin_ok(int gpio) { return gpio == -1 || j2_probe_allowed(gpio); }

static void op_pins(const hwt_req_t *q)
{
    j2_pins_t want = s_pins;
    int *dst[HWT_PIN_N] = {&want.sda, &want.scl, &want.bclk, &want.lrc, &want.din, &want.radar_rx};
    for (int i = 0; i < HWT_PIN_N; i++) {
        if (q->pin[i] != HWT_UNSET && !pin_ok(q->pin[i])) {
            send_json(reply(q->id, false, hwt_pin_name((hwt_pin_t)i)));
            return;
        }
        if (q->pin[i] != HWT_UNSET) *dst[i] = q->pin[i];
    }
    if (q->radar_bit != HWT_UNSET) want.radar_bit = q->radar_bit;
    if (q->pcf != HWT_UNSET) want.pcf_addr = (uint8_t)q->pcf;
    if (!j2_pins_valid(&want)) {
        send_json(reply(q->id, false, "pins_clash"));
        return;
    }
    probe_stop();
    s_pins = want;
    room_restart();
    cJSON *o = reply(q->id, true, NULL);
    add_pins(o);
    send_json(o);
}

static void op_probe(const hwt_req_t *q)
{
    if (q->gpio == HWT_UNSET || q->gpio == -1) {
        probe_stop();
        send_json(reply(q->id, true, NULL));
        return;
    }
    int index = -1;
    for (size_t i = 0; i < J2_PROBE_N; i++)
        if (J2_PROBE_GPIOS[i] == q->gpio) index = (int)i;
    if (index < 0 || !probe_ok(index)) {
        send_json(reply(q->id, false, index < 0 ? "gpio" : "radar_rx"));
        return;
    }
    probe_show(index);
    if (q->level == 0) j2_probe_drive(q->gpio, false);
    cJSON *o = reply(q->id, true, NULL);
    cJSON_AddNumberToObject(o, "gpio", q->gpio);
    cJSON_AddNumberToObject(o, "level", q->level == 0 ? 0 : 1);
    send_json(o);
}

static void op_levels(const hwt_req_t *q)
{
    probe_stop();
    j2_room_stop();
    j2_i2c_close();
    cJSON *o = reply(q->id, true, NULL);
    cJSON *lv = cJSON_AddObjectToObject(o, "levels");
    for (size_t i = 0; i < J2_PROBE_N; i++) {
        char k[8];
        snprintf(k, sizeof k, "%d", J2_PROBE_GPIOS[i]);
        cJSON_AddNumberToObject(lv, k, j2_probe_level(J2_PROBE_GPIOS[i]));
    }
    probe_release_all();
    send_json(o);
    room_restart();
}

static const char *guess(uint8_t a)
{
    switch (a) {
    case 0x23: case 0x5C: return "bh1750";
    case 0x38: return "aht2x (or a pcf8574a at its default)";
    case 0x52: case 0x53: return "ens160";
    case 0x76: case 0x77: return "bmp280";
    default: return a >= 0x20 && a <= 0x27 ? "pcf8574" : a >= 0x39 && a <= 0x3F ? "pcf8574a" : "?";
    }
}

static void op_scan(const hwt_req_t *q)
{
    probe_stop();
    bool mine = !j2_i2c_is_open();
    esp_err_t err = mine ? j2_i2c_open(s_pins.sda, s_pins.scl) : ESP_OK;
    if (err != ESP_OK) {
        send_json(reply(q->id, false, esp_err_to_name(err)));
        return;
    }
    uint8_t found[32];
    size_t n = j2_i2c_scan(found, sizeof found);
    if (mine && !j2_room_running()) j2_i2c_close();
    cJSON *o = reply(q->id, true, NULL);
    cJSON *arr = cJSON_AddArrayToObject(o, "found");
    for (size_t i = 0; i < n; i++) {
        cJSON *d = cJSON_CreateObject();
        char a[8];
        snprintf(a, sizeof a, "0x%02x", found[i]);
        cJSON_AddStringToObject(d, "addr", a);
        cJSON_AddStringToObject(d, "chip", guess(found[i]));
        cJSON_AddItemToArray(arr, d);
    }
    add_pins(o);
    send_json(o);
}

static void op_room(const hwt_req_t *q)
{
    if (q->room == HWT_ROOM_START) {
        probe_stop();
        s_room_wanted = true;
        room_restart();
    } else if (q->room == HWT_ROOM_STOP) {
        s_room_wanted = false;
        j2_room_stop();
        j2_i2c_close();
    }
    cJSON *o = reply(q->id, true, NULL);
    add_room(o);
    send_json(o);
}

static void op_watch(const hwt_req_t *q)
{
    int s = q->s == HWT_UNSET ? 30 : q->s;
    send_json(reply(q->id, true, NULL));
    for (int i = 0; i < s; i++) {
        cJSON *e = event("room");
        add_room(e);
        send_json(e);
        input_t in;
        if (xQueueReceive(s_input, &in, pdMS_TO_TICKS(1000)) == pdTRUE) handle_input(in);
    }
}

static void op_tone(const hwt_req_t *q)
{
    int hz = q->hz == HWT_UNSET ? 880 : q->hz, ms = q->ms == HWT_UNSET ? 400 : q->ms, vol = q->vol == HWT_UNSET ? 20 : q->vol;
    if (ms > 10000) ms = 10000;
    room_t before, after;
    j2_room_get(&before);
    esp_err_t err = j2_amp_tone(&s_pins, (uint32_t)hz, (uint32_t)ms, (uint8_t)vol);
    j2_room_get(&after);
    cJSON *o = reply(q->id, err == ESP_OK, err == ESP_OK ? NULL : esp_err_to_name(err));
    cJSON_AddNumberToObject(o, "presence_flips", after.presence_flips - before.presence_flips);
    cJSON_AddNumberToObject(o, "internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    send_json(o);
}

static void radar_cb(const ld2410_report_t *r, void *arg)
{
    int *n = arg;
    if ((*n)++ % 10) return;
    cJSON *e = event("radar");
    cJSON_AddStringToObject(e, "state", ld2410_target_name(r->state));
    cJSON_AddNumberToObject(e, "move_cm", r->move_cm);
    cJSON_AddNumberToObject(e, "move_energy", r->move_energy);
    cJSON_AddNumberToObject(e, "still_cm", r->still_cm);
    cJSON_AddNumberToObject(e, "still_energy", r->still_energy);
    cJSON_AddNumberToObject(e, "detect_cm", r->detect_cm);
    send_json(e);
}

static void op_radar(const hwt_req_t *q)
{
    int ms = q->ms == HWT_UNSET ? 10000 : q->ms;
    if (s_pins.radar_rx < 0) {
        send_json(reply(q->id, false, "radar_rx"));
        return;
    }
    bool shared = s_pins.radar_rx == s_pins.sda || s_pins.radar_rx == s_pins.scl;
    if (shared) {
        j2_room_stop();
        j2_i2c_close();
    }
    hwt_ui_busy("RADAR", "reading LD2410 UART");
    ld2410_rx_t stats;
    int n = 0;
    esp_err_t err = j2_radar_listen(s_pins.radar_rx, (uint32_t)ms, radar_cb, &n, &stats);
    if (shared) room_restart();
    if (s_probe < 0) ui_room();
    cJSON *o = reply(q->id, err == ESP_OK, err == ESP_OK ? NULL : esp_err_to_name(err));
    cJSON_AddNumberToObject(o, "frames", stats.frames);
    cJSON_AddNumberToObject(o, "dropped", stats.dropped);
    send_json(o);
}

static void op_info(const hwt_req_t *q)
{
    cJSON *o = reply(q->id, true, NULL);
    cJSON_AddStringToObject(o, "app", esp_app_get_description()->project_name);
    cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
    cJSON_AddNumberToObject(o, "internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(o, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON *g = cJSON_AddArrayToObject(o, "probe_gpios");
    for (size_t i = 0; i < J2_PROBE_N; i++) cJSON_AddItemToArray(g, cJSON_CreateNumber(J2_PROBE_GPIOS[i]));
    cJSON_AddNumberToObject(o, "probing", s_probe >= 0 ? J2_PROBE_GPIOS[s_probe] : -1);
    add_pins(o);
    add_room(o);
    send_json(o);
}

static void handle_line(const char *line)
{
    hwt_req_t q;
    const char *error;
    if (!hwt_parse(line, &q, &error)) {
        if (strncmp(line, HWT_PREFIX, strlen(HWT_PREFIX)) == 0) send_json(reply(q.id, false, error));
        return;
    }
    switch (q.op) {
    case HWT_INFO: op_info(&q); break;
    case HWT_PINS: op_pins(&q); break;
    case HWT_PROBE: op_probe(&q); break;
    case HWT_LEVELS: op_levels(&q); break;
    case HWT_SCAN: op_scan(&q); break;
    case HWT_ROOM: op_room(&q); break;
    case HWT_WATCH: op_watch(&q); break;
    case HWT_TONE: op_tone(&q); break;
    case HWT_RADAR: op_radar(&q); break;
    case HWT_STOP:
        probe_stop();
        send_json(reply(q.id, true, NULL));
        break;
    }
}

static void cmd_task(void *arg)
{
    (void)arg;
    static char line[HWT_LINE_MAX + 2];
    size_t n = 0;
    bool overflow = false;
    room_restart();
    for (;;) {
        input_t in;
        while (xQueueReceive(s_input, &in, 0) == pdTRUE) handle_input(in);
        uint8_t buf[64];
        int got = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(50));
        for (int i = 0; i < got; i++) {
            char c = (char)buf[i];
            if (c == '\r') continue;
            if (c != '\n') {
                if (n < sizeof line - 1)
                    line[n++] = c;
                else
                    overflow = true;
                continue;
            }
            line[n] = 0;
            if (!overflow) handle_line(line);
            n = 0;
            overflow = false;
        }
    }
}

static void knob_cb(void *event)
{
    input_t in = (knob_event_t)(intptr_t)event == KNOB_CLOCKWISE ? IN_TURN_CW : IN_TURN_CCW;
    xQueueSend(s_input, &in, 0);
}

static void button_cb(void *event)
{
    button_event_t ev = (button_event_t)(intptr_t)event;
    static bool held;
    input_t in;
    if (ev == BUTTON_PRESS_DOWN) {
        held = false;
        return;
    }
    if (ev == BUTTON_LONG_PRESS_HOLD) {
        if (held) return;
        held = true;
        in = IN_HOLD;
    } else if (ev == BUTTON_PRESS_UP && !held) {
        in = IN_PUSH;
    } else {
        return;
    }
    xQueueSend(s_input, &in, 0);
}

void app_main(void)
{
    s_input = xQueueCreate(8, sizeof(input_t));
    s_tx_mu = xSemaphoreCreateMutex();
    j2_pins_default(&s_pins);
    usb_serial_jtag_driver_config_t dc = {.rx_buffer_size = RX_RING, .tx_buffer_size = TX_RING};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&dc));
    usb_serial_jtag_vfs_use_driver();
    esp_log_set_vprintf(log_line);
    ESP_ERROR_CHECK(hwt_ui_start());
    bsp_knob_15_md50et_register_knob_cb(knob_cb);
    bsp_knob_15_md50et_register_button_cb(button_cb);
    ESP_LOGI(TAG, "ready; internal free %u B", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    xTaskCreatePinnedToCore(cmd_task, "hwt", 6144, NULL, 3, NULL, 1);
}
