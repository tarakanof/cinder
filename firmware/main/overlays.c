#include "overlays.h"

#include "config_store.h"
#include "day_view.h"
#include "ember_client.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input_route.h"
#include "lvgl.h"
#include "page_ops.h"
#include "pomo_client.h"
#include "press_route.h"

static const char *TAG = "cinder";

static lv_obj_t *s_reset_layer, *s_reset_arc, *s_reset_label;
static rg_state_t s_reset_shown = RG_IDLE;
static int s_reset_progress_shown = -1;

static EXT_RAM_BSS_ATTR lv_obj_t *s_hold_layer, *s_hold_arc, *s_hold_label;
static EXT_RAM_BSS_ATTR const char *s_hold_text;
static EXT_RAM_BSS_ATTR int s_hold_pct = -1;

#define HOLD_R 204

static void reset_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGW(TAG, "factory reset: erasing settings and Wi-Fi, rebooting");
    esp_err_t err = config_store_erase();
    if (err != ESP_OK) ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
    ember_client_forget_wifi();
    esp_restart();
}

static void hold_overlay_create(void)
{
    s_hold_layer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_hold_layer);
    lv_obj_set_size(s_hold_layer, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_hold_layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_hold_arc = lv_arc_create(s_hold_layer);
    lv_obj_set_size(s_hold_arc, 2 * HOLD_R, 2 * HOLD_R);
    lv_obj_center(s_hold_arc);
    lv_arc_set_bg_angles(s_hold_arc, 0, 360);
    lv_arc_set_rotation(s_hold_arc, 270);
    lv_arc_set_range(s_hold_arc, 0, 100);
    lv_arc_set_value(s_hold_arc, 0);
    lv_obj_remove_style(s_hold_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_hold_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(s_hold_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_hold_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_hold_arc, lv_color_hex(0xF4F4F2), LV_PART_INDICATOR);
    s_hold_label = lv_label_create(s_hold_layer);
    lv_obj_set_style_text_font(s_hold_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_hold_label, lv_color_hex(0xF4F4F2), 0);
    lv_obj_set_style_bg_color(s_hold_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_hold_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_hold_label, 12, 0);
    lv_obj_set_style_pad_ver(s_hold_label, 4, 0);
    lv_label_set_text_static(s_hold_label, "");
    lv_obj_align(s_hold_label, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_add_flag(s_hold_layer, LV_OBJ_FLAG_HIDDEN);
}

static void reset_overlay_create(void)
{
    s_reset_layer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_reset_layer);
    lv_obj_set_size(s_reset_layer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_reset_layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_reset_layer, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_reset_layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_reset_arc = lv_arc_create(s_reset_layer);
    lv_obj_set_size(s_reset_arc, 400, 400);
    lv_obj_center(s_reset_arc);
    lv_arc_set_bg_angles(s_reset_arc, 0, 360);
    lv_arc_set_rotation(s_reset_arc, 270);
    lv_arc_set_range(s_reset_arc, 0, RG_DETENTS);
    lv_arc_set_value(s_reset_arc, 0);
    lv_obj_remove_style(s_reset_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_reset_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_reset_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_reset_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_reset_arc, lv_color_hex(0x262626), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_reset_arc, lv_color_hex(0xE5484D), LV_PART_INDICATOR);

    s_reset_label = lv_label_create(s_reset_layer);
    lv_obj_set_style_text_font(s_reset_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_reset_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_reset_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_reset_label, "Reset knob?\nTurn right to confirm");
    lv_obj_center(s_reset_label);
    lv_obj_add_flag(s_reset_layer, LV_OBJ_FLAG_HIDDEN);
}

void overlays_create(void)
{
    reset_overlay_create();
    hold_overlay_create();
}

bool overlays_reset_frame(rg_state_t st, int progress)
{
    bool closed = false;
    if (st != s_reset_shown) {
        if (st == RG_IDLE) {
            lv_obj_add_flag(s_reset_layer, LV_OBJ_FLAG_HIDDEN);
            closed = true;
        } else {
            lv_obj_remove_flag(s_reset_layer, LV_OBJ_FLAG_HIDDEN);
        }
        if (st == RG_CONFIRMED) {
            lv_label_set_text(s_reset_label, "Resetting");
            xTaskCreatePinnedToCore(reset_task, "reset", 5120, NULL, 4, NULL, 1);
        }
        s_reset_shown = st;
    }
    if (st != RG_IDLE && progress != s_reset_progress_shown) {
        lv_arc_set_value(s_reset_arc, progress);
        s_reset_progress_shown = progress;
    }
    return closed;
}

void overlays_pause(void)
{
    lv_obj_add_flag(s_reset_layer, LV_OBJ_FLAG_HIDDEN);
    s_reset_shown = RG_IDLE;
}

static const char *hold_label(const page_desc_t *pg)
{
    if (pg->frame == page_day_frame) return day_view_hold_label();
    if (pg->frame == page_controls_frame) return "Mac action";
    if (pg->frame != page_pomo_frame) return NULL;
    pomo_snapshot_t snap;
    if (!pomo_client_get(&snap)) return NULL;
    pomo_state_t st = pomo_clock_at(&snap.clock, pomo_client_now());
    return pomo_action_for(&st, POMO_INPUT_LONG_PUSH) == POMO_ACT_NONE ? NULL : "stop";
}

void overlays_hold_tick(const page_desc_t *pg, long long held)
{
    const char *label = held >= 0 ? hold_label(pg) : NULL;
    if (label) input_route_hold_counting();
    int pct = label ? (int)((held - PR_COUNTDOWN_MS) * 100 / (PR_HOLD_MS - PR_COUNTDOWN_MS)) : -1;
    if (pct > 100) pct = 100;
    if (!s_hold_layer || pct == s_hold_pct) return;
    if (pct < 0) {
        lv_obj_add_flag(s_hold_layer, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s_hold_pct < 0) lv_obj_remove_flag(s_hold_layer, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_value(s_hold_arc, pct);
        if (label != s_hold_text) lv_label_set_text_static(s_hold_label, label);
    }
    s_hold_text = label;
    s_hold_pct = pct;
}
