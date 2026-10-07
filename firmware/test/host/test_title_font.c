#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font_title_bold.c"
#include "lvgl.h"
#include "ota_face.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

const lv_font_t lv_font_montserrat_14;
void lv_memset(void *dst, uint8_t v, size_t len) { memset(dst, v, len); }
void *lv_memcpy(void *dst, const void *src, size_t len) { return memcpy(dst, src, len); }
int lv_strcmp(const char *a, const char *b) { return strcmp(a, b); }
void *lv_malloc(size_t size) { return malloc(size); }
void *lv_malloc_zeroed(size_t size) { return calloc(1, size); }
void lv_free(void *p) { free(p); }
void lv_draw_buf_flush_cache(const lv_draw_buf_t *b, const lv_area_t *a) { (void)b; (void)a; }
uint32_t lv_draw_buf_width_to_stride(uint32_t w, lv_color_format_t cf) { (void)cf; return w; }
void *lv_utils_bsearch(const void *key, const void *base, size_t n, size_t size,
                       int (*cmp)(const void *ref, const void *element))
{
    return bsearch(key, base, n, size, cmp);
}

static uint8_t px[64 * 64];

static void check_glyph(const char *title, char ch)
{
    lv_font_glyph_dsc_t g;
    const lv_font_t *f = &font_title_bold;
    CHECK(lv_font_get_glyph_dsc(f, &g, (uint8_t)ch, 0) && !g.is_placeholder, "'%c' of \"%s\" has a glyph", ch, title);
    CHECK(g.adv_w > 0, "'%c' advances", ch);
    if (ch == ' ') return;
    CHECK(g.box_w > 0 && g.box_h > 0 && g.box_w <= 64 && g.box_h <= 64, "'%c' box %dx%d", ch, g.box_w, g.box_h);
    CHECK(g.ofs_y >= -f->base_line && g.ofs_y + g.box_h <= f->line_height - f->base_line,
          "'%c' fits the line (ofs_y %d, h %d)", ch, g.ofs_y, g.box_h);
    if (g.box_w > 64 || g.box_h > 64) return;
    lv_draw_buf_t buf = {.header = {.w = g.box_w, .h = g.box_h, .stride = g.box_w}, .data = px,
                         .data_size = sizeof px};
    memset(px, 0, sizeof px);
    CHECK(lv_font_get_bitmap_fmt_txt(&g, &buf) == &buf, "'%c' bitmap decodes", ch);
    int top = 0, bottom = 0, left = 0, right = 0;
    for (int x = 0; x < g.box_w; x++) {
        top |= px[x];
        bottom |= px[(g.box_h - 1) * g.box_w + x];
    }
    for (int y = 0; y < g.box_h; y++) {
        left |= px[y * g.box_w];
        right |= px[y * g.box_w + g.box_w - 1];
    }
    CHECK(top && bottom && left && right, "'%c' box is cropped to its ink", ch);
}

static void test_titles_have_glyphs(void)
{
    const char *titles[] = {OTA_FACE_TITLE, OTA_FACE_TITLE_RESTART};
    for (size_t t = 0; t < sizeof titles / sizeof titles[0]; t++)
        for (const char *c = titles[t]; *c; c++) check_glyph(titles[t], *c);
}

static void test_bitmaps_are_packed_back_to_back(void)
{
    const lv_font_fmt_txt_dsc_t *d = font_title_bold.dsc;
    uint32_t n = d->cmaps[0].list_length;
    CHECK(d->cmap_num == 1 && d->stride == 0 && d->bitmap_format == LV_FONT_FMT_TXT_PLAIN && d->bpp == 4,
          "one sparse cmap, plain 4 bpp, no row stride");
    for (uint32_t id = 1; id <= n; id++) {
        const lv_font_fmt_txt_glyph_dsc_t *g = &d->glyph_dsc[id];
        uint32_t bytes = ((uint32_t)g->box_w * g->box_h + 1) / 2;
        uint32_t end = id < n ? d->glyph_dsc[id + 1].bitmap_index : sizeof glyph_bitmap;
        CHECK(g->bitmap_index + bytes == end, "glyph %u ends at %u, not %u", (unsigned)id,
              (unsigned)(g->bitmap_index + bytes), (unsigned)end);
    }
}

static void test_line_box_matches_builtin_montserrat_36(void)
{
    const lv_font_t *f = &font_title_bold, *m = &lv_font_montserrat_36;
    CHECK(f->line_height == m->line_height && f->base_line == m->base_line, "line %d/%d, base %d/%d",
          (int)f->line_height, (int)m->line_height, (int)f->base_line, (int)m->base_line);
    CHECK(f->underline_position == m->underline_position && f->underline_thickness == m->underline_thickness,
          "underline %d/%d, %d/%d", f->underline_position, m->underline_position, f->underline_thickness,
          m->underline_thickness);
}

static void test_titles_fit_one_line(void)
{
    const char *titles[] = {OTA_FACE_TITLE, OTA_FACE_TITLE_RESTART};
    for (size_t t = 0; t < sizeof titles / sizeof titles[0]; t++) {
        int32_t w = 0;
        lv_font_glyph_dsc_t g;
        for (const char *c = titles[t]; *c; c++)
            if (lv_font_get_glyph_dsc(&font_title_bold, &g, (uint8_t)*c, 0)) w += g.adv_w;
        CHECK(w <= OTA_FACE_TITLE_W, "\"%s\" is %d px, label %d", titles[t], (int)w, OTA_FACE_TITLE_W);
    }
}

static void test_other_letters_are_missing(void)
{
    lv_font_glyph_dsc_t g;
    CHECK(!lv_font_get_glyph_dsc(&font_title_bold, &g, 'x', 0), "the subset holds only the titles' letters");
}

int main(void)
{
    test_titles_have_glyphs();
    test_bitmaps_are_packed_back_to_back();
    test_line_box_matches_builtin_montserrat_36();
    test_titles_fit_one_line();
    test_other_letters_are_missing();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("title font: all tests passed\n");
    return 0;
}
