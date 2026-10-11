#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font_digits_96.c"
#include "font_face_36.c"
#include "lvgl.h"
#include "src/core/lv_global.h"
#include "weather_face.h"
#include "wx_sun.h"

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

#define MID 1781996400LL
#define H(h, m) (MID + (h) * 3600LL + (m) * 60LL)
#define VALUE_SPACE (-2)
#define UNIT_GAP 6
#define END_X 96
#define END_Y 362

static int text_w(const lv_font_t *f, const char *s, int space, bool *all)
{
    uint32_t cp[64];
    int n = 0, w = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n < 63;) {
        uint32_t c = *p++;
        if (c >= 0xC0) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        cp[n++] = c;
    }
    cp[n] = 0;
    *all = true;
    for (int i = 0; i < n; i++) {
        lv_font_glyph_dsc_t g;
        *all &= lv_font_get_glyph_dsc(f, &g, cp[i], cp[i + 1]) && !g.is_placeholder && g.adv_w > 0;
        w += lv_font_get_glyph_width(f, cp[i], cp[i + 1]) + (i + 1 < n ? space : 0);
    }
    return w;
}

static float chord(float y) { return 2 * sqrtf(216.0f * 216.0f - (y - 233.0f) * (y - 233.0f)); }

int main(void)
{
    const lv_font_t *f30 = &lv_font_montserrat_30;
    int sub_w = 0, val_w = 0, end_w = 0;
    char sub_worst[40] = "", val_worst[40] = "", end_worst[8] = "";
    for (int off = -43200; off <= 50400; off += 3600)
        for (long long now = H(0, 0); now < H(24, 0); now += 300) {
            wx_obs_t o = {.valid = true, .enabled = true, .now_min = -1, .rise_min = -1, .set_min = -1, .moon_up = 1,
                          .observed_at = now, .has_obs_age = true, .has_utc_offset = true, .utc_offset = off,
                          .sunrise = H(7, 30), .sunset = H(19, 30),
                          .twilight = {H(6, 55), H(7, 13), H(8, 30), H(18, 30), H(19, 47), H(20, 5)}};
            wx_sun_t s;
            wx_sun_from(&o, &s);
            bool all;
            int w = text_w(f30, s.sub, 0, &all);
            CHECK(all, "'%s': every glyph in Montserrat 30", s.sub);
            if (w > sub_w) {
                sub_w = w;
                snprintf(sub_worst, sizeof sub_worst, "%s", s.sub);
            }
            for (int k = 0; k <= wx_sun_count_n(&s); k++) {
                char v[16];
                const char *u = wx_sun_value(&s, k, v, sizeof v);
                bool vall, uall;
                int vw = text_w(&font_digits_96, v, VALUE_SPACE, &vall) + UNIT_GAP + text_w(f30, u, 0, &uall);
                CHECK(vall && uall, "'%s %s': every glyph", v, u);
                if (vw > val_w) {
                    val_w = vw;
                    snprintf(val_worst, sizeof val_worst, "%s %s", v, u);
                }
            }
            const char *ends[2] = {s.rise, s.set};
            for (int i = 0; i < 2; i++) {
                int ew = text_w(&font_face_36, ends[i], 0, &all);
                CHECK(all, "'%s': every glyph in font_face_36", ends[i]);
                if (ew > end_w) {
                    end_w = ew;
                    snprintf(end_worst, sizeof end_worst, "%s", ends[i]);
                }
            }
        }
    const char *polar[] = {"Midnight sun", "Polar night", "of daylight today"};
    for (int i = 0; i < 3; i++) {
        bool all;
        int w = text_w(f30, polar[i], 0, &all);
        CHECK(all && w < sub_w + 1, "'%s' (%d px) no wider than the widest timed line", polar[i], w);
    }
    float sub_chord = chord(302 + 36);
    CHECK(sub_w + 32 <= sub_chord, "the widest sub-line '%s' (%d px) keeps 16 px from the ring (%.0f px)", sub_worst, sub_w, sub_chord);
    CHECK(val_w + 32 <= chord(284), "the widest value '%s' (%d px) keeps 16 px from the ring (%.0f px)", val_worst, val_w, chord(284));
    float ink = END_Y + font_face_36.line_height - font_face_36.base_line;
    float lx = 233 - END_X, rx = 233 - (END_X + end_w);
    CHECK(sqrtf(lx * lx + (ink - 233) * (ink - 233)) <= 216 && rx > 0,
          "the end labels' ink ('%s', %d px) stays inside the ring and clear of the centre", end_worst, end_w);
    printf("sun label: widest sub-line %d px ('%s'), value %d px ('%s'), end label %d px ('%s')\n", sub_w, sub_worst, val_w,
           val_worst, end_w, end_worst);
    if (failures) {
        printf("sun label: %d failure(s)\n", failures);
        return 1;
    }
    printf("sun label: all passed\n");
    return 0;
}
