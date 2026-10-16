#include "join_view.h"

#include <string.h>

#include "esp_attr.h"

static EXT_RAM_BSS_ATTR lv_obj_t *s_line, *s_ssid;

static lv_obj_t *line(const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xF5A623), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(l, JOIN_VIEW_W, lv_font_get_line_height(font));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

static void shown(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void join_view_create(void)
{
    s_line = line(&lv_font_montserrat_24);
    s_ssid = line(&lv_font_montserrat_18);
    lv_obj_align(s_ssid, LV_ALIGN_BOTTOM_MID, 0, JOIN_VIEW_Y);
}

void join_view_show(join_label_t l, const char *ssid)
{
    char text[64];
    join_label_text(l, ssid, text, sizeof text);
    char *nl = strchr(text, '\n');
    if (nl) *nl = 0;
    if (nl && !nl[1]) nl = NULL;
    lv_label_set_text(s_line, text);
    lv_label_set_text(s_ssid, nl ? nl + 1 : "");
    lv_obj_align(s_line, LV_ALIGN_BOTTOM_MID, 0, JOIN_VIEW_Y - (nl ? lv_font_get_line_height(&lv_font_montserrat_18) : 0));
    shown(s_line, l != JOIN_NONE);
    shown(s_ssid, l != JOIN_NONE && nl);
}

void join_view_shift(int dx, int dy)
{
    lv_obj_set_style_translate_x(s_line, dx, 0);
    lv_obj_set_style_translate_y(s_line, dy, 0);
    lv_obj_set_style_translate_x(s_ssid, dx, 0);
    lv_obj_set_style_translate_y(s_ssid, dy, 0);
}
