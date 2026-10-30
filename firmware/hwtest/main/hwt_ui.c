#include "hwt_ui.h"

#include <stdio.h>
#include <string.h>

#include "bsp_knob_15_md50et.h"
#include "esp_timer.h"
#include "j2.h"
#include "lvgl.h"

#define UI_MS 250
#define STALE_MS 30000
#define BRIGHTNESS 60

typedef struct {
    hwt_ui_mode_t mode;
    int gpio, index, count;
    char headline[32];
    char detail[96];
    char pins[64];
} ui_state_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static ui_state_t s_state = {.mode = HWT_UI_ROOM};
static lv_obj_t *s_title, *s_head, *s_sub, *s_body;
static bool s_lit;

static bool fresh(int64_t at_ms, int64_t now_ms) { return at_ms && now_ms - at_ms < STALE_MS; }

static void room_text(char *out, size_t cap, const char *pins)
{
    room_t r;
    j2_room_get(&r);
    int64_t now = esp_timer_get_time() / 1000;
    char climate[40] = "climate --", air[48] = "air --", light[24] = "light --", pres[40] = "presence --", press[24] = "";
    if (fresh(r.climate_ms, now)) snprintf(climate, sizeof climate, "%.1f\xC2\xB0" "C  %.0f%%", r.temp_c, r.rh_pct);
    if (r.has & ROOM_HAS_AIR) {
        if (fresh(r.air_ms, now))
            snprintf(air, sizeof air, "eCO2 %u  TVOC %u  AQI %u%s", r.eco2_ppm, r.tvoc_ppb, r.aqi,
                     r.air_validity == ENS160_NORMAL ? "" : r.air_validity == ENS160_WARM_UP ? " (warm-up)" : " (start-up)");
        else
            snprintf(air, sizeof air, "air: %s", ens160_validity_name(r.air_validity));
    }
    if (fresh(r.light_ms, now)) snprintf(light, sizeof light, "%.0f lx", r.lux);
    if (fresh(r.presence_ms, now)) snprintf(pres, sizeof pres, "%s  flips %u", r.present ? "PRESENT" : "away", (unsigned)r.presence_flips);
    if (fresh(r.pressure_ms, now)) snprintf(press, sizeof press, "\n%.1f hPa", r.press_hpa);
    snprintf(out, cap, "%s\n%s\n%s\n%s%s\n%s  err %u", climate, air, light, pres, press, pins, (unsigned)r.errors);
}

static void found_text(char *out, size_t cap)
{
    uint32_t f = j2_room_found();
    snprintf(out, cap, "%s%s%s%s%s", f & ROOM_HAS_CLIMATE ? "aht " : "", f & ROOM_HAS_AIR ? "ens160 " : "", f & ROOM_HAS_LIGHT ? "bh1750 " : "",
             f & ROOM_HAS_PRESENCE ? "pcf " : "", f & ROOM_HAS_PRESSURE ? "bmp280" : "");
    if (!out[0]) snprintf(out, cap, j2_room_running() ? "no sensors found" : "sensors off");
}

static void ui_timer(lv_timer_t *t)
{
    (void)t;
    if (!s_lit) {
        s_lit = bsp_knob_15_md50et_set_brightness(BRIGHTNESS) == ESP_OK;
    }
    ui_state_t s;
    taskENTER_CRITICAL(&s_mux);
    s = s_state;
    taskEXIT_CRITICAL(&s_mux);
    char sub[48], body[256];
    switch (s.mode) {
    case HWT_UI_PROBE:
        lv_label_set_text_fmt(s_head, "GPIO %d", s.gpio);
        snprintf(sub, sizeof sub, "HIGH  %d/%d", s.index + 1, s.count);
        lv_label_set_text(s_sub, sub);
        lv_label_set_text(s_body, "Measure each J2 pin to GND.\nAbout 3.3 V marks this GPIO.\npush: next   hold: exit");
        break;
    case HWT_UI_BUSY:
        lv_label_set_text(s_head, s.headline);
        lv_label_set_text(s_sub, "");
        lv_label_set_text(s_body, s.detail);
        break;
    default:
        lv_label_set_text(s_head, "ROOM");
        found_text(sub, sizeof sub);
        lv_label_set_text(s_sub, sub);
        room_text(body, sizeof body, s.pins);
        lv_label_set_text(s_body, body);
        break;
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, 360);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    lv_label_set_text(l, "");
    return l;
}

esp_err_t hwt_ui_start(void)
{
    bsp_knob_15_md50et_handles_t h = {0};
    esp_err_t err = bsp_knob_15_md50et_init(&h);
    if (err != ESP_OK) return err;
    if ((err = bsp_knob_15_md50et_lock(-1)) != ESP_OK) return err;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_title = label(scr, &lv_font_montserrat_18, lv_color_hex(0x808080), 44);
    lv_label_set_text(s_title, "cinder hwtest");
    s_head = label(scr, &lv_font_montserrat_48, lv_color_white(), 80);
    s_sub = label(scr, &lv_font_montserrat_18, lv_color_hex(0xFFB000), 140);
    s_body = label(scr, &lv_font_montserrat_24, lv_color_white(), 176);
    lv_timer_create(ui_timer, UI_MS, NULL);
    bsp_knob_15_md50et_unlock();
    return ESP_OK;
}

static void copy(char *dst, size_t cap, const char *src)
{
    size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void set(void (*edit)(ui_state_t *, const void *), const void *arg)
{
    taskENTER_CRITICAL(&s_mux);
    edit(&s_state, arg);
    taskEXIT_CRITICAL(&s_mux);
}

static void edit_room(ui_state_t *s, const void *arg)
{
    s->mode = HWT_UI_ROOM;
    copy(s->pins, sizeof s->pins, arg);
}

void hwt_ui_room(const char *pins_line) { set(edit_room, pins_line ? pins_line : ""); }

typedef struct {
    int gpio, index, count;
} probe_arg_t;

static void edit_probe(ui_state_t *s, const void *arg)
{
    const probe_arg_t *p = arg;
    s->mode = HWT_UI_PROBE;
    s->gpio = p->gpio;
    s->index = p->index;
    s->count = p->count;
}

void hwt_ui_probe(int gpio, int index, int count) { set(edit_probe, &(probe_arg_t){gpio, index, count}); }

typedef struct {
    const char *headline, *detail;
} busy_arg_t;

static void edit_busy(ui_state_t *s, const void *arg)
{
    const busy_arg_t *b = arg;
    s->mode = HWT_UI_BUSY;
    copy(s->headline, sizeof s->headline, b->headline ? b->headline : "");
    copy(s->detail, sizeof s->detail, b->detail ? b->detail : "");
}

void hwt_ui_busy(const char *headline, const char *detail) { set(edit_busy, &(busy_arg_t){headline, detail}); }
