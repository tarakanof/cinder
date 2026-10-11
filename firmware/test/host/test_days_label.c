#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font_face_36.c"
#include "lvgl.h"
#include "src/core/lv_global.h"
#include "weather_face.h"
#include "wx_days.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

const lv_font_t lv_font_montserrat_14;
lv_global_t lv_global;
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

#define INNER_R 216.0f
#define MARGIN 6.0f

static int text_w(const lv_font_t *f, const char *s, bool *all)
{
    uint32_t cp[32];
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n < 31;) {
        uint32_t c = *p++;
        if (c >= 0xE0) {
            c = ((c & 0x0F) << 12) | ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if (c >= 0xC0) {
            c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        }
        cp[n++] = c;
    }
    cp[n] = 0;
    int w = 0;
    *all = true;
    for (int i = 0; i < n; i++) {
        lv_font_glyph_dsc_t g;
        *all &= lv_font_get_glyph_dsc(f, &g, cp[i], cp[i + 1]) && !g.is_placeholder && g.adv_w > 0;
        w += lv_font_get_glyph_width(f, cp[i], cp[i + 1]);
    }
    return w;
}

static void deg(char *buf, size_t n, int v)
{
    if (v < 0) snprintf(buf, n, "\xE2\x88\x92%d\xC2\xB0", -v);
    else snprintf(buf, n, "%d\xC2\xB0", v);
}

static bool inside(float x, float y) { return hypotf(x - 233, y - 233) <= INNER_R - MARGIN; }

static void test_widths(void)
{
    bool all;
    char buf[16];
    for (int v = -99; v <= 99; v++) {
        deg(buf, sizeof buf, v);
        int w = text_w(&font_face_36, buf, &all);
        CHECK(all && w == wx_days_deg_w(v), "'%s' in font_face_36: %d px, the table says %d", buf, w, wx_days_deg_w(v));
    }
    for (int d = 0; d < 7; d++) {
        int w = text_w(&lv_font_montserrat_30, wx_weekday_name(d), &all);
        CHECK(all && w == wx_days_name_w(d), "'%s' in Montserrat 30: %d px, the table says %d", wx_weekday_name(d), w, wx_days_name_w(d));
    }
    for (int p = 0; p <= 100; p++) {
        snprintf(buf, sizeof buf, "%d%%", p);
        int w = text_w(&lv_font_montserrat_24, buf, &all);
        CHECK(all && w <= WX_DAYS_ICON + 2 * WX_DAYS_GAP + 2, "'%s' in Montserrat 24: %d px, within the icon and its gaps", buf, w);
    }
}

static wx_days_t rows(int n, int wday, int lo, int hi)
{
    wx_days_t d = {.on = true, .have = n > 0, .n = n, .amin = lo, .amax = hi > lo ? hi : lo + 1};
    for (int i = 0; i < n; i++) d.row[i] = (wx_days_row_t){(signed char)(wday + i), WX_ICON_RAIN, 100, (short)lo, (short)hi};
    return d;
}

static void test_fit(void)
{
    const int line = font_face_36.line_height, base = font_face_36.base_line;
    const int name_line = lv_font_montserrat_30.line_height, pop_line = lv_font_montserrat_24.line_height;
    int worst_x0 = 466, worst_x1 = 0;
    char worst[48] = "";
    for (int lo = -45; lo <= 45; lo++)
        for (int hi = lo; hi <= 45; hi += 3)
            for (int wday = 0; wday < 7; wday++) {
                wx_days_t d = rows(3, wday, lo, hi);
                wx_days_layout_t l;
                wx_days_layout(&d, &l);
                const wx_days_cols_t *c = &l.c;
                for (int i = 0; i < l.n; i++) {
                    float top = (float)(l.y[i] - line / 2 + line - base - 25), bottom = (float)(l.y[i] - line / 2 + line - base);
                    float name_top = (float)(l.y[i] - name_line / 2 + 4);
                    bool ok = inside((float)c->x0, top) && inside((float)c->x0, bottom) && inside((float)c->x1, top) &&
                              inside((float)c->x1, bottom) && inside((float)c->x0, name_top) && inside((float)c->x0, name_top + 22);
                    float pop_y = (float)(l.y[i] + WX_DAYS_POP_DY + pop_line);
                    ok &= inside((float)(c->icon_x - WX_DAYS_GAP), pop_y) && inside((float)(c->icon_x + WX_DAYS_ICON + WX_DAYS_GAP), pop_y);
                    if (!ok) {
                        CHECK(false, "%s %d..%d row %d: %d..%d leaves the ring's inner edge less than %.0f px", wx_weekday_name(wday), lo,
                              hi, i, c->x0, c->x1, MARGIN);
                        return;
                    }
                }
                CHECK(c->day_x + wx_days_name_w(d.row[0].wday) <= c->icon_x - 4 && c->lo_x + c->lo_w <= c->bar_x - 4 &&
                          c->bar_x + c->bar_w <= c->hi_x - 4,
                      "%d..%d: columns apart", lo, hi);
                if (c->x1 - c->x0 > worst_x1 - worst_x0) {
                    worst_x0 = c->x0;
                    worst_x1 = c->x1;
                    snprintf(worst, sizeof worst, "%s %d..%d", wx_weekday_name(wday), lo, hi);
                }
            }
    CHECK(WX_DAYS_POP_DY >= line - line / 2 - base + 2, "the chance starts below the temperatures' ink");
    printf("days label: widest row %d px (%s), %d..%d\n", worst_x1 - worst_x0, worst, worst_x0, worst_x1);
}

static void test_sub(void)
{
    static const char *const AGE[] = {"just now", "59 min ago", "23 h ago", "999 d ago", "stale"};
    bool all;
    float y = WX_DAYS_SUB_Y + lv_font_montserrat_30.line_height - lv_font_montserrat_30.base_line;
    float chord = 2 * sqrtf((INNER_R - MARGIN) * (INNER_R - MARGIN) - (y - 233) * (y - 233));
    for (size_t i = 0; i < sizeof AGE / sizeof AGE[0]; i++) {
        int w = text_w(&lv_font_montserrat_30, AGE[i], &all);
        CHECK(all && w <= chord, "offline '%s' (%d px) under the rows fits the chord (%.0f px)", AGE[i], w, chord);
    }
    wx_days_t d = rows(3, 0, 0, 10);
    wx_days_layout_t l;
    wx_days_layout(&d, &l);
    CHECK(WX_DAYS_SUB_Y >= l.y[2] + WX_DAYS_POP_DY + lv_font_montserrat_24.line_height, "the offline line starts below the last row's chance");
}

int main(void)
{
    test_widths();
    test_fit();
    test_sub();
    if (failures) {
        printf("days label: %d failure(s)\n", failures);
        return 1;
    }
    printf("days label: all passed\n");
    return 0;
}
