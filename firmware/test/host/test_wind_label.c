#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "src/core/lv_global.h"
#include "weather_face.h"
#include "wx_wind.h"

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

static int plain(const char *s, uint32_t *cp, int max)
{
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n < max;) {
        if (*p == '#') {
            p += strlen((const char *)p) >= 8 && p[7] == ' ' ? 8 : 1;
            continue;
        }
        uint32_t c = *p++;
        if (c >= 0xE0) {
            c = ((c & 0x0F) << 12) | ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if (c >= 0xC0) {
            c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        }
        cp[n++] = c;
    }
    return n;
}

static int text_w(const lv_font_t *f, const char *s, bool *all)
{
    uint32_t cp[64];
    int n = plain(s, cp, 63), w = 0;
    cp[n] = 0;
    *all = true;
    for (int i = 0; i < n; i++) {
        lv_font_glyph_dsc_t g;
        *all &= lv_font_get_glyph_dsc(f, &g, cp[i], cp[i + 1]) && g.adv_w > 0;
        w += lv_font_get_glyph_width(f, cp[i], cp[i + 1]);
    }
    return w;
}

int main(void)
{
    const lv_font_t *f30 = &lv_font_montserrat_30;
    const float chord = 2 * sqrtf(216.0f * 216.0f - (338.0f - 233.0f) * (338.0f - 233.0f));
    int widest = 0;
    char worst[40] = "";
    for (int kmh = 0; kmh <= 130; kmh++)
        for (int dir = -1; dir < 360; dir += 45) {
            wx_obs_t o = {.valid = true, .enabled = true, .now_min = -1, .rise_min = -1, .set_min = -1, .moon_up = -1,
                          .has_wind = true, .wind_kmh = kmh, .has_wind_dir = dir >= 0, .wind_dir = dir < 0 ? 0 : dir};
            wx_wind_t w;
            wx_wind_from(&o, NULL, &w);
            bool all;
            int px = text_w(f30, w.sub, &all);
            CHECK(all, "'%s': every glyph in Montserrat 30", w.sub);
            if (px > widest) {
                widest = px;
                snprintf(worst, sizeof worst, "%s", w.sub);
            }
        }
    CHECK(widest + 32 <= chord, "the widest sub-line '%s' (%d px) keeps 16 px from the ring's inner edge (%.0f px)", worst, widest, chord);
    bool all;
    CHECK(text_w(f30, "km/h", &all) > 0 && all, "the unit in Montserrat 30");
    printf("wind label: widest sub-line %d px ('%s'), chord at its baseline %.0f px\n", widest, worst, chord);
    if (failures) {
        printf("wind label: %d failure(s)\n", failures);
        return 1;
    }
    printf("wind label: all passed\n");
    return 0;
}
