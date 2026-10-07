#include "ota_face.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"

#define ARC_SIZE 456
#define ARC_W 12
#define COL_TITLE 0xFF8A3D
#define COL_VERSION 0x9A9A9A
#define COL_PCT 0xF4F4F2

LV_FONT_DECLARE(font_title_bold);

static atomic_bool s_want;
static atomic_int s_pct;
static atomic_bool s_restarting;
static atomic_uint s_gen;
static EXT_RAM_BSS_ATTR char s_version[32];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static lv_obj_t *s_scr, *s_prev, *s_arc, *s_title, *s_version_label, *s_pct_label;
static int s_shown_pct = -1;
static bool s_shown_restarting;
static unsigned s_shown_gen;

void ota_face_show(const char *version)
{
    taskENTER_CRITICAL(&s_mux);
    snprintf(s_version, sizeof s_version, "%s", version ? version : "");
    taskEXIT_CRITICAL(&s_mux);
    atomic_store(&s_pct, 0);
    atomic_store(&s_restarting, false);
    atomic_fetch_add(&s_gen, 1);
    atomic_store(&s_want, true);
}

void ota_face_pct(int pct) { atomic_store(&s_pct, pct < 0 ? 0 : pct > 100 ? 100 : pct); }

void ota_face_restarting(void)
{
    atomic_store(&s_restarting, true);
    atomic_store(&s_want, true);
}

void ota_face_hide(void) { atomic_store(&s_want, false); }

bool ota_face_requested(void) { return atomic_load(&s_want); }

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int width, int dy)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, width);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static void create(void)
{
    s_prev = lv_screen_active();
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_arc = lv_arc_create(s_scr);
    lv_obj_set_size(s_arc, ARC_SIZE, ARC_SIZE);
    lv_obj_set_pos(s_arc, 8, 4);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 0);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x262626), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(COL_PCT), LV_PART_INDICATOR);

    s_title = label(s_scr, &font_title_bold, COL_TITLE, OTA_FACE_TITLE_W, -2);
    s_version_label = label(s_scr, &lv_font_montserrat_30, COL_VERSION, 320, 38);
    lv_obj_set_height(s_version_label, lv_font_montserrat_30.line_height);
    lv_label_set_long_mode(s_version_label, LV_LABEL_LONG_MODE_DOTS);
    s_pct_label = label(s_scr, &lv_font_montserrat_48, COL_PCT, 320, 150);
    s_shown_pct = -1;
    s_shown_restarting = false;
    s_shown_gen = atomic_load(&s_gen) - 1;
    lv_screen_load(s_scr);
}

static void destroy(void)
{
    if (s_prev) lv_screen_load(s_prev);
    lv_obj_delete(s_scr);
    s_scr = s_arc = s_title = s_version_label = s_pct_label = NULL;
}

bool ota_face_frame(void)
{
    bool want = atomic_load(&s_want);
    if (!want) {
        if (s_scr) destroy();
        return false;
    }
    if (!s_scr) create();
    unsigned gen = atomic_load(&s_gen);
    bool restarting = atomic_load(&s_restarting);
    if (gen != s_shown_gen || restarting != s_shown_restarting) {
        char version[sizeof s_version];
        taskENTER_CRITICAL(&s_mux);
        memcpy(version, s_version, sizeof version);
        taskEXIT_CRITICAL(&s_mux);
        lv_label_set_text_static(s_title, restarting ? OTA_FACE_TITLE_RESTART : OTA_FACE_TITLE);
        lv_label_set_text(s_version_label, version);
        s_shown_gen = gen;
        s_shown_restarting = restarting;
    }
    int pct = atomic_load(&s_pct);
    if (pct != s_shown_pct) {
        char t[8];
        snprintf(t, sizeof t, "%d %%", pct);
        lv_label_set_text(s_pct_label, t);
        lv_arc_set_value(s_arc, pct);
        s_shown_pct = pct;
    }
    return true;
}
