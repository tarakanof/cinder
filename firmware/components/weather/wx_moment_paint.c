#include "wx_moment.h"

#include <math.h>
#include <string.h>

#include "wx_paint.h"

#define OX (232.0f - 233.0f * WXM_K + 3.0f - WXM_BOX_X)
#define OY (62.0f - 84.0f * WXM_K - WXM_BOX_Y)
#define PTS 64
#ifndef WXM_PIXEL
#define WXM_PIXEL(im, y) ((void)0)
#endif

typedef struct {
    float x0, y0, x1, y1;
} rect_t;

static const rect_t SUN_R = {229, 83, 295, 149}, GLANCE_CLOUD_R = {175, 108, 288, 172};
static const rect_t DROP_R = {158, 91, 171, 111}, UMBRELLA_R = {176, 84, 292, 178};
static const rect_t BOLT_R = {218, 122, 264, 182}, STORM_CLOUD_R = {174, 84, 292, 140};
static const rect_t FLAKE_R = {184, 124, 200, 140}, SNOW_CLOUD_R = {186, 84, 279, 132};
static const rect_t LEAF_R = {154, 126, 177, 142};
static const rect_t HEAT_GLOW_R = {185, 84, 281, 178}, HEAT_SUN_R = {189, 86, 277, 174};
static const rect_t MOON_R = {168, 84, 256, 172}, STAR1_R = {262, 94, 282, 114}, STAR2_R = {286, 142, 302, 158};
static const rect_t BOW_R = {166, 104, 300, 174}, FEET_R = {158, 143, 308, 178};
static const float DROP_X[4] = {166, 296, 180, 306};
static const float FLAKE_X[5] = {192, 236, 214, 280, 258};
static const float STREAK_Y[4] = {103, 124, 148, 170}, STREAK_LEN[4] = {54, 40, 58, 36};
static const bool STREAK_CURL[4] = {true, false, true, false};

static const wx_cloud_t GLANCE_CLOUD = {false, 166, 3, {{198, 153, 19}, {233, 139, 25}, {266, 154, 17}}};
static const wx_cloud_t STORM_CLOUD = {false, 134, 3, {{198, 122, 18}, {233, 112, 24}, {268, 122, 18}}};
static const wx_cloud_t SNOW_CLOUD = {false, 128, 3, {{206, 118, 15}, {233, 108, 20}, {260, 118, 14}}};
static const wx_cloud_t FOOT_L = {false, 174, 3, {{172, 166, 10}, {186, 161, 13}, {200, 167, 9}}};
static const wx_cloud_t FOOT_R = {false, 174, 3, {{266, 167, 9}, {280, 161, 13}, {294, 166, 10}}};

int wxm_clouds(const wx_cloud_t *out[WXM_CLOUDS])
{
    static const wx_cloud_t *const ALL[WXM_CLOUDS] = {&GLANCE_CLOUD, &STORM_CLOUD, &SNOW_CLOUD, &FOOT_L, &FOOT_R};
    for (int i = 0; i < WXM_CLOUDS; i++) out[i] = ALL[i];
    return WXM_CLOUDS;
}

static wxm_layer_t layer_of(rect_t r)
{
    int x0 = (int)floorf(r.x0 * WXM_K + OX), y0 = (int)floorf(r.y0 * WXM_K + OY);
    int x1 = (int)ceilf(r.x1 * WXM_K + OX), y1 = (int)ceilf(r.y1 * WXM_K + OY);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > WXM_BOX_W) x1 = WXM_BOX_W;
    if (y1 > WXM_BOX_H) y1 = WXM_BOX_H;
    return (wxm_layer_t){x0, y0, x1 - x0, y1 - y0, 0};
}

static int add_layer(wxm_layout_t *l, rect_t r)
{
    wxm_layer_t a = layer_of(r);
    a.at = l->total;
    l->total += a.w * a.h * 3;
    l->layer[l->nl] = a;
    return l->nl++;
}

static void add_el(wxm_layout_t *l, int layer, float dx, float dy)
{
    const wxm_layer_t *a = &l->layer[layer];
    l->el[l->ne++] = (wxm_place_t){layer, a->x + (int)lroundf(dx * WXM_K), a->y + (int)lroundf(dy * WXM_K)};
}

static rect_t streak_r(int i)
{
    float y = STREAK_Y[i];
    return (rect_t){154, y - (STREAK_CURL[i] ? 17 : 3), 156 + STREAK_LEN[i] + (STREAK_CURL[i] ? 10 : 3), y + 3};
}

void wxm_layout(const wxm_moment_t *m, wxm_layout_t *l)
{
    memset(l, 0, sizeof *l);
    switch (m->kind) {
    case WXM_GLANCE:
    case WXM_FROST:
        if (m->sun) add_el(l, add_layer(l, SUN_R), 0, 0);
        add_el(l, add_layer(l, GLANCE_CLOUD_R), 0, 0);
        break;
    case WXM_RAIN: {
        int d = add_layer(l, DROP_R);
        for (int i = 0; i < 4; i++) add_el(l, d, DROP_X[i] - DROP_X[0], 0);
        for (int p = 0; p < 3; p++) add_el(l, add_layer(l, UMBRELLA_R), 0, 0);
        break;
    }
    case WXM_STORM:
        add_el(l, add_layer(l, BOLT_R), 0, 0);
        add_el(l, add_layer(l, STORM_CLOUD_R), 0, 0);
        add_el(l, add_layer(l, STORM_CLOUD_R), 0, 0);
        break;
    case WXM_SNOW: {
        int f = add_layer(l, FLAKE_R);
        for (int i = 0; i < 5; i++) add_el(l, f, FLAKE_X[i] - FLAKE_X[0], 0);
        add_el(l, add_layer(l, SNOW_CLOUD_R), 0, 0);
        break;
    }
    case WXM_WIND:
        for (int i = 0; i < 4; i++) add_el(l, add_layer(l, streak_r(i)), 0, 0);
        add_el(l, add_layer(l, LEAF_R), 0, 0);
        break;
    case WXM_HEAT:
        add_el(l, add_layer(l, HEAT_GLOW_R), 0, 0);
        add_el(l, add_layer(l, HEAT_SUN_R), 0, 0);
        break;
    case WXM_NIGHT:
        if (m->moon) add_el(l, add_layer(l, MOON_R), 0, 0);
        add_el(l, add_layer(l, STAR1_R), 0, 0);
        add_el(l, add_layer(l, STAR2_R), 0, 0);
        break;
    case WXM_AFTERRAIN:
        add_el(l, add_layer(l, BOW_R), 0, 0);
        add_el(l, add_layer(l, FEET_R), 0, 0);
        break;
    default: break;
    }
}

typedef struct {
    uint16_t *px;
    uint8_t *a;
    int w, h, x0, y0;
    void (*yield)(void);
} img_t;

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline float cov(float d) { return clamp01(0.5f - d * WXM_K); }

static inline uint16_t mix(uint16_t d, uint16_t s, float a)
{
    int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
    int r = dr + (int)lroundf((float)(sr - dr) * a), g = dg + (int)lroundf((float)(sg - dg) * a),
        b = db + (int)lroundf((float)(sb - db) * a);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void put(img_t *im, int i, uint16_t c, float a)
{
    if (!(a > 0.002f)) return;
    if (a > 1) a = 1;
    float da = im->a[i] / 255.0f;
    float oa = a + da * (1 - a);
    im->px[i] = da <= 0 ? c : mix(im->px[i], c, a / oa);
    im->a[i] = (uint8_t)lroundf(oa * 255.0f);
}

static uint32_t lerp_rgb(uint32_t a, uint32_t b, float f)
{
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
        out |= (uint32_t)lroundf(ca + (cb - ca) * clamp01(f)) << s;
    }
    return out;
}

typedef float (*sd_fn)(const void *ctx, float u, float v);
typedef uint32_t (*shade_fn)(const void *ctx, float u, float v, float *alpha);

static void design_at(const img_t *im, int x, int y, float *u, float *v)
{
    WXM_PIXEL(im, y);
    *u = ((float)(im->x0 + x) + 0.5f - OX) / WXM_K;
    *v = ((float)(im->y0 + y) + 0.5f - OY) / WXM_K;
}

static void row(const img_t *im)
{
    if (im->yield) im->yield();
}

static rect_t grow(rect_t r, float by) { return (rect_t){r.x0 - by, r.y0 - by, r.x1 + by, r.y1 + by}; }

static rect_t ring_box(float cx, float cy, float r) { return (rect_t){cx - r, cy - r, cx + r, cy + r}; }

static void span(const img_t *im, rect_t r, int *x0, int *x1, int *y0, int *y1)
{
    *x0 = (int)floorf(r.x0 * WXM_K + OX) - im->x0 - 1;
    *y0 = (int)floorf(r.y0 * WXM_K + OY) - im->y0 - 1;
    *x1 = (int)ceilf(r.x1 * WXM_K + OX) - im->x0 + 1;
    *y1 = (int)ceilf(r.y1 * WXM_K + OY) - im->y0 + 1;
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > im->w) *x1 = im->w;
    if (*y1 > im->h) *y1 = im->h;
}

static void fill(img_t *im, sd_fn sd, const void *ctx, rect_t ink, uint32_t rgb, float line_hw, uint32_t line_rgb)
{
    const uint16_t c = wx_rgb565(rgb), lc = wx_rgb565(line_rgb);
    int x0, x1, y0, y1;
    span(im, grow(ink, line_hw + 1), &x0, &x1, &y0, &y1);
    for (int y = 0; y < im->h; y++) {
        row(im);
        if (y < y0 || y >= y1) continue;
        for (int x = x0; x < x1; x++) {
            float u, v;
            design_at(im, x, y, &u, &v);
            float d = sd(ctx, u, v);
            int i = y * im->w + x;
            put(im, i, c, cov(d));
            if (line_hw > 0) put(im, i, lc, cov(fabsf(d) - line_hw));
        }
    }
}

static void glow(img_t *im, float cx, float cy, float r, float from, float alpha, uint32_t rgb)
{
    const uint16_t c = wx_rgb565(rgb);
    int x0, x1, y0, y1;
    span(im, ring_box(cx, cy, r + 1), &x0, &x1, &y0, &y1);
    for (int y = 0; y < im->h; y++) {
        row(im);
        if (y < y0 || y >= y1) continue;
        for (int x = x0; x < x1; x++) {
            float u, v;
            design_at(im, x, y, &u, &v);
            float g = hypotf(u - cx, v - cy) / r;
            put(im, y * im->w + x, c, g <= from ? alpha : g >= 1 ? 0 : alpha * (1 - (g - from) / (1 - from)));
        }
    }
}

typedef struct {
    int n;
    bool closed;
    float hw;
    float p[PTS][2];
} path_t;

static rect_t path_box(const path_t *q)
{
    rect_t r = {q->p[0][0], q->p[0][1], q->p[0][0], q->p[0][1]};
    for (int i = 1; i < q->n; i++) {
        r.x0 = fminf(r.x0, q->p[i][0]);
        r.y0 = fminf(r.y0, q->p[i][1]);
        r.x1 = fmaxf(r.x1, q->p[i][0]);
        r.y1 = fmaxf(r.y1, q->p[i][1]);
    }
    return grow(r, q->hw);
}

static void fill_path(img_t *im, const path_t *q, uint32_t rgb)
{
    const uint16_t c = wx_rgb565(rgb);
    const float far = q->hw + 1, far2 = far * far;
    int x0, x1, y0, y1;
    span(im, grow(path_box(q), 1), &x0, &x1, &y0, &y1);
    for (int y = 0; y < im->h; y++) {
        row(im);
        if (y < y0 || y >= y1) continue;
        float u, v;
        design_at(im, 0, y, &u, &v);
        uint8_t near[PTS];
        float cross[PTS];
        int nn = 0, nc = 0;
        for (int i = 0, j = q->n - 1; i < q->n; j = i++) {
            if (q->closed && (q->p[i][1] > v) != (q->p[j][1] > v))
                cross[nc++] = (q->p[j][0] - q->p[i][0]) * (v - q->p[i][1]) / (q->p[j][1] - q->p[i][1]) + q->p[i][0];
            if ((i || q->closed) && fminf(q->p[i][1], q->p[j][1]) - far <= v && v <= fmaxf(q->p[i][1], q->p[j][1]) + far)
                near[nn++] = (uint8_t)i;
        }
        for (int x = x0; x < x1; x++) {
            design_at(im, x, y, &u, &v);
            float best = far2, bx = far, by = 0;
            for (int k = 0; k < nn; k++) {
                const int i = near[k], j = i ? i - 1 : q->n - 1;
                const float ax = q->p[j][0], ay = q->p[j][1], cx = q->p[i][0], cy = q->p[i][1];
                if (fminf(ax, cx) - u > far || u - fmaxf(ax, cx) > far) continue;
                float dx = cx - ax, dy = cy - ay, l2 = dx * dx + dy * dy;
                float t = l2 > 0 ? ((u - ax) * dx + (v - ay) * dy) / l2 : 0;
                t = clamp01(t);
                float ex = u - ax - t * dx, ey = v - ay - t * dy, d2 = ex * ex + ey * ey;
                if (d2 < best) {
                    best = d2;
                    bx = ex;
                    by = ey;
                }
            }
            bool in = false;
            for (int k = 0; k < nc; k++)
                if (u < cross[k]) in = !in;
            float d = best < far2 ? hypotf(bx, by) : far;
            put(im, y * im->w + x, c, cov((in ? -d : d) - q->hw));
        }
    }
}

static void pt(path_t *q, float x, float y)
{
    if (q->n < PTS) {
        q->p[q->n][0] = x;
        q->p[q->n][1] = y;
        q->n++;
    }
}

static void quad(path_t *q, float cx, float cy, float x, float y, int steps)
{
    float x0 = q->p[q->n - 1][0], y0 = q->p[q->n - 1][1];
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps, s = 1 - t;
        pt(q, s * s * x0 + 2 * s * t * cx + t * t * x, s * s * y0 + 2 * s * t * cy + t * t * y);
    }
}

static void cubic(path_t *q, float ax, float ay, float bx, float by, float x, float y, int steps)
{
    float x0 = q->p[q->n - 1][0], y0 = q->p[q->n - 1][1];
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps, s = 1 - t;
        float w0 = s * s * s, w1 = 3 * s * s * t, w2 = 3 * s * t * t, w3 = t * t * t;
        pt(q, w0 * x0 + w1 * ax + w2 * bx + w3 * x, w0 * y0 + w1 * ay + w2 * by + w3 * y);
    }
}

static void arc(path_t *q, float cx, float cy, float rx, float ry, float a0, float a1, int steps)
{
    for (int i = 1; i <= steps; i++) {
        float c, s;
        wx_deg_cs(a0 + (a1 - a0) * (float)i / steps, &c, &s);
        pt(q, cx + rx * c, cy + ry * s);
    }
}

static void line(img_t *im, float ax, float ay, float bx, float by, float w, uint32_t rgb)
{
    path_t q = {.n = 0, .closed = false, .hw = w / 2};
    pt(&q, ax, ay);
    pt(&q, bx, by);
    fill_path(im, &q, rgb);
}

static float cloud_sd(const void *ctx, float u, float v) { return wx_cloud_sd(ctx, u, v); }

enum { TONE_WHITE, TONE_LIGHT, TONE_DARK };

static __attribute__((warn_unused_result)) int cloud(img_t *im, const wx_cloud_t *c, int tone)
{
    static const uint32_t TONES[3][2] = {{0xE6ECF2, 0xC4CCD4}, {0x8E96A0, 0xA8B0BA}, {0x555D68, 0x6A7480}};
    wx_cloud_shape_t sh;
    bool ok = wx_cloud_shape(c, &sh);
    rect_t ink = {c->c[0].x - c->c[0].r, c->c[0].y - c->c[0].r, c->c[0].x + c->c[0].r, c->base};
    for (int k = 1; k < c->n; k++) {
        ink.x0 = fminf(ink.x0, c->c[k].x - c->c[k].r);
        ink.y0 = fminf(ink.y0, c->c[k].y - c->c[k].r);
        ink.x1 = fmaxf(ink.x1, c->c[k].x + c->c[k].r);
    }
    fill(im, cloud_sd, &sh, ink, TONES[tone][0], 2.25f, TONES[tone][1]);
    return !ok;
}

typedef struct {
    float cx, cy, r;
} disc_t;

static float disc_sd(const void *ctx, float u, float v)
{
    const disc_t *d = ctx;
    return hypotf(u - d->cx, v - d->cy) - d->r;
}

static void sun_disc(img_t *im, float cx, float cy, float r)
{
    const float gx = cx - r * 0.3f, gy = cy - r * 0.3f, gr = r * 1.3f;
    const uint16_t rim = wx_rgb565(0xFFB547);
    int x0, x1, y0, y1;
    span(im, ring_box(cx, cy, r + 2), &x0, &x1, &y0, &y1);
    for (int y = 0; y < im->h; y++) {
        row(im);
        if (y < y0 || y >= y1) continue;
        for (int x = x0; x < x1; x++) {
            float u, v;
            design_at(im, x, y, &u, &v);
            float d = hypotf(u - cx, v - cy) - r, f = hypotf(u - gx, v - gy) / gr;
            uint32_t c = f <= 0.6f ? lerp_rgb(0xFFE07A, 0xFFB547, f / 0.6f) : lerp_rgb(0xFFB547, 0xF08A3A, (f - 0.6f) / 0.4f);
            int i = y * im->w + x;
            put(im, i, wx_rgb565(c), cov(d));
            put(im, i, rim, cov(fabsf(d) - 0.75f));
        }
    }
}

static void paint_glance_sun(img_t *im)
{
    glow(im, 262, 116, 30, 0.7f, 0.28f, 0xFFB547);
    sun_disc(im, 262, 116, 21);
}

static void handle(path_t *q)
{
    *q = (path_t){.n = 0, .closed = false, .hw = 2.25f};
    pt(q, 233, 132);
    pt(q, 233, 166);
    arc(q, 226, 166, 7, 7, 0, 180, 12);
}

static void umbrella(img_t *im, int pose)
{
    const uint32_t O = 0xFF8A3D, D = 0xD9692A, HC = 0xC9D2DC;
    path_t q;
    handle(&q);
    fill_path(im, &q, HC);
    if (pose > 0) line(im, 233, 97, 233, 89, 4, O);
    q = (path_t){.n = 0, .closed = true, .hw = 0};
    if (pose == 0) {
        pt(&q, 233, 92);
        quad(&q, 244, 120, 237, 150, 10);
        pt(&q, 229, 150);
        quad(&q, 222, 120, 233, 92, 10);
        fill_path(im, &q, O);
        q = (path_t){.n = 0, .closed = false, .hw = 1};
        pt(&q, 233, 96);
        quad(&q, 236, 122, 233, 148, 10);
        fill_path(im, &q, D);
    } else if (pose == 1) {
        pt(&q, 205, 142);
        quad(&q, 211, 104, 233, 96, 10);
        quad(&q, 255, 104, 261, 142, 10);
        quad(&q, 247, 136, 233, 142, 6);
        quad(&q, 219, 136, 205, 142, 6);
        fill_path(im, &q, O);
        q = (path_t){.n = 0, .closed = true, .hw = 0};
        pt(&q, 233, 96);
        quad(&q, 226, 118, 225, 139, 8);
        quad(&q, 229, 138, 233, 142, 4);
        quad(&q, 237, 138, 241, 139, 4);
        quad(&q, 240, 118, 233, 96, 8);
        fill_path(im, &q, D);
    } else {
        pt(&q, 181, 132);
        arc(&q, 233, 132, 52, 36, 180, 360, 24);
        for (int i = 3; i >= 0; i--) arc(&q, 194 + 26 * (float)i, 132, 13, 7, 0, -180, 6);
        fill_path(im, &q, O);
        q = (path_t){.n = 0, .closed = true, .hw = 0};
        pt(&q, 233, 96);
        quad(&q, 213, 104, 207, 132, 10);
        arc(&q, 220, 132, 13, 7, 180, 360, 6);
        arc(&q, 246, 132, 13, 7, 180, 360, 6);
        quad(&q, 253, 104, 233, 96, 10);
        fill_path(im, &q, D);
    }
}

static void bolt(img_t *im)
{
    static const float P[7][2] = {{236, 126}, {258, 126}, {246, 148}, {260, 148}, {228, 178}, {236, 156}, {222, 156}};
    path_t q = {.n = 0, .closed = true, .hw = 1};
    for (int i = 0; i < 7; i++) pt(&q, P[i][0], P[i][1]);
    fill_path(im, &q, WXM_STORM_RGB);
}

static void flake(img_t *im, float x, float y)
{
    for (int a = 0; a < 180; a += 60) {
        float c, s;
        wx_deg_cs((float)a, &c, &s);
        line(im, x - 6 * c, y - 6 * s, x + 6 * c, y + 6 * s, 2.5f, 0xD0DCE8);
    }
}

static void streak(img_t *im, int i)
{
    float y = STREAK_Y[i], x1 = 156 + STREAK_LEN[i];
    path_t q = {.n = 0, .closed = false, .hw = 2};
    pt(&q, 156, y);
    pt(&q, x1, y);
    if (STREAK_CURL[i]) arc(&q, x1, y - 7, 7, 7, 90, -180, 24);
    fill_path(im, &q, 0xD0DCE8);
}

static void leaf(img_t *im)
{
    path_t q = {.n = 0, .closed = true, .hw = 0};
    pt(&q, 0, 0);
    cubic(&q, 4, -5, 12, -5, 16, 0, 12);
    cubic(&q, 12, 5, 4, 5, 0, 0, 12);
    float c, s;
    wx_deg_cs(-20, &c, &s);
    for (int i = 0; i < q.n; i++) {
        float x = q.p[i][0], y = q.p[i][1];
        q.p[i][0] = 158 + x * c - y * s;
        q.p[i][1] = 136 + x * s + y * c;
    }
    fill_path(im, &q, 0x8ACF7A);
}

static void heat_sun(img_t *im)
{
    for (int a = 0; a < 360; a += 45) {
        float c, s;
        wx_deg_cs((float)a, &c, &s);
        line(im, 233 + 31 * c, 130 + 31 * s, 233 + 40 * c, 130 + 40 * s, 5, 0xFFB547);
    }
    sun_disc(im, 233, 130, 24);
}

static void moon(img_t *im, int phase, int illum_pct)
{
    const float cx = 212, cy = 128, r = 30;
    glow(im, cx, cy, 42, 0.6f, 0.22f, 0xAFC4E0);
    disc_t body = {cx, cy, r};
    fill(im, disc_sd, &body, ring_box(cx, cy, r), 0x000000, 0, 0);
    const float il = clamp01((float)illum_pct / 100.0f), sgn = phase < 50 ? 1.0f : -1.0f, rx = fabsf(1 - 2 * il) * r;
    int x0, x1, y0, y1;
    span(im, ring_box(cx, cy, r + 1), &x0, &x1, &y0, &y1);
    for (int y = 0; y < im->h; y++) {
        row(im);
        if (y < y0 || y >= y1) continue;
        for (int x = x0; x < x1; x++) {
            float u, v;
            design_at(im, x, y, &u, &v);
            float dx = u - cx, dy = v - cy, d = hypotf(dx, dy);
            float in_body = cov(d - r), side = cov(-sgn * dx), de;
            if (rx < 0.5f) {
                de = fabsf(dx) - rx;
            } else {
                float f = dx * dx / (rx * rx) + dy * dy / (r * r) - 1;
                float gx = dx / (rx * rx), gy = dy / (r * r), gl = 2 * sqrtf(gx * gx + gy * gy);
                de = gl > 1e-6f ? f / gl : -rx;
            }
            float in_e = cov(de);
            float lit = fminf(il < 0.5f ? fminf(side, 1 - in_e) : fmaxf(side, in_e), in_body);
            put(im, y * im->w + x, wx_rgb565(lerp_rgb(0xE8ECF2, 0xC9D2DC, hypotf(u - cx, v - cy + 6) / (r * 1.2f))), lit);
        }
    }
}

static void star(img_t *im, float x, float y, float s)
{
    path_t q = {.n = 0, .closed = true, .hw = 0};
    pt(&q, x, y - s);
    quad(&q, x, y, x + s, y, 8);
    quad(&q, x, y, x, y + s, 8);
    quad(&q, x, y, x - s, y, 8);
    quad(&q, x, y, x, y - s, 8);
    fill_path(im, &q, 0xE8ECF2);
}

typedef struct {
    float r;
} bow_t;

static float bow_sd(const void *ctx, float u, float v)
{
    const bow_t *b = ctx;
    return fmaxf(fabsf(hypotf(u - 233, v - 172) - b->r) - 3.3f, v - 172);
}

static void rainbow(img_t *im)
{
    static const uint32_t BANDS[5] = {0xE86A5A, 0xF0A040, 0xF0D040, 0x5CCB8A, 0x5C9CE0};
    for (int i = 0; i < 5; i++) {
        bow_t b = {63.0f - 6.0f * (float)i};
        fill(im, bow_sd, &b, (rect_t){233 - b.r - 3.3f, 172 - b.r - 3.3f, 233 + b.r + 3.3f, 172}, BANDS[i], 0, 0);
    }
}

int wxm_paint(const wxm_moment_t *m, const wxm_layout_t *l, uint8_t *arena, void (*yield)(void))
{
    img_t im[WXM_LAYERS];
    int refused = 0;
    for (int i = 0; i < l->nl; i++) {
        const wxm_layer_t *a = &l->layer[i];
        im[i] = (img_t){(uint16_t *)(arena + a->at), arena + a->at + a->w * a->h * 2, a->w, a->h, a->x, a->y, yield};
        memset(arena + a->at, 0, (size_t)(a->w * a->h * 3));
    }
    if (yield) yield();
    switch (m->kind) {
    case WXM_GLANCE:
    case WXM_FROST:
        if (m->sun) paint_glance_sun(&im[0]);
        refused += cloud(&im[m->sun ? 1 : 0], &GLANCE_CLOUD, TONE_WHITE);
        break;
    case WXM_RAIN:
        line(&im[0], DROP_X[0], 96, DROP_X[0] - 3, 106, 4, WXM_BLUE);
        for (int p = 0; p < 3; p++) umbrella(&im[1 + p], p);
        break;
    case WXM_STORM:
        bolt(&im[0]);
        refused += cloud(&im[1], &STORM_CLOUD, TONE_LIGHT);
        refused += cloud(&im[2], &STORM_CLOUD, TONE_DARK);
        break;
    case WXM_SNOW:
        flake(&im[0], FLAKE_X[0], 132);
        refused += cloud(&im[1], &SNOW_CLOUD, TONE_WHITE);
        break;
    case WXM_WIND:
        for (int i = 0; i < 4; i++) streak(&im[i], i);
        leaf(&im[4]);
        break;
    case WXM_HEAT:
        glow(&im[0], 233, 130, 46, 0.5f, 0.45f, 0xFF8A3D);
        heat_sun(&im[1]);
        break;
    case WXM_NIGHT: {
        int s = 0;
        if (m->moon) moon(&im[s++], m->moon_phase, m->moon_illum);
        star(&im[s], 272, 104, 8);
        star(&im[s + 1], 294, 150, 6);
        break;
    }
    case WXM_AFTERRAIN:
        rainbow(&im[0]);
        refused += cloud(&im[1], &FOOT_L, TONE_WHITE);
        refused += cloud(&im[1], &FOOT_R, TONE_WHITE);
        break;
    default: break;
    }
    return refused;
}
