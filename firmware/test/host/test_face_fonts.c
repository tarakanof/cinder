#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font_digits_96.c"
#include "font_face_36.c"
#include "lvgl.h"

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

static uint8_t px[128 * 128];

/* The glyphs the faces draw in each font (docs/face-design.md "Fonts"); U+00B0 degree, U+00BD one half,
 * U+2212 minus. */
static const uint32_t FACE_36[] = {' ', '%', '+', '-', '.', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                                   ':', 'h', 0xb0, 0x2212};
static const uint32_t DIGITS_96[] = {' ', '%', '+', '-', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':',
                                     0xb0, 0xbd, 0x2212};

static void check_glyph(const lv_font_t *f, const char *name, uint32_t cp)
{
    lv_font_glyph_dsc_t g;
    CHECK(lv_font_get_glyph_dsc(f, &g, cp, 0) && !g.is_placeholder, "%s: U+%04X has a glyph", name, (unsigned)cp);
    CHECK(g.adv_w > 0, "%s: U+%04X advances", name, (unsigned)cp);
    if (cp == ' ') return;
    CHECK(g.box_w > 0 && g.box_h > 0 && g.box_w <= 128 && g.box_h <= 128, "%s: U+%04X box %dx%d", name,
          (unsigned)cp, g.box_w, g.box_h);
    CHECK(g.ofs_y >= -f->base_line && g.ofs_y + g.box_h <= f->line_height - f->base_line,
          "%s: U+%04X fits the line (ofs_y %d, h %d)", name, (unsigned)cp, g.ofs_y, g.box_h);
    if (g.box_w > 128 || g.box_h > 128) return;
    lv_draw_buf_t buf = {.header = {.w = g.box_w, .h = g.box_h, .stride = g.box_w}, .data = px,
                         .data_size = sizeof px};
    memset(px, 0, sizeof px);
    CHECK(lv_font_get_bitmap_fmt_txt(&g, &buf) == &buf, "%s: U+%04X bitmap decodes", name, (unsigned)cp);
    int top = 0, bottom = 0, left = 0, right = 0;
    for (int x = 0; x < g.box_w; x++) {
        top |= px[x];
        bottom |= px[(g.box_h - 1) * g.box_w + x];
    }
    for (int y = 0; y < g.box_h; y++) {
        left |= px[y * g.box_w];
        right |= px[y * g.box_w + g.box_w - 1];
    }
    CHECK(top && bottom && left && right, "%s: U+%04X box is cropped to its ink", name, (unsigned)cp);
}

static void check_packed(const lv_font_t *f, const char *name, uint32_t bitmap_size)
{
    const lv_font_fmt_txt_dsc_t *d = f->dsc;
    uint32_t n = d->cmaps[0].list_length;
    CHECK(d->cmap_num == 1 && d->stride == 0 && d->bitmap_format == LV_FONT_FMT_TXT_PLAIN && d->bpp == 4,
          "%s: one sparse cmap, plain 4 bpp, no row stride", name);
    for (uint32_t id = 1; id <= n; id++) {
        const lv_font_fmt_txt_glyph_dsc_t *g = &d->glyph_dsc[id];
        uint32_t bytes = ((uint32_t)g->box_w * g->box_h + 1) / 2;
        uint32_t end = id < n ? d->glyph_dsc[id + 1].bitmap_index : bitmap_size;
        CHECK(g->bitmap_index + bytes == end, "%s: glyph %u ends at %u, not %u", name, (unsigned)id,
              (unsigned)(g->bitmap_index + bytes), (unsigned)end);
    }
}

static int32_t width(const lv_font_t *f, const uint32_t *cps, size_t n)
{
    int32_t w = 0;
    lv_font_glyph_dsc_t g;
    for (size_t i = 0; i < n; i++)
        if (lv_font_get_glyph_dsc(f, &g, cps[i], 0)) w += g.adv_w;
    return w;
}

static void test_glyphs(void)
{
    for (size_t i = 0; i < sizeof FACE_36 / sizeof FACE_36[0]; i++) check_glyph(&font_face_36, "36", FACE_36[i]);
    for (size_t i = 0; i < sizeof DIGITS_96 / sizeof DIGITS_96[0]; i++)
        check_glyph(&font_digits_96, "96", DIGITS_96[i]);
    lv_font_glyph_dsc_t g;
    CHECK(!lv_font_get_glyph_dsc(&font_face_36, &g, 'x', 0), "36: only the subset");
    CHECK(!lv_font_get_glyph_dsc(&font_digits_96, &g, 'h', 0), "96: only the subset");
}

static void test_packing(void)
{
    check_packed(&font_face_36, "36", sizeof font_face_36_bitmap);
    check_packed(&font_digits_96, "96", sizeof font_digits_96_bitmap);
}

static void test_line_box_matches_builtin_montserrat_36(void)
{
    const lv_font_t *f = &font_face_36, *m = &lv_font_montserrat_36;
    CHECK(f->line_height == m->line_height && f->base_line == m->base_line, "line %d/%d, base %d/%d",
          (int)f->line_height, (int)m->line_height, (int)f->base_line, (int)m->base_line);
    CHECK(f->underline_position == m->underline_position && f->underline_thickness == m->underline_thickness,
          "underline %d/%d, %d/%d", f->underline_position, m->underline_position, f->underline_thickness,
          m->underline_thickness);
}

static void test_widest_labels_fit(void)
{
    /* End labels sit 96 px in from the face edge and must stay clear of the centre column. */
    const uint32_t sunrise[] = {'0', '7', ':', '3', '0'}, cold[] = {0x2212, '1', '5', 0xb0}, pct[] = {'1', '0', '0', '%'};
    CHECK(width(&font_face_36, sunrise, 5) <= 110, "\"07:30\" is %d px", (int)width(&font_face_36, sunrise, 5));
    CHECK(width(&font_face_36, cold, 4) <= 110, "\"-15°\" is %d px", (int)width(&font_face_36, cold, 4));
    CHECK(width(&font_face_36, pct, 4) <= 110, "\"100%%\" is %d px", (int)width(&font_face_36, pct, 4));
    /* Big values stay inside the 300 px middle band. */
    const uint32_t t[] = {0x2212, '1', '5', 0xb0}, aqi[] = {'1', '0', '0'}, clock[] = {'1', '2', ':', '3', '4'},
                   half[] = {'2', 0xbd};
    CHECK(width(&font_digits_96, t, 4) <= 300, "\"-15°\" is %d px", (int)width(&font_digits_96, t, 4));
    CHECK(width(&font_digits_96, aqi, 3) <= 300, "\"100\" is %d px", (int)width(&font_digits_96, aqi, 3));
    CHECK(width(&font_digits_96, clock, 5) <= 300, "\"12:34\" is %d px", (int)width(&font_digits_96, clock, 5));
    CHECK(width(&font_digits_96, half, 2) <= 300, "\"2½\" is %d px", (int)width(&font_digits_96, half, 2));
}

int main(void)
{
    test_glyphs();
    test_packing();
    test_line_box_matches_builtin_montserrat_36();
    test_widest_labels_fit();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("face fonts: all tests passed\n");
    return 0;
}
