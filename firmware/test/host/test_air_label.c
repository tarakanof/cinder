#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font_digits_96.c"
#include "font_face_36.c"
#include "lvgl.h"
#include "src/core/lv_global.h"
#include "weather_face.h"
#include "wx_air.h"

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

#define VALUE_SPACE (-2)
#define UNIT_GAP 6

static int text_w(const lv_font_t *f, const char *s, int space, bool *all)
{
    int w = 0, n = (int)strlen(s);
    *all = true;
    for (int i = 0; i < n; i++) {
        lv_font_glyph_dsc_t g;
        uint32_t next = i + 1 < n ? (uint32_t)(unsigned char)s[i + 1] : 0;
        *all &= lv_font_get_glyph_dsc(f, &g, (unsigned char)s[i], next) && !g.is_placeholder && g.adv_w > 0;
        w += lv_font_get_glyph_width(f, (unsigned char)s[i], next) + (i + 1 < n ? space : 0);
    }
    return w;
}

static float chord(float y) { return 2 * sqrtf(216.0f * 216.0f - (y - 233.0f) * (y - 233.0f)); }

int main(void)
{
    const lv_font_t *f30 = &lv_font_montserrat_30;
    int word_w = 0, val_w = 0;
    char word_worst[24] = "", val_worst[16] = "";
    for (int b = 0; b < WX_AIR_BANDS; b++) {
        bool all;
        int w = text_w(f30, wx_air_word(b), 0, &all);
        CHECK(all, "'%s': every glyph in Montserrat 30", wx_air_word(b));
        if (w > word_w) {
            word_w = w;
            snprintf(word_worst, sizeof word_worst, "%s", wx_air_word(b));
        }
    }
    for (int v = 0; v <= WX_AIR_MAX; v++) {
        char txt[8];
        snprintf(txt, sizeof txt, "%d", v);
        bool vall, uall;
        int w = text_w(&font_digits_96, txt, VALUE_SPACE, &vall) + UNIT_GAP + text_w(f30, "AQI", 0, &uall);
        CHECK(vall && uall, "'%s AQI': every glyph", txt);
        if (w > val_w) {
            val_w = w;
            snprintf(val_worst, sizeof val_worst, "%s AQI", txt);
        }
    }
    CHECK(word_w + 32 <= chord(302 + 36), "the widest band '%s' (%d px) keeps 16 px from the ring", word_worst, word_w);
    CHECK(val_w + 32 <= chord(284), "the widest value '%s' (%d px) keeps 16 px from the ring (%.0f px)", val_worst, val_w, chord(284));
    printf("air label: widest band %d px ('%s'), value %d px ('%s')\n", word_w, word_worst, val_w, val_worst);
    if (failures) {
        printf("air label: %d failure(s)\n", failures);
        return 1;
    }
    printf("air label: all passed\n");
    return 0;
}
