#include "controls_view.h"

#include <stdio.h>
#include <string.h>

#include "controls.h"
#include "esp_attr.h"

#define SCREEN_W 472   /* driver frame 472; visible circle 466 (docs/llm.md) */
#define SCREEN_H 466
#define TEXT_W 400
#define TITLE_Y 136
#define TARGET_Y 198
#define NOTE_Y 280
#define COL_TEXT 0xF4F4F2
#define COL_DIM 0x9A9A9A

static EXT_RAM_BSS_ATTR lv_obj_t *s_parent, *s_root, *s_target, *s_note;
static EXT_RAM_BSS_ATTR char s_target_txt[CTL_TARGET_MAX];
static EXT_RAM_BSS_ATTR const char *s_note_txt;

static lv_obj_t *line(const lv_font_t *f, uint32_t rgb, int y)
{
    lv_obj_t *l = lv_label_create(s_root);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(l, TEXT_W, f->line_height);
    lv_obj_set_pos(l, (SCREEN_W - TEXT_W) / 2, y);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text_static(l, "");
    return l;
}

void controls_view_create(lv_obj_t *parent) { s_parent = parent; }

void controls_view_show(bool on)
{
    if (!s_parent || on == (s_root != NULL)) return;
    if (!on) {
        lv_obj_delete(s_root);
        s_root = s_target = s_note = NULL;
        return;
    }
    s_root = lv_obj_create(s_parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text_static(line(&lv_font_montserrat_30, COL_DIM, TITLE_Y), "Controls");
    s_target = line(&lv_font_montserrat_48, COL_TEXT, TARGET_Y);
    s_note = line(&lv_font_montserrat_30, COL_DIM, NOTE_Y);
    s_target_txt[0] = 0;
    s_note_txt = "";
}

void controls_view_update(const char *target, const char *note)
{
    if (!s_root) return;
    if (strcmp(target, s_target_txt) != 0) {
        snprintf(s_target_txt, sizeof s_target_txt, "%s", target);
        lv_label_set_text(s_target, s_target_txt);
    }
    if (note != s_note_txt) {
        s_note_txt = note;
        lv_label_set_text_static(s_note, note);
    }
}
