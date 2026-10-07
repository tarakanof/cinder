#include "pomo_view.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "pomo_ring.h"

#define SCREEN_W 472   /* driver frame 472; visible circle 466 (docs/llm.md) */
#define SCREEN_H 466

#define COL_FOCUS 0xFF6A3D
#define COL_BREAK 0x4FA9FF
#define COL_OTHER 0x9A9A9A
#define COL_IDLE_TRACK 0x262626
#define COL_TEXT 0xF4F4F2
#define COL_TEXT_DIM 0x5A5A5A
#define COL_NOTE 0x9A9A9A
#define TRACK_GAIN 0.18f
#define PAUSED_GAIN 0.45f

static const pomo_ring_t RING = {.cx = 236, .cy = 233, .r = 222, .hw = 6};

static lv_obj_t *s_root, *s_ring, *s_time, *s_phase, *s_round;
static uint16_t *s_ring_buf;
static pomo_ring_work_t *s_work;

static bool s_ring_drawn;
static float s_ring_frac;
static uint32_t s_ring_arc, s_ring_track;
static char s_time_txt[12], s_phase_txt[24], s_round_txt[16];
static uint32_t s_time_col = 1, s_phase_col = 1;
static const char *s_note;

static uint32_t scale_rgb(uint32_t rgb, float k)
{
    uint32_t r = (uint32_t)(((rgb >> 16) & 0xFF) * k), g = (uint32_t)(((rgb >> 8) & 0xFF) * k),
             b = (uint32_t)((rgb & 0xFF) * k);
    return (r << 16) | (g << 8) | b;
}

static lv_obj_t *label_create(lv_obj_t *parent, const lv_font_t *font, int width, int dy)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, width);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text_static(l, "");
    return l;
}

void pomo_view_create(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_ring_buf = heap_caps_calloc(SCREEN_W * SCREEN_H, 2, MALLOC_CAP_SPIRAM);
    s_work = heap_caps_malloc(sizeof *s_work, MALLOC_CAP_SPIRAM);
    assert(s_ring_buf && s_work);
    s_ring = lv_canvas_create(s_root);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(s_ring, s_ring_buf, SCREEN_W, SCREEN_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_ring, 0, 0);

    s_time = label_create(s_root, &lv_font_montserrat_48, 260, -6);
    s_phase = label_create(s_root, &lv_font_montserrat_24, 300, 46);
    s_round = label_create(s_root, &lv_font_montserrat_14, 200, -54);
    lv_obj_set_style_text_color(s_round, lv_color_hex(COL_TEXT_DIM), 0);
}

void pomo_view_show(bool show)
{
    if (show) lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void pomo_view_set_note(const char *note) { s_note = note; }

static void label_text(lv_obj_t *l, char *shown, size_t cap, const char *txt)
{
    if (strcmp(shown, txt) == 0) return;
    strlcpy(shown, txt, cap);
    lv_label_set_text_static(l, shown);
}

static void label_color(lv_obj_t *l, uint32_t *shown, uint32_t rgb)
{
    if (*shown == rgb) return;
    *shown = rgb;
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
}

static void ring_update(float frac, uint32_t arc, uint32_t track)
{
    if (!s_ring_drawn || arc != s_ring_arc || track != s_ring_track) {
        pomo_ring_render(&RING, s_work, s_ring_buf, SCREEN_W, SCREEN_H, (pomo_rect_t){0, 0, SCREEN_W, SCREEN_H},
                         frac, arc, track);
        lv_obj_invalidate(s_ring);
    } else {
        pomo_rect_t d;
        if (!pomo_ring_dirty(&RING, s_ring_frac, frac, SCREEN_W, SCREEN_H, &d)) return;
        pomo_ring_render(&RING, s_work, s_ring_buf, SCREEN_W, SCREEN_H, d, frac, arc, track);
        lv_area_t a = {.x1 = d.x, .y1 = d.y, .x2 = d.x + d.w - 1, .y2 = d.y + d.h - 1};
        lv_obj_invalidate_area(s_ring, &a);
    }
    s_ring_drawn = true;
    s_ring_frac = frac;
    s_ring_arc = arc;
    s_ring_track = track;
}

static const char *phase_name(pomo_phase_t p)
{
    switch (p) {
    case POMO_PHASE_FOCUS: return "FOCUS";
    case POMO_PHASE_SHORT_BREAK: return "BREAK";
    case POMO_PHASE_LONG_BREAK: return "LONG BREAK";
    default: return "TIMER";
    }
}

void pomo_view_update(const pomo_state_t *s, double now)
{
    pomo_mode_t mode = s ? pomo_mode(s) : POMO_MODE_IDLE;
    uint32_t base = !s ? COL_OTHER
                       : s->phase == POMO_PHASE_FOCUS ? COL_FOCUS
                       : pomo_phase_is_break(s->phase) ? COL_BREAK : COL_OTHER;
    bool active = s && mode != POMO_MODE_IDLE;

    float frac = active ? pomo_fraction(s) : 0;
    uint32_t arc = mode == POMO_MODE_RUNNING ? base : scale_rgb(base, PAUSED_GAIN);
    uint32_t track = active ? scale_rgb(base, TRACK_GAIN) : COL_IDLE_TRACK;
    ring_update(frac, active ? arc : 0, track);

    char txt[12];
    if (active) pomo_format_mmss(s->remaining_sec, txt, sizeof txt);
    else strlcpy(txt, "--:--", sizeof txt);
    label_text(s_time, s_time_txt, sizeof s_time_txt, txt);
    bool dim = !active || (mode == POMO_MODE_PAUSED && ((long)floor(now)) % 2);
    label_color(s_time, &s_time_col, dim ? COL_TEXT_DIM : COL_TEXT);

    char ph[24];
    uint32_t ph_col = arc;
    if (s_note) {
        strlcpy(ph, s_note, sizeof ph);
        ph_col = COL_NOTE;
    } else if (!s) {
        strlcpy(ph, "OFFLINE", sizeof ph);
        ph_col = COL_NOTE;
    } else if (mode == POMO_MODE_IDLE) {
        strlcpy(ph, "PUSH TO START", sizeof ph);
        ph_col = COL_NOTE;
    } else if (mode == POMO_MODE_PAUSED) {
        strlcpy(ph, "PAUSED", sizeof ph);
    } else if (mode == POMO_MODE_PARKED) {
        snprintf(ph, sizeof ph, "%s NEXT", phase_name(s->phase));
    } else {
        strlcpy(ph, phase_name(s->phase), sizeof ph);
    }
    label_text(s_phase, s_phase_txt, sizeof s_phase_txt, ph);
    label_color(s_phase, &s_phase_col, ph_col);

    char rd[16] = "";
    if (s && s->round > 0) snprintf(rd, sizeof rd, "%d DONE", s->round);
    label_text(s_round, s_round_txt, sizeof s_round_txt, rd);
}
