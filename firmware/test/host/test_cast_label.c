#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "weather_face.h"
#include "wx_cast.h"

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

#define DAY 1782000000LL

static int text_w(const lv_font_t *f, const char *s)
{
    char plain[64];
    int n = 0;
    for (const char *p = s; *p && n < 63; p++) {
        if (*p == '#') {
            if (p[1] && strlen(p) >= 8 && p[7] == ' ') p += 7;
            continue;
        }
        plain[n++] = *p;
    }
    plain[n] = 0;
    int w = 0;
    for (int i = 0; i < n; i++) w += lv_font_get_glyph_width(f, (uint8_t)plain[i], (uint8_t)plain[i + 1]);
    return w;
}

static bool hit(wx_box_t a, wx_box_t b)
{
    return a.x0 < b.x1 + WX_LABEL_GAP && b.x0 < a.x1 + WX_LABEL_GAP && a.y0 < b.y1 + WX_LABEL_GAP && b.y0 < a.y1 + WX_LABEL_GAP;
}

int main(void)
{
    const lv_font_t *f24 = &lv_font_montserrat_24, *f30 = &lv_font_montserrat_30;
    int shown = 0, hidden = 0, cases = 0, naive = 0;
    for (int m = 0; m < 1440; m++)
        for (int wet = 0; wet <= 1; wet++)
            for (int v = 1; v <= 3; v++)
                for (int kind = 0; kind <= 1; kind++) {
                    long long at = DAY + m * 60LL - 3600, now = at - 30 * 60;
                    wx_obs_t o = {.valid = true, .enabled = true, .now_min = -1, .rise_min = -1, .set_min = -1, .moon_up = -1,
                                  .observed_at = now, .has_obs_age = true, .has_rain = true, .has_utc_offset = true,
                                  .utc_offset = 3600, .has_now_rain = true, .rain_n = 1};
                    snprintf(o.provider, sizeof o.provider, "open-meteo");
                    snprintf(o.condition, sizeof o.condition, "clouds");
                    o.rain_spell[0] = wet ? (wx_spell_t){now - 600, at, v, kind} : (wx_spell_t){at, at + 3600, v, kind};
                    if (wet) {
                        o.now_rain = v;
                        o.rain_until = at;
                    }
                    wx_cast_t c;
                    wx_cast_from(&o, NULL, &c);
                    if (!c.has_count) {
                        CHECK(0, "minute %d wet %d: no countdown", m, wet);
                        continue;
                    }
                    cases++;
                    int lw = text_w(f24, c.label), lh = f24->line_height, x, y;
                    wx_box_t sub = wx_cast_sub_box(text_w(f30, c.sub), f30->line_height);
                    float ax, ay;
                    int al;
                    wx_cast_label_at(c.label_deg, &ax, &ay, &al);
                    bool on = wx_cast_label_place(c.label_deg, lw, lh, sub, &x, &y);
                    wx_box_t lb = {x, y, x + lw, y + lh};
                    CHECK(abs((al < 0 ? x + lw : al == 0 ? x + lw / 2 : x) - (int)(ax + 0.5f)) <= 1 && abs(y + lh / 2 - (int)(ay + 0.5f)) <= 1,
                          "minute %d: the label sits at its anchor", m);
                    CHECK(on == !hit(lb, sub), "minute %d wet %d '%s' '%s': shown exactly when clear of the sub-line", m, wet, c.label, c.sub);
                    CHECK(lb.x0 >= 0 && lb.x1 <= 466 && lb.y0 >= 0 && lb.y1 <= 466, "minute %d: on the face", m);
                    naive += hit(lb, sub);
                    if (on) shown++;
                    else hidden++;
                }
    CHECK(naive > 0, "the old placement collided somewhere (%d cases)", naive);
    long long now = DAY + (14 * 60 + 30) * 60LL - 3600;
    static const int CANVAS[3][2] = {{17 * 60, 1}, {15 * 60 + 15, 3}, {16 * 60, 2}};
    for (int i = 0; i < 3; i++) {
        long long at = DAY + CANVAS[i][0] * 60LL - 3600;
        wx_obs_t o = {.valid = true, .enabled = true, .now_min = -1, .rise_min = -1, .set_min = -1, .moon_up = -1, .observed_at = now,
                      .has_obs_age = true, .has_rain = true, .has_utc_offset = true, .utc_offset = 3600, .has_now_rain = true,
                      .rain_n = 1, .rain_spell = {{at, at + 3600, CANVAS[i][1], 0}}};
        snprintf(o.provider, sizeof o.provider, "open-meteo");
        snprintf(o.condition, sizeof o.condition, "clouds");
        wx_cast_t c;
        wx_cast_from(&o, NULL, &c);
        int x, y;
        CHECK(wx_cast_label_place(c.label_deg, text_w(f24, c.label), f24->line_height,
                                  wx_cast_sub_box(text_w(f30, c.sub), f30->line_height), &x, &y),
              "canvas case %d: the label shows", i);
    }
    printf("cast label: %d cases, label shown %d, hidden %d (would have touched the sub-line %d)\n", cases, shown, hidden, naive);
    if (failures) {
        printf("cast label: %d failure(s)\n", failures);
        return 1;
    }
    printf("cast label: all passed\n");
    return 0;
}
