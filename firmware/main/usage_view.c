#include "usage_view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pomo_client.h"
#include "tool_marks.h"
#include "usage_client.h"

LV_FONT_DECLARE(font_digits_96);

#define SCREEN_W 472
#define SCREEN_H 466
#define CX 236
#define MARK_TOP 82
#define NAME_Y 140
#define NAME_W 300
#define LINE_W 340
#define WEEK_W 300
#define VALUE_BASE 262
#define RESET_BASE 310
#define WEEK_BASE 350
#define PCT_GAP 4
#define TICK_OUT 10
#define TICK_IN 6
#define OTHER_MARK 36

static const char *TAG = "usage";

typedef struct {
    float frac;
    uint32_t rgb;
} ring_key_t;

static EXT_RAM_BSS_ATTR struct {
    lv_obj_t *parent, *root, *ring, *mark, *other, *name, *value, *pct, *reset, *week, *tick_out, *tick_in;
    uint16_t *buf;
    lv_image_dsc_t mark_img;
    usage_block_t blk;
    usage_clock_t clk;
    unsigned gen;
    bool moved;
    usage_nav_t nav;
    usage_face_t face;
    ring_key_t key5, key7;
    int dots_n, dots_idx;
    bool ring_drawn, offline;
    int face_idx;
    long long face_sec;
    int mark_tool;
    uint32_t mark_rgb;
    int tick_x, tick_y;
    char name_txt[sizeof ((usage_acct_t *)0)->name], value_txt[sizeof ((usage_face_t *)0)->value],
        reset_txt[sizeof ((usage_face_t *)0)->reset], week_txt[sizeof ((usage_face_t *)0)->week_line];
} V;

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *disc(lv_obj_t *parent, int r, uint32_t rgb)
{
    lv_obj_t *o = plain(parent);
    lv_obj_set_size(o, 2 * r, 2 * r);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static int top_for(const lv_font_t *f, int base) { return base - (f->line_height - f->base_line); }

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *f, int w, int base, uint32_t rgb)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    lv_label_set_text_static(l, "");
    if (w > 0) {
        lv_obj_set_size(l, w, f->line_height);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(l, CX - w / 2, top_for(f, base));
    }
    return l;
}

void usage_view_create(lv_obj_t *parent) { V.parent = parent; }

static void destroy(void)
{
    if (V.root) lv_obj_delete(V.root);
    V.root = NULL;
    heap_caps_free(V.buf);
    V.buf = NULL;
}

static bool build(void)
{
    V.buf = heap_caps_calloc(SCREEN_W * SCREEN_H, 2, MALLOC_CAP_SPIRAM);
    if (!V.buf) {
        ESP_LOGW(TAG, "no PSRAM for the face (%d B): staying on the bot", SCREEN_W * SCREEN_H * 2);
        return false;
    }
    V.root = plain(V.parent);
    lv_obj_set_size(V.root, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(V.root, 0, 0);
    V.ring = lv_canvas_create(V.root);
    lv_obj_remove_flag(V.ring, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(V.ring, V.buf, SCREEN_W, SCREEN_H, LV_COLOR_FORMAT_RGB565);
    V.mark = lv_image_create(V.root);
    lv_obj_remove_flag(V.mark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_image_recolor_opa(V.mark, LV_OPA_COVER, 0);
    V.other = plain(V.root);
    lv_obj_set_size(V.other, OTHER_MARK, OTHER_MARK);
    lv_obj_set_pos(V.other, CX - OTHER_MARK / 2, MARK_TOP + (TOOL_MARK_H - OTHER_MARK) / 2);
    lv_obj_set_style_radius(V.other, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(V.other, 5, 0);
    V.name = text(V.root, &lv_font_montserrat_24, NAME_W, NAME_Y + lv_font_montserrat_24.line_height - lv_font_montserrat_24.base_line,
                  USAGE_DIM);
    V.value = text(V.root, &font_digits_96, 0, VALUE_BASE, USAGE_GREEN);
    V.pct = text(V.root, &lv_font_montserrat_48, 0, VALUE_BASE, USAGE_GREEN);
    lv_label_set_text_static(V.pct, "%");
    V.reset = text(V.root, &lv_font_montserrat_24, LINE_W, RESET_BASE, USAGE_TEXT);
    V.week = text(V.root, &lv_font_montserrat_24, WEEK_W, WEEK_BASE, USAGE_DIM);
    V.tick_out = disc(V.root, TICK_OUT, 0x000000);
    V.tick_in = disc(V.root, TICK_IN, USAGE_TEXT);
    V.ring_drawn = false;
    V.face_idx = -1;
    V.face_sec = -1;
    V.mark_tool = -1;
    V.mark_rgb = 0xFFFFFFFF;
    V.tick_x = V.tick_y = -1000;
    V.name_txt[0] = V.value_txt[0] = V.reset_txt[0] = V.week_txt[0] = 0;
    return true;
}

static void show(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void set_text(lv_obj_t *l, char *shown, size_t cap, const char *s)
{
    if (strcmp(shown, s) == 0) return;
    snprintf(shown, cap, "%s", s);
    lv_label_set_text(l, s);
}

static void set_color(lv_obj_t *l, uint32_t rgb)
{
    lv_color_t c = lv_color_hex(rgb);
    if (!lv_color_eq(lv_obj_get_style_text_color(l, LV_PART_MAIN), c)) lv_obj_set_style_text_color(l, c, 0);
}

static void apply_mark(const usage_face_t *f)
{
    if (f->tool == V.mark_tool && f->mark_rgb == V.mark_rgb) return;
    uint32_t rgb;
    int w, h;
    const uint8_t *m = tool_mark(f->tool, &rgb, &w, &h);
    if (f->tool != V.mark_tool && m) {
        V.mark_img = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = (uint32_t)w, .h = (uint32_t)h, .stride = (uint32_t)w},
            .data_size = (uint32_t)(w * h),
            .data = m,
        };
        lv_image_set_src(V.mark, &V.mark_img);
        lv_obj_set_pos(V.mark, CX - w / 2, MARK_TOP + (TOOL_MARK_H - h) / 2);
    }
    show(V.mark, m != NULL);
    show(V.other, m == NULL);
    lv_obj_set_style_image_recolor(V.mark, lv_color_hex(f->mark_rgb), 0);
    lv_obj_set_style_border_color(V.other, lv_color_hex(f->mark_rgb), 0);
    V.mark_tool = f->tool;
    V.mark_rgb = f->mark_rgb;
}

static void apply_value(const usage_face_t *f)
{
    bool moved = strcmp(V.value_txt, f->value) != 0;
    set_text(V.value, V.value_txt, sizeof V.value_txt, f->value);
    set_color(V.value, f->value_rgb);
    set_color(V.pct, f->value_rgb);
    show(V.pct, strcmp(f->value, "--") != 0);
    if (!moved && V.face_idx >= 0) return;
    lv_point_t v, p;
    lv_text_get_size(&v, f->value, &font_digits_96, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&p, "%", &lv_font_montserrat_48, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int w = v.x + (lv_obj_has_flag(V.pct, LV_OBJ_FLAG_HIDDEN) ? 0 : PCT_GAP + p.x), x = CX - w / 2;
    lv_obj_set_pos(V.value, x, top_for(&font_digits_96, VALUE_BASE));
    lv_obj_set_pos(V.pct, x + v.x + PCT_GAP, top_for(&lv_font_montserrat_48, VALUE_BASE));
}

static void apply_tick(const usage_face_t *f)
{
    show(V.tick_out, f->tick);
    show(V.tick_in, f->tick);
    if (!f->tick) return;
    float a = f->tick_frac * 6.2831853f;
    int x = (int)lroundf(USAGE_CX + USAGE_R5 * sinf(a)), y = (int)lroundf(USAGE_CY - USAGE_R5 * cosf(a));
    if (x == V.tick_x && y == V.tick_y) return;
    V.tick_x = x;
    V.tick_y = y;
    lv_obj_set_pos(V.tick_out, x - TICK_OUT, y - TICK_OUT);
    lv_obj_set_pos(V.tick_in, x - TICK_IN, y - TICK_IN);
}

static bool same_key(const ring_key_t *k, float frac, uint32_t rgb) { return V.ring_drawn && k->frac == frac && k->rgb == rgb; }

static void apply_rings(const usage_face_t *f)
{
    bool r5 = !same_key(&V.key5, f->five_frac, f->five_rgb), r7 = !same_key(&V.key7, f->week_frac, f->week_rgb);
    bool dots = !V.ring_drawn || V.dots_n != V.blk.n || V.dots_idx != V.nav.idx;
    if (!r5 && !r7 && !dots) return;
    int64_t t0 = esp_timer_get_time();
    if (r5) usage_ring5_render(V.buf, SCREEN_W, SCREEN_H, f);
    if (r7) usage_ring7_render(V.buf, SCREEN_W, SCREEN_H, f);
    if (dots) usage_dots_render(V.buf, SCREEN_W, SCREEN_H, V.blk.n, V.nav.idx);
    if (r5 || r7) {
        lv_obj_invalidate(V.ring);
    } else {
        int x, y, w, h;
        usage_dots_box(&x, &y, &w, &h);
        lv_obj_invalidate_area(V.ring, &(lv_area_t){x, y, x + w - 1, y + h - 1});
    }
    ESP_LOGI(TAG, "face %d/%d: rings in %d ms", V.nav.idx + 1, V.blk.n, (int)((esp_timer_get_time() - t0) / 1000));
    V.key5 = (ring_key_t){f->five_frac, f->five_rgb};
    V.key7 = (ring_key_t){f->week_frac, f->week_rgb};
    V.dots_n = V.blk.n;
    V.dots_idx = V.nav.idx;
    V.ring_drawn = true;
}

static const char *week_text(const usage_face_t *f)
{
    if (!f->week_short[0]) return f->week_line;
    lv_point_t p;
    lv_text_get_size(&p, f->week_line, lv_obj_get_style_text_font(V.week, LV_PART_MAIN), lv_obj_get_style_text_letter_space(V.week, LV_PART_MAIN),
                     0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_update_layout(V.week);
    return p.x > lv_obj_get_content_width(V.week) ? f->week_short : f->week_line;
}

static double server_now(void) { return V.clk.have_srv ? pomo_client_now() + V.clk.srv_offset : 0; }

static void apply_face(void)
{
    double now = server_now();
    long long sec = (long long)floor(now);
    if (!V.moved && V.face_idx == V.nav.idx && sec == V.face_sec) return;
    usage_face_t *f = &V.face;
    const usage_acct_t *a = &V.blk.a[V.nav.idx];
    usage_acct_t off;
    if (V.offline) {
        off = usage_as_stale(a, V.clk.fed_at);
        a = &off;
    }
    usage_face(a, now, V.clk.has_utc_offset, V.clk.utc_offset, f);
    apply_rings(f);
    apply_mark(f);
    set_text(V.name, V.name_txt, sizeof V.name_txt, V.blk.a[V.nav.idx].name);
    apply_value(f);
    set_text(V.reset, V.reset_txt, sizeof V.reset_txt, f->reset);
    set_color(V.reset, f->reset_rgb);
    set_text(V.week, V.week_txt, sizeof V.week_txt, week_text(f));
    apply_tick(f);
    V.face_idx = V.nav.idx;
    V.face_sec = sec;
    V.moved = false;
}

void usage_view_poll(void)
{
    if (!usage_client_take(&V.gen, &V.blk, &V.clk)) return;
    V.moved = true;
    if (usage_nav_sync(&V.nav, &V.blk)) usage_view_close();
}

bool usage_view_open(void) { return V.nav.open; }

void usage_view_set_offline(bool offline)
{
    if (offline == V.offline) return;
    V.offline = offline;
    V.moved = true;
}

bool usage_view_hint(void) { return !V.offline && usage_hint(&V.blk, server_now()); }

void usage_view_close(void)
{
    usage_nav_close(&V.nav);
    destroy();
}

bool usage_view_input(const usage_in_t *in, double t)
{
    bool was = V.nav.open;
    usage_act_t a = usage_input(&V.nav, &V.blk, in, t);
    if (a == USAGE_ACT_OPEN && !build()) usage_nav_close(&V.nav);
    if (a == USAGE_ACT_CLOSE) destroy();
    if (a == USAGE_ACT_OPEN && V.nav.open) apply_face();
    return was || V.nav.open;
}

bool usage_view_frame(double t)
{
    if (!V.nav.open) return false;
    if (usage_nav_tick(&V.nav, t)) {
        destroy();
        return false;
    }
    apply_face();
    return true;
}
