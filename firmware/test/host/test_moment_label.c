#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "src/core/lv_global.h"
#include "weather_face.h"
#include "wx_moment.h"
#include "wx_temp.h"

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


#define NOW 1782043200LL

static wx_obs_t obs(int code, int night, int cloud, float temp)
{
    wx_obs_t o = {.valid = true, .enabled = true, .has_temp = true, .temp_c = temp, .now_min = -1, .rise_min = -1,
                  .set_min = -1, .moon_up = -1, .observed_at = NOW - 300, .has_obs_age = true, .obs_age_s = 300,
                  .has_utc_offset = true, .utc_offset = 3600, .has_now_rain = true, .has_rain = true, .has_night = true,
                  .night = night, .has_cloud = cloud >= 0, .cloud = cloud, .has_wind = true};
    snprintf(o.provider, sizeof o.provider, "open-meteo");
    snprintf(o.code, sizeof o.code, "%d", code);
    return o;
}

static int s_widest, s_n;
static char s_worst[WXM_TEXT_MAX];

static void check(const lv_font_t *f, const wxm_moment_t *m)
{
    bool all;
    int w = text_w(f, m->text, &all);
    s_n++;
    CHECK(all, "a glyph missing in \"%s\"", m->text);
    CHECK(w <= WXM_TEXT_W, "\"%s\" is %d px, over the %d px line", m->text, w, WXM_TEXT_W);
    if (w > s_widest) {
        s_widest = w;
        snprintf(s_worst, sizeof s_worst, "%s", m->text);
    }
}

int main(void)
{
    const lv_font_t *f30 = &lv_font_montserrat_30;
    static wx_temp_t scratch;
    wxm_moment_t m;
    static const int CODES[] = {0, 1, 2, 3, 45, 48, 51, 61, 65, 71, 73, 75, 95, 99};
    static const int CLOUDS[] = {-1, 0, 20, 50, 80, 95};
    static const wxm_kind_t TEMPS[] = {WXM_GLANCE, WXM_NIGHT, WXM_FROST, WXM_HEAT, WXM_SNOW};
    for (size_t c = 0; c < sizeof CODES / sizeof CODES[0]; c++)
        for (int night = 0; night < 2; night++)
            for (size_t k = 0; k < sizeof CLOUDS / sizeof CLOUDS[0]; k++)
                for (int temp = -99; temp <= 99; temp++) {
                    wx_obs_t o = obs(CODES[c], night, CLOUDS[k], (float)temp);
                    for (size_t i = 0; i < sizeof TEMPS / sizeof TEMPS[0]; i++)
                        if (wxm_moment_from(TEMPS[i], &o, false, &scratch, &m)) check(f30, &m);
                }
    for (int kmh = 0; kmh <= WX_WIND_MAX_KMH; kmh++) {
        wx_obs_t o = obs(2, 0, -1, 12);
        o.wind_kmh = kmh;
        if (wxm_moment_from(WXM_WIND, &o, false, &scratch, &m)) check(f30, &m);
    }
    for (int in = 1; in <= 3; in++)
        for (int min = 0; min < 1440; min++) {
            wx_obs_t o = obs(61, 0, -1, 9);
            o.now_rain = in;
            o.rain_until = NOW + 600 + 60 * min;
            if (wxm_moment_from(WXM_RAIN, &o, false, &scratch, &m)) check(f30, &m);
            o.rain_until = 0;
            if (wxm_moment_from(WXM_RAIN, &o, false, &scratch, &m)) check(f30, &m);
        }
    wx_obs_t o = obs(95, 0, -1, 20);
    if (wxm_moment_from(WXM_STORM, &o, false, &scratch, &m)) check(f30, &m);
    if (wxm_moment_from(WXM_AFTERRAIN, &o, false, &scratch, &m)) check(f30, &m);
    CHECK(s_n > 100000, "texts checked: %d", s_n);
    CHECK(lv_font_get_line_height(f30) <= WXM_TEXT_H, "the 30 px line fits the %d px text box: %d", WXM_TEXT_H,
          lv_font_get_line_height(f30));
    if (failures) {
        printf("moment label: %d failure(s)\n", failures);
        return 1;
    }
    printf("moment label: %d texts fit the %d px line, widest \"%s\" %d px\n", s_n, WXM_TEXT_W, s_worst, s_widest);
    return 0;
}
