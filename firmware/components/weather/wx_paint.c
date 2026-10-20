#include "wx_paint.h"

#include <math.h>
#include <string.h>

#define RIM_R 195.0f
#define HERO_FADE (0.82f * RIM_R)
#define SUN_C 116.3f
#define SUN_CORE 52.0f
#define DEG ((float)M_PI / 180.0f)

uint16_t wx_rgb565(uint32_t rgb)
{
    return (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xFF) >> 3));
}

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static inline float cov(float d) { return clamp01(0.5f - d); }

static void row(const wx_img_t *im)
{
    if (im->yield) im->yield();
}

static inline uint16_t mix(uint16_t d, uint16_t s, float a)
{
    int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
    int r = dr + (int)lroundf((float)(sr - dr) * a), g = dg + (int)lroundf((float)(sg - dg) * a),
        b = db + (int)lroundf((float)(sb - db) * a);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void put(wx_img_t *im, int i, uint16_t c, float a)
{
    if (!(a > 0.002f)) return;
    if (a > 1) a = 1;
    if (!im->a) {
        im->px[i] = a >= 0.998f ? c : mix(im->px[i], c, a);
        return;
    }
    float da = im->a[i] / 255.0f;
    float oa = a + da * (1 - a);
    im->px[i] = da <= 0 ? c : mix(im->px[i], c, a / oa);
    im->a[i] = (uint8_t)lroundf(oa * 255.0f);
}

static void scale_alpha(wx_img_t *im, float k, float fade_from)
{
    for (int y = 0; y < im->h; y++) {
        row(im);
        float fy = (float)(im->y0 + y) + 0.5f - WX_FACE_C;
        for (int x = 0; x < im->w; x++) {
            int i = y * im->w + x;
            if (!im->a[i]) continue;
            float fx = (float)(im->x0 + x) + 0.5f - WX_FACE_C;
            float d = sqrtf(fx * fx + fy * fy);
            float r = d <= fade_from ? 1 : d >= RIM_R ? 0 : (RIM_R - d) / (RIM_R - fade_from);
            im->a[i] = (uint8_t)lroundf(im->a[i] * k * r);
        }
    }
}

static float rim(float fx, float fy, float from)
{
    float dx = fx - WX_FACE_C, dy = fy - WX_FACE_C;
    float d = sqrtf(dx * dx + dy * dy);
    return d <= from ? 1 : d >= RIM_R ? 0 : (RIM_R - d) / (RIM_R - from);
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

void wx_paint_clear(wx_img_t *im)
{
    memset(im->px, 0, (size_t)im->w * im->h * 2);
    if (im->a) memset(im->a, 0, (size_t)im->w * im->h);
}

static void clip(const wx_img_t *im, float fx0, float fy0, float fx1, float fy1, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = (int)floorf(fx0) - im->x0;
    *y0 = (int)floorf(fy0) - im->y0;
    *x1 = (int)ceilf(fx1) - im->x0;
    *y1 = (int)ceilf(fy1) - im->y0;
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > im->w) *x1 = im->w;
    if (*y1 > im->h) *y1 = im->h;
}

#define RING_STEPS (WX_RING_LUT - 1)

static void ring_raster(wx_ring_t *r, const uint16_t lut[WX_RING_LUT]);

void wx_paint_ring(wx_ring_t *r, int lo, int hi, bool grey, uint16_t lut[WX_RING_LUT])
{
    for (int i = 0; i <= RING_STEPS; i++)
        lut[i] = wx_rgb565(grey ? 0x262626 : wx_temp_rgb((float)lo + (float)(hi - lo) * i / RING_STEPS));
    ring_raster(r, lut);
}

void wx_paint_ring_rain(wx_ring_t *r, int intensity, bool grey, uint16_t lut[WX_RING_LUT])
{
    uint32_t to = wx_rain_rgb(intensity);
    for (int i = 0; i <= RING_STEPS; i++)
        lut[i] = wx_rgb565(grey ? 0x262626 : lerp_rgb(0x2E3A48, to, (float)i / RING_STEPS));
    ring_raster(r, lut);
}

typedef float (*ring_px_fn)(const void *ctx, float fx, float fy, float r2, uint16_t *col);

static void annulus(wx_ring_t *r, ring_px_fn fn, const void *ctx)
{
    const float r_in = WX_RING_R - WX_RING_HW - 1, r_out = WX_RING_R + WX_RING_HW + 1;
    wx_img_t face = {.w = WX_FACE, .h = WX_FACE, .yield = r->yield};
    int x0, y0, x1, y1;
    clip(&face, WX_FACE_C - r_out, WX_FACE_C - r_out, WX_FACE_C + r_out, WX_FACE_C + r_out, &x0, &y0, &x1, &y1);
    r->nrun = r->npx = 0;
    for (int y = y0; y < y1; y++) {
        row(&face);
        float fy = (float)y + 0.5f, dy = fy - WX_FACE_C;
        bool in = false;
        for (int x = x0; x < x1; x++) {
            float fx = (float)x + 0.5f, dx = fx - WX_FACE_C;
            float r2 = dx * dx + dy * dy;
            if (r2 < r_in * r_in || r2 > r_out * r_out) {
                in = false;
                continue;
            }
            if (!in) {
                if (r->nrun == WX_RING_RUNS) return;
                r->run[r->nrun++] = (wx_run_t){(int16_t)y, (int16_t)x, 0};
                in = true;
            }
            if (r->npx == WX_RING_PX) return;
            uint16_t col = 0, px = 0;
            float a = fn(ctx, fx, fy, r2, &col);
            wx_img_t one = {.px = &px, .w = 1, .h = 1};
            put(&one, 0, col, a);
            r->px[r->npx++] = px;
            r->run[r->nrun - 1].n++;
        }
    }
}

typedef struct {
    const uint16_t *lut;
    float sx, sy, ex, ey;
} gauge_t;

static float gauge_px(const void *ctx, float fx, float fy, float r2, uint16_t *col)
{
    const gauge_t *g = ctx;
    float th = atan2f(fx - WX_FACE_C, -(fy - WX_FACE_C)) / DEG;
    float s = th - 225.0f;
    while (s < 0) s += 360.0f;
    float d;
    int li;
    if (s <= 270.0f) {
        d = fabsf(sqrtf(r2) - WX_RING_R) - WX_RING_HW;
        li = (int)lroundf(s * RING_STEPS / 270.0f);
    } else {
        float ds = hypotf(fx - g->sx, fy - g->sy), de = hypotf(fx - g->ex, fy - g->ey);
        d = (ds < de ? ds : de) - WX_RING_HW;
        li = ds < de ? 0 : RING_STEPS;
    }
    *col = g->lut[li];
    return cov(d);
}

static void ring_raster(wx_ring_t *r, const uint16_t lut[WX_RING_LUT])
{
    gauge_t g = {.lut = lut};
    wx_gauge_xy(0, &g.sx, &g.sy);
    wx_gauge_xy(1, &g.ex, &g.ey);
    annulus(r, gauge_px, &g);
}

#define CELL_GREY 0x262626

static float dial_px(const void *ctx, float fx, float fy, float r2, uint16_t *col)
{
    const wx_cast_t *c = ctx;
    float rr = sqrtf(r2), th = atan2f(fx - WX_FACE_C, -(fy - WX_FACE_C)) / DEG;
    if (th < 0) th += 360.0f;
    int k = (int)(th / 30.0f);
    if (k > 11) k = 11;
    float lo = 30.0f * (float)k + 1.5f, hi = lo + 27.0f, px = DEG * rr;
    float cell = clamp01(fminf(th - lo, hi - th) * px + 0.5f);
    float lit = 0;
    int in = 0;
    for (int i = 0; i < c->narc; i++) {
        float a0 = (float)c->arc[i].a * 0.5f, a1 = a0 + (float)c->arc[i].n * 0.5f;
        for (int w = 0; w <= 1; w++) {
            float t = th + 360.0f * (float)w, s0 = fmaxf(a0, lo + 360.0f * (float)w), s1 = fminf(a1, hi + 360.0f * (float)w);
            if (s1 - s0 <= 0.2f) continue;
            float l = clamp01(fminf(t - s0, s1 - t) * px + 0.5f);
            if (l > lit) {
                lit = l;
                in = c->arc[i].intensity;
            }
        }
    }
    uint16_t grey = wx_rgb565(CELL_GREY);
    *col = lit > 0 ? mix(grey, wx_rgb565(wx_rain_rgb(in)), lit) : grey;
    return cov(fabsf(rr - WX_RING_R) - WX_RING_HW) * cell;
}

void wx_paint_ring_cast(wx_ring_t *r, const wx_cast_t *c) { annulus(r, dial_px, c); }

void wx_ring_blit(const wx_ring_t *r, uint16_t *bg)
{
    const uint16_t *p = r->px;
    for (int i = 0; i < r->nrun; i++) {
        const wx_run_t *u = &r->run[i];
        memcpy(bg + (size_t)u->y * WX_FACE + u->x, p, (size_t)u->n * sizeof *p);
        p += u->n;
    }
}

static const float KEEP_OUT[5][5] = {
    {190, 88, 286, 202, 18}, {76, 208, 390, 339, 24}, {84, 356, 166, 410, 12}, {299, 356, 382, 410, 12}, {180, 422, 286, 452, 10},
};

static float rbox(float px, float py, const float *b)
{
    float cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2;
    float qx = fabsf(px - cx) - ((b[2] - b[0]) / 2 - b[4]), qy = fabsf(py - cy) - ((b[3] - b[1]) / 2 - b[4]);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - b[4];
}

#define RAYS 22

void wx_paint_rays(wx_img_t *im)
{
    const uint16_t col = wx_rgb565(0xFFB547);
    const float start = SUN_CORE + 14, step = 360.0f / RAYS;
    const float out_dir = atan2f(SUN_C - WX_FACE_C, SUN_C - WX_FACE_C) / DEG + 360.0f;
    float ang[RAYS], len[RAYS];
    for (int k = 0; k < RAYS; k++) {
        ang[k] = k * step + 4;
        float off = fmodf(ang[k] - out_dir + 180.0f + 720.0f, 360.0f) - 180.0f;
        len[k] = fabsf(off) < 50 ? SUN_CORE + 40 : 330;
    }
    int x0, y0, x1, y1;
    clip(im, WX_FACE_C - RIM_R, WX_FACE_C - RIM_R, WX_FACE_C + RIM_R, WX_FACE_C + RIM_R, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        row(im);
        float fy = (float)(im->y0 + y) + 0.5f;
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f;
            float m = rim(fx, fy, HERO_FADE);
            if (m <= 0) continue;
            float vx = fx - SUN_C, vy = fy - SUN_C;
            float rho = sqrtf(vx * vx + vy * vy);
            if (rho < start - 3 || rho > 300) continue;
            float phi = atan2f(vy, vx) / DEG;
            int k = (int)floorf((phi - 4) / step + 0.5f);
            k = ((k % RAYS) + RAYS) % RAYS;
            float da = (phi - ang[k]) * DEG;
            float along = rho * cosf(da), perp = rho * fabsf(sinf(da));
            float d = along < start ? hypotf(along - start, perp) : along > len[k] ? hypotf(along - len[k], perp) : perp;
            float c = cov(d - ((k % 2) ? 1.5f : 1.75f));
            if (c <= 0) continue;
            float g = rho / 300.0f;
            float ga = g <= 0.2f ? 0.55f : 0.55f * (1 - (g - 0.2f) / 0.8f);
            float block = 0;
            for (int b = 0; b < 5; b++) {
                float kb = clamp01(0.5f - rbox(fx, fy, KEEP_OUT[b]) / 24.0f);
                if (kb > block) block = kb;
            }
            put(im, y * im->w + x, col, c * ga * m * (1 - block));
        }
    }
}

static const float STARS[10][3] = {
    {214, 70, 1.4f}, {262, 62, 1.2f}, {305, 95, 1.8f}, {352, 126, 1.3f}, {392, 186, 1.5f},
    {320, 168, 1.1f}, {404, 254, 1.6f}, {396, 300, 1.2f}, {62, 262, 1.4f}, {74, 312, 1.1f},
};

static void disc(wx_img_t *im, float cx, float cy, float r, uint16_t c, float alpha, float fade_from)
{
    int x0, y0, x1, y1;
    clip(im, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        row(im);
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            float a = cov(hypotf(fx - cx, fy - cy) - r) * alpha;
            if (fade_from > 0) a *= rim(fx, fy, fade_from);
            put(im, y * im->w + x, c, a);
        }
    }
}

void wx_paint_face_dots(wx_img_t *im, int face, int n)
{
    if (n < 2) return;
    for (int k = 0; k < n; k++) {
        float x, y;
        wx_face_dot_xy(k, n, &x, &y);
        disc(im, x, y, WX_DOT_R, wx_rgb565(k == face ? WX_DOT_ON : WX_DOT_OFF), 1, 0);
    }
}

void wx_paint_temp_dots(wx_img_t *im, const wx_temp_t *t)
{
    if (t->dots_n > 0) wx_paint_face_dots(im, t->dots_k, t->dots_n);
    else wx_paint_face_dots(im, t->face, WXF_N);
}

void wx_paint_stars(wx_img_t *im)
{
    for (int i = 0; i < 10; i++) disc(im, STARS[i][0], STARS[i][1], STARS[i][2], wx_rgb565(0xDDE6F0), 0.35f, HERO_FADE);
}

static float thermo_d(float x, float y)
{
    float dy = WX_THERMO_DY;
    float top = hypotf(x - 233, y - (127 + dy)) - 11;
    float bulb = hypotf(x - 233, y - (183 + dy)) - 18;
    float qx = fabsf(x - 233) - 11, qy = fabsf(y - (147.875f + dy)) - 20.875f;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
    float rect = sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0);
    float d = top < bulb ? top : bulb;
    return rect < d ? rect : d;
}

void wx_paint_thermo(wx_img_t *im)
{
    int x0, y0, x1, y1;
    clip(im, 212, 110 + WX_THERMO_DY, 254, 206 + WX_THERMO_DY, &x0, &y0, &x1, &y1);
    const uint16_t stroke = wx_rgb565(0x9AA4B0);
    for (int y = y0; y < y1; y++) {
        row(im);
        for (int x = x0; x < x1; x++) {
            float d = thermo_d((float)(im->x0 + x) + 0.5f, (float)(im->y0 + y) + 0.5f);
            put(im, y * im->w + x, 0, cov(d));
            put(im, y * im->w + x, stroke, cov(fabsf(d) - 2.25f));
        }
    }
}

static void sun(wx_img_t *im)
{
    const uint16_t halo = wx_rgb565(0xFFB547);
    for (int y = 0; y < im->h; y++) {
        row(im);
        for (int x = 0; x < im->w; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            float d = hypotf(fx - SUN_C, fy - SUN_C);
            int i = y * im->w + x;
            float g = d / 80.0f;
            put(im, i, halo, g <= 0.65f ? 0.22f : g >= 1 ? 0 : 0.22f * (1 - (g - 0.65f) / 0.35f));
            float f = d / SUN_CORE;
            uint32_t c = f <= 0.6f ? lerp_rgb(0xFFE07A, 0xFFB547, f / 0.6f) : lerp_rgb(0xFFB547, 0xF08A3A, (f - 0.6f) / 0.4f);
            put(im, i, wx_rgb565(c), cov(d - SUN_CORE));
            put(im, i, halo, cov(fabsf(d - SUN_CORE) - 0.75f));
        }
    }
    scale_alpha(im, 0.88f, HERO_FADE);
}

static void moon(wx_img_t *im, float cx, float cy, float r, float glow, int phase, int illum_pct)
{
    const float gr = roundf(r * 1.6f), il = illum_pct / 100.0f, sgn = phase < 50 ? 1.0f : -1.0f;
    const float rx = fabsf(1 - 2 * il) * r;
    const float gcx = cx - sgn * r * 0.35f, gcy = cy - r * 0.2f, grr = r * 1.2f;
    const uint16_t gc = wx_rgb565(0xAFC4E0);
    for (int y = 0; y < im->h; y++) {
        row(im);
        for (int x = 0; x < im->w; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            float dx = fx - cx, dy = fy - cy, d = sqrtf(dx * dx + dy * dy);
            int i = y * im->w + x;
            float g = d / gr;
            put(im, i, gc, g <= 0.6f ? glow : g >= 1 ? 0 : glow * (1 - (g - 0.6f) / 0.4f));
            float body = cov(d - r);
            put(im, i, 0, body);
            float side = cov(-sgn * dx);
            float de;
            if (rx < 0.5f) {
                de = fabsf(dx) - rx;
            } else {
                float f = dx * dx / (rx * rx) + dy * dy / (r * r) - 1;
                float gx = dx / (rx * rx), gy = dy / (r * r);
                float gl = 2 * sqrtf(gx * gx + gy * gy);
                de = gl > 1e-6f ? f / gl : -rx;
            }
            float in_e = cov(de);
            float lit = il < 0.5f ? fminf(side, 1 - in_e) : fmaxf(side, in_e);
            lit = fminf(lit, body);
            uint32_t c = lerp_rgb(0xE8ECF2, 0xC9D2DC, hypotf(fx - gcx, fy - gcy) / grr);
            put(im, i, wx_rgb565(c), lit);
        }
    }
    scale_alpha(im, 1, HERO_FADE);
}

wx_box_t wx_paint_hero(wx_img_t *im, wx_hero_t hero, int moon_phase, int moon_illum)
{
    float cx = SUN_C, cy = SUN_C;
    if (hero == WX_HERO_MOON) cx = cy = 144.6f;
    if (hero == WX_HERO_MOON_SMALL) {
        cx = 300;
        cy = 72;
    }
    im->x0 = (int)lroundf(cx) - WX_HERO_BOX / 2;
    im->y0 = (int)lroundf(cy) - WX_HERO_BOX / 2;
    wx_paint_clear(im);
    if (hero == WX_HERO_SUN) sun(im);
    else if (hero == WX_HERO_MOON) moon(im, cx, cy, 50, 0.2f, moon_phase, moon_illum);
    else if (hero == WX_HERO_MOON_SMALL) moon(im, cx, cy, 34, 0.14f, moon_phase, moon_illum);
    return (wx_box_t){im->x0, im->y0, im->x0 + im->w, im->y0 + im->h};
}

wx_box_t wx_clouds_box(const wx_clouds_t *c)
{
    wx_box_t b = {WX_FACE, WX_FACE, 0, 0};
    for (int i = 0; i < c->n; i++) {
        const wx_cloud_t *cl = &c->cl[i];
        for (int k = 0; k < cl->n; k++) {
            const wx_circle_t *q = &cl->c[k];
            int x0 = (int)floorf(q->x - q->r - 3), x1 = (int)ceilf(q->x + q->r + 3), y0 = (int)floorf(q->y - q->r - 3);
            if (x0 < b.x0) b.x0 = x0;
            if (x1 > b.x1) b.x1 = x1;
            if (y0 < b.y0) b.y0 = y0;
        }
        int y1 = (int)ceilf(cl->base + 3);
        if (y1 > b.y1) b.y1 = y1;
    }
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 > WX_FACE) b.x1 = WX_FACE;
    if (b.y1 > WX_FACE) b.y1 = WX_FACE;
    b.x0 &= ~1;
    b.y0 &= ~1;
    b.x1 += b.x1 & 1;
    b.y1 += b.y1 & 1;
    if (b.x1 <= b.x0 || b.y1 <= b.y0) b = (wx_box_t){0, 0, 0, 0};
    return b;
}

static bool upper_cross(const wx_circle_t *a, const wx_circle_t *b, float *x, float *y)
{
    float dx = b->x - a->x, dy = b->y - a->y, d = hypotf(dx, dy);
    if (!(d < a->r + b->r) || !(d > fabsf(a->r - b->r))) return false;
    float p = (a->r * a->r - b->r * b->r + d * d) / (2 * d), h2 = a->r * a->r - p * p, h = h2 > 0 ? sqrtf(h2) : 0;
    float mx = a->x + p * dx / d, my = a->y + p * dy / d;
    float y1 = my - h * dx / d, y2 = my + h * dx / d;
    *x = y1 < y2 ? mx + h * dy / d : mx - h * dy / d;
    *y = fminf(y1, y2);
    return true;
}

static bool base_end(const wx_circle_t *c, float base, float side, float *x)
{
    float k = c->r * c->r - (c->y - base) * (c->y - base);
    *x = c->x + side * (k > 0 ? sqrtf(k) : 0);
    return k > 0;
}

static bool inside_other(const wx_cloud_t *cl, int a, int b, float x, float y)
{
    for (int k = 0; k < cl->n; k++)
        if (k != a && k != b && hypotf(x - cl->c[k].x, y - cl->c[k].y) <= cl->c[k].r) return true;
    return false;
}

static bool shape_ok(const wx_cloud_t *cl, wx_cloud_shape_t *s)
{
    const wx_circle_t *c = cl->c;
    const int n = cl->n;
    if (n < 1 || n > 4) return false;
    for (int k = 0; k < n; k++)
        if (!(c[k].r > 0) || !(c[k].y - c[k].r < cl->base)) return false;
    s->np = 0;
    if (!base_end(&c[0], cl->base, -1, &s->p[0][0]) || inside_other(cl, 0, 0, s->p[0][0], cl->base)) return false;
    s->p[s->np++][1] = cl->base;
    for (int i = 0; i + 1 < n; i++) {
        float *q = s->p[s->np];
        if (!upper_cross(&c[i], &c[i + 1], &q[0], &q[1]) || !(q[1] < cl->base) || !(q[0] > s->p[s->np - 1][0]) ||
            inside_other(cl, i, i + 1, q[0], q[1]))
            return false;
        s->np++;
    }
    float *q = s->p[s->np];
    if (!base_end(&c[n - 1], cl->base, 1, &q[0]) || !(q[0] > s->p[s->np - 1][0]) ||
        inside_other(cl, n - 1, n - 1, q[0], cl->base))
        return false;
    q[1] = cl->base;
    s->np++;
    return true;
}

bool wx_cloud_shape(const wx_cloud_t *cl, wx_cloud_shape_t *s)
{
    memset(s, 0, sizeof *s);
    if (!shape_ok(cl, s)) {
        memset(s, 0, sizeof *s);
        return false;
    }
    s->cl = *cl;
    const wx_circle_t *c = s->cl.c;
    for (int k = 0; k < s->cl.n; k++) {
        float ax = s->p[k][0] - c[k].x, ay = s->p[k][1] - c[k].y;
        float bx = s->p[k + 1][0] - c[k].x, by = s->p[k + 1][1] - c[k].y;
        s->large[k] = ax * by - ay * bx < 0;
    }
    s->box[0] = s->box[2] = s->p[0][0];
    s->box[1] = s->box[3] = s->p[0][1];
    for (int i = 1; i < s->np; i++) {
        s->box[0] = fminf(s->box[0], s->p[i][0]);
        s->box[1] = fminf(s->box[1], s->p[i][1]);
        s->box[2] = fmaxf(s->box[2], s->p[i][0]);
        s->box[3] = fmaxf(s->box[3], s->p[i][1]);
    }
    return true;
}

float wx_cloud_sd(const wx_cloud_shape_t *s, float x, float y)
{
    const int n = s->cl.n;
    if (n <= 0) return 1e9f;
    float b = y - s->cl.base, deep = 1e9f, c[4];
    for (int k = 0; k < n; k++) {
        float dx = x - s->cl.c[k].x, dy = y - s->cl.c[k].y;
        c[k] = sqrtf(dx * dx + dy * dy) - s->cl.c[k].r;
        deep = fminf(deep, c[k]);
    }
    if (deep <= -WX_CLOUD_EXACT && b <= -WX_CLOUD_EXACT) return fmaxf(deep, b);
    float ox = fmaxf(fmaxf(s->box[0] - x, x - s->box[2]), 0), oy = fmaxf(fmaxf(s->box[1] - y, y - s->box[3]), 0);
    if (b >= WX_CLOUD_EXACT || (deep >= WX_CLOUD_EXACT && ox * ox + oy * oy >= WX_CLOUD_EXACT * WX_CLOUD_EXACT))
        return fmaxf(b, deep);
    float d = 1e9f, v2 = 1e18f;
    for (int k = 0; k < n; k++) {
        const wx_circle_t *q = &s->cl.c[k];
        float dx = x - q->x, dy = y - q->y;
        float ax = s->p[k][0] - q->x, ay = s->p[k][1] - q->y, bx = s->p[k + 1][0] - q->x, by = s->p[k + 1][1] - q->y;
        bool on = s->large[k] ? !(bx * dy - by * dx > 0 && dx * ay - dy * ax > 0)
                              : ax * dy - ay * dx >= 0 && dx * by - dy * bx >= 0;
        if (on) d = fminf(d, fabsf(c[k]));
    }
    bool in = false;
    for (int i = 0, j = s->np - 1; i < s->np; j = i++) {
        const float *p = s->p[i], *q = s->p[j];
        float ex = x - p[0], ey = y - p[1];
        v2 = fminf(v2, ex * ex + ey * ey);
        if ((p[1] > y) != (q[1] > y) && x < (q[0] - p[0]) * (y - p[1]) / (q[1] - p[1]) + p[0]) in = !in;
    }
    d = fminf(d, sqrtf(v2));
    if (x >= s->p[0][0] && x <= s->p[s->np - 1][0]) d = fminf(d, fabsf(b));
    return b <= 0 && (in || deep < 0) ? -d : d;
}

void wx_paint_clouds(wx_img_t *im, const wx_clouds_t *c)
{
    static const uint32_t TONES[4][3] = {
        {0xE6ECF2, 0xE6ECF2, 0xC4CCD4},
        {0x8E96A0, 0x737B86, 0xA8B0BA},
        {0x555D68, 0x3A4048, 0x6A7480},
        {0x3E4652, 0x2E343D, 0x56606C},
    };
    wx_paint_clear(im);
    const uint32_t *tone = TONES[c->tone];
    const uint16_t line = wx_rgb565(tone[2]);
    for (int i = 0; i < c->n; i++) {
        const wx_cloud_t *cl = &c->cl[i];
        const uint16_t fill = wx_rgb565(tone[cl->back ? 1 : 0]);
        float bx0 = WX_FACE, bx1 = 0, by0 = WX_FACE;
        for (int k = 0; k < cl->n; k++) {
            bx0 = fminf(bx0, cl->c[k].x - cl->c[k].r - 3);
            bx1 = fmaxf(bx1, cl->c[k].x + cl->c[k].r + 3);
            by0 = fminf(by0, cl->c[k].y - cl->c[k].r - 3);
        }
        wx_cloud_shape_t sh;
        wx_cloud_shape(cl, &sh);
        int x0, y0, x1, y1;
        clip(im, bx0, by0, bx1, cl->base + 3, &x0, &y0, &x1, &y1);
        for (int y = y0; y < y1; y++) {
            row(im);
            for (int x = x0; x < x1; x++) {
                float d = wx_cloud_sd(&sh, (float)(im->x0 + x) + 0.5f, (float)(im->y0 + y) + 0.5f);
                int idx = y * im->w + x;
                put(im, idx, fill, cov(d));
                put(im, idx, line, cov(fabsf(d) - 2.25f));
            }
        }
    }
    scale_alpha(im, 1, c->fade_from);
}

static float capsule(float px, float py, float ax, float bx, float y)
{
    float cx = px < ax ? ax : px > bx ? bx : px;
    return hypotf(px - cx, py - y);
}

void wx_paint_mercury(wx_img_t *im, float top, uint32_t rgb)
{
    static const float TICKS[3] = {160, 147, 134};
    const float dy = WX_THERMO_DY;
    const uint16_t c = wx_rgb565(rgb), grey = wx_rgb565(0x8A8A8A);
    im->x0 = WX_THERMO_X;
    im->y0 = WX_THERMO_Y;
    wx_paint_clear(im);
    float cy = (top + WX_MERC_BOTTOM) / 2 + dy, hh = (WX_MERC_BOTTOM - top) / 2;
    for (int y = 0; y < im->h; y++) {
        row(im);
        for (int x = 0; x < im->w; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            float qx = fabsf(fx - 233) - 0, qy = fabsf(fy - cy) - (hh - 4 > 0 ? hh - 4 : 0);
            float rr = (qy > 0 ? hypotf(qx, qy) : qx) - 4;
            float bulb = hypotf(fx - 233, fy - (183 + dy)) - 10;
            int i = y * im->w + x;
            put(im, i, c, cov(fminf(rr, bulb)));
            for (int t = 0; t < 3; t++)
                put(im, i, top <= TICKS[t] ? c : grey, cov(capsule(fx, fy, 253, 261, TICKS[t] + dy) - 1.5f));
        }
    }
}

void wx_paint_marker(wx_img_t *im)
{
    const float m = WX_MARKER / 2.0f;
    wx_paint_clear(im);
    const uint16_t ring = wx_rgb565(0xF4F4F2);
    for (int y = 0; y < im->h; y++) {
        row(im);
        for (int x = 0; x < im->w; x++) {
            float d = hypotf((float)x + 0.5f - m, (float)y + 0.5f - m);
            int i = y * im->w + x;
            put(im, i, 0, cov(d - 17));
            put(im, i, ring, cov(fabsf(d - 10) - 2.5f));
        }
    }
}

void wx_cloud_one(const wx_clouds_t *all, int i, wx_clouds_t *one)
{
    *one = *all;
    one->n = 0;
    if (i < 0 || i >= all->n) return;
    one->n = 1;
    one->cl[0] = all->cl[i];
}

static int box_px(wx_box_t b) { return (b.x1 - b.x0) * (b.y1 - b.y0); }

static int scene_bytes(wx_box_t b) { return (box_px(b) * 3 + WX_SCENE_ALIGN - 1) / WX_SCENE_ALIGN * WX_SCENE_ALIGN; }

static void clouds_none(wx_bake_out_t *out)
{
    out->nclouds = 0;
    memset(out->cloud, 0, sizeof out->cloud);
    memset(out->cloud_at, 0, sizeof out->cloud_at);
    memset(out->cloud_back, 0, sizeof out->cloud_back);
}

int wx_scene_px(void)
{
    int m = 0;
    for (int band = 1; band <= 4; band++) {
        wx_clouds_t all, one;
        wx_clouds_for(band, false, &all);
        int sum = 0;
        for (int i = 0; i < all.n; i++) {
            wx_cloud_one(&all, i, &one);
            sum += scene_bytes(wx_clouds_box(&one));
        }
        sum = (sum + 2) / 3;
        int whole = box_px(wx_clouds_box(&all));
        if (sum > m) m = sum;
        if (whole > m) m = whole;
    }
    return m;
}

static wx_box_t layer(uint8_t *buf, const wx_clouds_t *c, void (*yield)(void))
{
    wx_box_t b = wx_clouds_box(c);
    if (!c->n || b.x1 <= b.x0) return (wx_box_t){0, 0, 0, 0};
    int w = b.x1 - b.x0, h = b.y1 - b.y0;
    wx_img_t im = {.px = (uint16_t *)buf, .a = buf + (size_t)w * h * 2, .w = w, .h = h, .x0 = b.x0, .y0 = b.y0, .yield = yield};
    wx_paint_clouds(&im, c);
    return b;
}

static void bake_days(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out);
static void bake_sun(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out);
static void bake_air(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out);
static void paint_sock(uint8_t *fill, uint8_t *dark, wx_box_t box, const wx_pt_t q[WX_WIND_QUADS][4], void (*yield)(void));
static void paint_sun_rays(uint8_t *a8, wx_box_t box, void (*yield)(void));

static void bake_rain(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    const wx_rain_t *r = &t->rain;
    if (kind & WX_BAKE_RING) wx_paint_ring_rain(b->ring, r->intensity, !r->has_pop, b->lut);
    if (kind & WX_BAKE_HERO) out->hero = (wx_box_t){0, 0, 0, 0};
    if (!(kind & WX_BAKE_SCENE)) return;
    wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE, .yield = b->yield};
    wx_paint_clear(&bg);
    wx_paint_umbrella(&bg);
    wx_paint_temp_dots(&bg, t);
    wx_ring_blit(b->ring, b->bg);
    clouds_none(out);
    wx_rain_layout_t l;
    wx_rain_layout(&l);
    uint8_t *m = b->scene;
    for (int s = 0; s < WX_SHEETS; s++) {
        wx_paint_sheet(m + l.tile[s], s, r);
        wx_paint_ramp(m + l.top[s], s, false, l.ramp_h[s]);
        wx_paint_ramp(m + l.bottom[s], s, true, l.ramp_h[s]);
        wx_box_t c = l.cover_box[s];
        int w = c.x1 - c.x0, h = c.y1 - c.y0;
        wx_img_t cv = {.px = (uint16_t *)(m + l.cover[s]), .a = m + l.cover[s] + (size_t)w * h * 2, .w = w, .h = h,
                       .x0 = c.x0, .y0 = c.y0, .yield = b->yield};
        wx_paint_clear(&cv);
        wx_paint_umbrella(&cv);
    }
    for (int p = 0; p < WX_DRIP_POSES; p++) wx_paint_drip(m + l.drip[p], p);
    for (int n = 0; n < WX_SPLASHES; n++) wx_paint_splash(m + l.splash[n], n);
    wx_paint_bolt(m + l.bolt);
}

static void bake_cast(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    const wx_cast_t *c = &t->cast;
    if (kind & WX_BAKE_RING) wx_paint_ring_cast(b->ring, c);
    if (kind & WX_BAKE_HERO) out->hero = (wx_box_t){0, 0, 0, 0};
    if (!(kind & WX_BAKE_SCENE)) return;
    wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE, .yield = b->yield};
    wx_paint_clear(&bg);
    wx_paint_temp_dots(&bg, t);
    wx_ring_blit(b->ring, b->bg);
    clouds_none(out);
    wx_cast_layout_t l;
    wx_cast_layout(c->variant, &l);
    for (int s = 0; s < l.sides; s++) {
        wx_clouds_t cl;
        wx_cast_clouds(c->variant, s, &cl);
        wx_box_t cb = l.cloud[s];
        int w = cb.x1 - cb.x0, h = cb.y1 - cb.y0;
        uint8_t *m = b->scene + l.cloud_at[s];
        wx_img_t im = {.px = (uint16_t *)m, .a = m + (size_t)w * h * 2, .w = w, .h = h, .x0 = cb.x0, .y0 = cb.y0,
                       .yield = b->yield};
        wx_paint_clouds(&im, &cl);
        const wx_cast_sheet_t *sh = wx_cast_sheet(c->variant, s);
        wx_paint_cast_sheet(b->scene + l.strip[s], sh, c->snow);
        wx_paint_cast_ramp(b->scene + l.top[s], sh, false, l.ramp_h[s]);
        wx_paint_cast_ramp(b->scene + l.bottom[s], sh, true, l.ramp_h[s]);
    }
}

static void bake_wind(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    const wx_wind_t *w = &t->wind;
    if (kind & WX_BAKE_RING) wx_paint_ring_wind(b->ring, !w->have, b->lut);
    if (kind & WX_BAKE_HERO) out->hero = (wx_box_t){0, 0, 0, 0};
    if (!(kind & WX_BAKE_SCENE)) return;
    wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE, .yield = b->yield};
    wx_paint_clear(&bg);
    wx_paint_pole(&bg, w->level);
    wx_paint_temp_dots(&bg, t);
    wx_ring_blit(b->ring, b->bg);
    clouds_none(out);
    wx_wind_layout_t l;
    wx_wind_layout(w->level, &l);
    for (int p = 0; p < WX_WIND_POSES; p++) {
        wx_pt_t q[WX_WIND_QUADS][4];
        for (int k = 0; k < WX_WIND_QUADS; k++) wx_wind_quad(w->level, p, k, q[k]);
        paint_sock(b->scene + l.fill[p], b->scene + l.dark[p], l.sock, q, b->yield);
    }
    for (int r = 0; r < l.nrows; r++) wx_paint_streak(b->scene + l.streak[r], w->level, r, l.streak_w[r]);
    for (int k = 0; l.nleaves && k < WX_LEAF_ROTS; k++) wx_paint_leaf(b->scene + l.leaf[k], k);
}

void wx_bake(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    b->ring->yield = b->yield;
    if (kind & WX_BAKE_SCENE) kind = WX_BAKE_FULL;
    if (t->sun.on) {
        bake_sun(t, kind, b, out);
        return;
    }
    if (t->air.on) {
        bake_air(t, kind, b, out);
        return;
    }
    if (t->days.on) {
        bake_days(t, kind, b, out);
        return;
    }
    if (t->wind.on) {
        bake_wind(t, kind, b, out);
        return;
    }
    if (t->cast.on) {
        bake_cast(t, kind, b, out);
        return;
    }
    if (t->rain.on) {
        bake_rain(t, kind, b, out);
        return;
    }
    if (kind & WX_BAKE_RING) wx_paint_ring(b->ring, t->lo, t->hi, !t->have, b->lut);
    if (kind & WX_BAKE_SCENE) {
        wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE, .yield = b->yield};
        wx_paint_clear(&bg);
        if (t->hero == WX_HERO_SUN) wx_paint_rays(&bg);
        if (t->stars) wx_paint_stars(&bg);
        wx_paint_thermo(&bg);
        wx_paint_temp_dots(&bg, t);
        wx_ring_blit(b->ring, b->bg);
        wx_clouds_t all, one;
        wx_clouds_for(t->cloud_band, t->night, &all);
        clouds_none(out);
        for (int i = 0, at = 0; i < all.n && out->nclouds < WX_CLOUDS_MAX; i++) {
            wx_cloud_one(&all, i, &one);
            wx_box_t bx = layer(b->scene + at, &one, b->yield);
            if (bx.x1 <= bx.x0) continue;
            out->cloud[out->nclouds] = bx;
            out->cloud_back[out->nclouds] = one.cl[0].back;
            out->cloud_at[out->nclouds++] = at;
            at += scene_bytes(bx);
        }
    }
    if (kind & WX_BAKE_HERO) {
        out->hero = (wx_box_t){0, 0, 0, 0};
        if (t->hero != WX_HERO_NONE) {
            wx_img_t h = {.px = (uint16_t *)b->hero, .a = b->hero + WX_HERO_BOX * WX_HERO_BOX * 2, .w = WX_HERO_BOX,
                          .h = WX_HERO_BOX, .yield = b->yield};
            out->hero = wx_paint_hero(&h, t->hero, t->moon_phase, t->moon_illum);
        }
    }
}

static const wx_box_t COVER[WX_SHEETS] = {{102, 150, 118, 202}, {348, 150, 364, 202}, {150, 78, 316, 132}};

static int box_w(wx_box_t b) { return b.x1 - b.x0; }
static int box_h(wx_box_t b) { return b.y1 - b.y0; }

void wx_rain_layout(wx_rain_layout_t *l)
{
    int o = 0;
    for (int s = 0; s < WX_SHEETS; s++) {
        wx_box_t b = wx_sheet(s)->box;
        l->ramp_h[s] = (int)ceilf(WX_RAMP * (float)box_h(b));
        l->tile[s] = o;
        o += box_w(b) * WX_SHEET_PERIOD;
        l->top[s] = o;
        o += box_w(b) * l->ramp_h[s];
        l->bottom[s] = o;
        o += box_w(b) * l->ramp_h[s];
        l->cover_box[s] = COVER[s];
        l->cover[s] = o;
        o += box_w(COVER[s]) * box_h(COVER[s]) * 3;
    }
    for (int p = 0; p < WX_DRIP_POSES; p++) {
        l->drip[p] = o;
        o += WX_DRIP_W * WX_DRIP_H;
    }
    for (int n = 0; n < WX_SPLASHES; n++) {
        l->splash[n] = o;
        o += WX_SPLASH_W * WX_SPLASH_H;
    }
    l->bolt = o;
    o += WX_BOLT_W * WX_BOLT_H;
    l->total = o;
}

static const float DRIP_X[WX_DRIPS] = {107, 359};

int wx_drip_x(int i) { return (int)DRIP_X[i] - WX_DRIP_W / 2; }

static inline void put_a(uint8_t *a, float v)
{
    if (!(v > 0.002f)) return;
    if (v > 1) v = 1;
    float o = v + (*a / 255.0f) * (1 - v);
    *a = (uint8_t)lroundf(o * 255.0f);
}

static float seg_d(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float l2 = vx * vx + vy * vy;
    float t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
    t = clamp01(t);
    return hypotf(wx - t * vx, wy - t * vy);
}

static float ellipse_d(float x, float y, float cx, float cy, float rx, float ry)
{
    float dx = x - cx, dy = y - cy;
    float f = dx * dx / (rx * rx) + dy * dy / (ry * ry) - 1;
    float gx = 2 * dx / (rx * rx), gy = 2 * dy / (ry * ry);
    float gl = sqrtf(gx * gx + gy * gy);
    return gl > 1e-6f ? f / gl : -(rx < ry ? rx : ry);
}

#define CANOPY_L 105.0f
#define CANOPY_R 361.0f
#define CANOPY_RIM 198.0f
#define CANOPY_RY 103.0f
#define SCALLOPS 5
#define UMB_HW 2.25f
#define SCALLOP_RY 9.0f
#define HANDLE_X 233.0f
#define HANDLE_TOP 185.0f
#define HANDLE_GAP0 210.75f
#define HANDLE_GAP1 336.25f
#define HOOK_Y 366.0f
#define HOOK_R 11.0f
#define FERRULE_Y0 81.0f
#define FERRULE_Y1 93.0f

static float canopy_d(float x, float y)
{
    float cx = (CANOPY_L + CANOPY_R) / 2, rx = (CANOPY_R - CANOPY_L) / 2;
    float d = fmaxf(ellipse_d(x, y, cx, CANOPY_RIM, rx, CANOPY_RY), y - CANOPY_RIM);
    float w = (CANOPY_R - CANOPY_L) / SCALLOPS;
    for (int k = 0; k < SCALLOPS; k++) d = fmaxf(d, -ellipse_d(x, y, CANOPY_L + w * (k + 0.5f), CANOPY_RIM, w / 2, SCALLOP_RY));
    return d;
}

static float handle_d(float x, float y)
{
    float d = seg_d(x, y, HANDLE_X, HANDLE_TOP, HANDLE_X, HANDLE_GAP0);
    d = fminf(d, seg_d(x, y, HANDLE_X, HANDLE_GAP1, HANDLE_X, HOOK_Y));
    float arc = y >= HOOK_Y ? fabsf(hypotf(x - (HANDLE_X + HOOK_R), y - HOOK_Y) - HOOK_R)
                            : fminf(hypotf(x - HANDLE_X, y - HOOK_Y), hypotf(x - (HANDLE_X + 2 * HOOK_R), y - HOOK_Y));
    return fminf(d, arc);
}

#define UMB_REACH (UMB_HW + 0.5f)

static void umbrella_handle(wx_img_t *im, uint16_t c, float fy0, float fy1)
{
    int x0, y0, x1, y1;
    clip(im, HANDLE_X - UMB_REACH - 1, fy0, HANDLE_X + 2 * HOOK_R + UMB_REACH + 1, fy1, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        row(im);
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            put(im, y * im->w + x, c, cov(handle_d(fx, fy) - UMB_HW));
        }
    }
}

static void umbrella_canopy(wx_img_t *im, uint16_t c)
{
    const float cx = (CANOPY_L + CANOPY_R) / 2, rx = (CANOPY_R - CANOPY_L) / 2;
    const float out2 = 1.06f * 1.06f, in2 = 0.94f * 0.94f, scallop_reach = SCALLOP_RY + 14.0f;
    int x0, y0, x1, y1;
    clip(im, CANOPY_L - 4, CANOPY_RIM - CANOPY_RY - 8, CANOPY_R + 4, CANOPY_RIM + UMB_REACH + 1, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        row(im);
        float fy = (float)(im->y0 + y) + 0.5f, v = (fy - CANOPY_RIM) / CANOPY_RY;
        bool deep_row = CANOPY_RIM - fy >= scallop_reach;
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, u = (fx - cx) / rx, r2 = u * u + v * v;
            if (r2 >= out2) continue;
            int i = y * im->w + x;
            if (deep_row && r2 <= in2) {
                put(im, i, 0, 1);
                continue;
            }
            float d = canopy_d(fx, fy);
            put(im, i, 0, cov(d));
            put(im, i, c, cov(fabsf(d) - UMB_HW));
        }
    }
}

void wx_paint_umbrella(wx_img_t *im)
{
    const uint16_t orange = wx_rgb565(0xFF8A3D);
    umbrella_handle(im, orange, HANDLE_TOP - UMB_REACH - 1, HANDLE_GAP0 + UMB_REACH + 1);
    umbrella_handle(im, orange, HANDLE_GAP1 - UMB_REACH - 1, HOOK_Y + HOOK_R + UMB_REACH + 1);
    umbrella_canopy(im, orange);
    int x0, y0, x1, y1;
    clip(im, HANDLE_X - UMB_REACH - 1, FERRULE_Y0 - UMB_REACH - 1, HANDLE_X + UMB_REACH + 1, FERRULE_Y1 + UMB_REACH + 1, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        row(im);
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            put(im, y * im->w + x, orange, cov(seg_d(fx, fy, HANDLE_X, FERRULE_Y1, HANDLE_X, FERRULE_Y0) - UMB_HW));
        }
    }
}

static float flake_d(float x, float y, float cx, float cy, float r)
{
    float d = 1e9f;
    for (int k = 0; k < 3; k++) {
        float a = (float)k * 60.0f * DEG, dx = r * cosf(a), dy = r * sinf(a);
        d = fminf(d, seg_d(x, y, cx - dx, cy - dy, cx + dx, cy + dy));
    }
    return d;
}

void wx_paint_sheet(uint8_t *a8, int s, const wx_rain_t *r)
{
    const wx_sheet_t *sh = wx_sheet(s);
    int w = box_w(sh->box);
    memset(a8, 0, (size_t)w * WX_SHEET_PERIOD);
    bool snow = r->kind == WX_PRECIP_SNOW;
    for (int n = 0; n < sh->n; n++) {
        const wx_drop_t *d = &sh->drop[n];
        if (!wx_drop_shown(d, r)) continue;
        float ax = d->x - (float)sh->box.x0, ay = d->y + (d->tile ? WX_SHEET_H : 0);
        float bx = ax - d->slant, by = ay + d->len;
        float cx = (ax + bx) / 2, cy = (ay + by) / 2, fr = d->op >= 0.8f ? 5.0f : 4.0f;
        float ext = snow ? fr + 2 : 3;
        float lo_y = (snow ? cy - ext : ay - ext), hi_y = (snow ? cy + ext : by + ext);
        float lo_x = fminf(ax, bx) - (snow ? fr + 2 : 3), hi_x = fmaxf(ax, bx) + (snow ? fr + 2 : 3);
        for (int wrap = -1; wrap <= 1; wrap++) {
            float oy = (float)(wrap * WX_SHEET_PERIOD);
            int ys = (int)floorf(lo_y + oy), ye = (int)ceilf(hi_y + oy);
            if (ys < 0) ys = 0;
            if (ye > WX_SHEET_PERIOD) ye = WX_SHEET_PERIOD;
            int xs = (int)floorf(lo_x), xe = (int)ceilf(hi_x);
            if (xs < 0) xs = 0;
            if (xe > w) xe = w;
            for (int y = ys; y < ye; y++)
                for (int x = xs; x < xe; x++) {
                    float px = (float)x + 0.5f, py = (float)y + 0.5f - oy;
                    float c = snow ? cov(flake_d(px, py, cx, cy, fr) - 1.25f) : cov(seg_d(px, py, ax, ay, bx, by) - 2.0f);
                    put_a(&a8[y * w + x], c * d->op);
                }
        }
    }
}

void wx_paint_ramp(uint8_t *a8, int s, bool bottom, int ramp_h)
{
    wx_box_t b = wx_sheet(s)->box;
    int w = box_w(b), h = box_h(b);
    for (int k = 0; k < ramp_h; k++) {
        int row = bottom ? h - ramp_h + k : k;
        float t = ((float)row + 0.5f) / (float)h;
        float m = t < WX_RAMP ? t / WX_RAMP : t > 1 - WX_RAMP ? (1 - t) / WX_RAMP : 1;
        memset(a8 + (size_t)k * w, (int)lroundf((1 - clamp01(m)) * 255.0f), (size_t)w);
    }
}

static float drip_hw(float t) { return 8.0f * t * (1 - t); }

void wx_paint_drip(uint8_t *a8, int pose)
{
    static const float SCALE[WX_DRIP_POSES] = {0.45f, 0.75f, 1.0f};
    const float sc = SCALE[pose < 0 ? 0 : pose >= WX_DRIP_POSES ? WX_DRIP_POSES - 1 : pose];
    const float ox = WX_DRIP_W / 2.0f, oy = 203.0f - WX_DRIP_Y;
    memset(a8, 0, WX_DRIP_W * WX_DRIP_H);
    for (int y = 0; y < WX_DRIP_H; y++) {
        float hw[4];
        bool in[4], any = false;
        for (int sy = 0; sy < 4; sy++) {
            float py = ((float)y + (sy + 0.5f) / 4 - oy) / sc + 203.0f;
            in[sy] = !(py < 199 || py > 208);
            if (!in[sy]) continue;
            any = true;
            float lo = 0, hi = 1;
            for (int it = 0; it < 24; it++) {
                float t = (lo + hi) / 2, u = 1 - t;
                float yt = 199 * u * u + 2 * u * t * 205 + 208 * t * t;
                if (yt < py) lo = t;
                else hi = t;
            }
            hw[sy] = drip_hw((lo + hi) / 2);
        }
        if (!any) continue;
        for (int x = 0; x < WX_DRIP_W; x++) {
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; in[sy] && sx < 4; sx++)
                    if (fabsf(((float)x + (sx + 0.5f) / 4 - ox) / sc) <= hw[sy]) hit++;
            a8[y * WX_DRIP_W + x] = (uint8_t)(hit * 255 / 16);
        }
    }
}

void wx_paint_splash(uint8_t *a8, int n)
{
    wx_splash_at_t at;
    memset(a8, 0, WX_SPLASH_W * WX_SPLASH_H);
    if (!wx_splash_at(n, &at)) return;
    const wx_drop_t *d = &wx_sheet(at.sheet)->drop[at.drop];
    float x = d->x, y = wx_canopy_y(d->x) - 3;
    for (int r = 0; r < WX_SPLASH_H; r++)
        for (int c = 0; c < WX_SPLASH_W; c++) {
            float px = (float)(at.x + c) + 0.5f, py = (float)(at.y + r) + 0.5f;
            float dd = fminf(seg_d(px, py, x, y, x - 5, y - 6), seg_d(px, py, x, y, x + 5, y - 6));
            put_a(&a8[r * WX_SPLASH_W + c], cov(dd - 1.5f));
        }
}

void wx_paint_bolt(uint8_t *a8)
{
    static const float P[7][2] = {{14, 0}, {36, 0}, {24, 22}, {38, 22}, {6, 52}, {14, 30}, {0, 30}};
    const float k = 0.8f, o = 1.0f;
    memset(a8, 0, WX_BOLT_W * WX_BOLT_H);
    for (int y = 0; y < WX_BOLT_H; y++)
        for (int x = 0; x < WX_BOLT_W; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f, d = 1e9f;
            bool in = false;
            for (int i = 0, j = 6; i < 7; j = i++) {
                float ax = P[j][0] * k + o, ay = P[j][1] * k + o, bx = P[i][0] * k + o, by = P[i][1] * k + o;
                d = fminf(d, seg_d(px, py, ax, ay, bx, by));
                if ((ay > py) != (by > py) && px < (bx - ax) * (py - ay) / (by - ay) + ax) in = !in;
            }
            a8[y * WX_BOLT_W + x] = (uint8_t)lroundf(cov((in ? -d : d) - 0.8f) * 255.0f);
        }
}

typedef struct {
    int n;
    wx_tone_t tone;
    float fade_from;
    struct {
        bool back;
        float base;
        int n;
        float c[4][3];
    } cl[2];
} cast_clouds_t;

static const cast_clouds_t CAST_CLOUDS[3][WX_CAST_SIDES] = {
    {{1, WX_TONE_WHITE, 195, {{0, 170, 4, {{168.5f, 156.5f, 19.5f}, {203, 140, 27}, {245, 134, 28.5f}, {291.5f, 152, 22.5f}}}}}},
    {{1, WX_TONE_LIGHT, 180, {{0, 180, 3, {{63.5f, 161.1f, 29.7f}, {114.8f, 144.9f, 35.1f}, {160.7f, 163.8f, 21.6f}}}}},
     {1, WX_TONE_LIGHT, 180, {{0, 172, 3, {{305.3f, 155.8f, 21.6f}, {351.2f, 136.9f, 35.1f}, {402.5f, 153.1f, 29.7f}}}}}},
    {{2, WX_TONE_DARK, 160,
      {{1, 132, 4, {{20, 116, 28}, {66, 96, 32}, {118, 102, 30}, {166, 118, 22}}},
       {0, 172, 4, {{30, 154, 30}, {80, 138, 34}, {134, 144, 30}, {186, 158, 24}}}}},
     {2, WX_TONE_DARK, 160,
      {{1, 122, 4, {{300, 108, 22}, {348, 92, 30}, {400, 86, 32}, {446, 106, 28}}},
       {0, 162, 4, {{264, 148, 24}, {314, 134, 30}, {368, 128, 34}, {420, 144, 30}}}}}},
};

void wx_cast_clouds(int variant, int side, wx_clouds_t *out)
{
    memset(out, 0, sizeof *out);
    if (side < 0 || side >= wx_cast_sides(variant)) return;
    const cast_clouds_t *src = &CAST_CLOUDS[variant - 1][side];
    out->n = src->n;
    out->tone = src->tone;
    out->fade_from = src->fade_from;
    for (int i = 0; i < src->n; i++) {
        out->cl[i].back = src->cl[i].back;
        out->cl[i].base = src->cl[i].base;
        out->cl[i].n = src->cl[i].n;
        for (int k = 0; k < src->cl[i].n; k++)
            out->cl[i].c[k] = (wx_circle_t){src->cl[i].c[k][0], src->cl[i].c[k][1], src->cl[i].c[k][2]};
    }
}

static wx_box_t box_union(wx_box_t a, wx_box_t b)
{
    return (wx_box_t){a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
}

void wx_cast_layout(int variant, wx_cast_layout_t *l)
{
    memset(l, 0, sizeof *l);
    l->sides = wx_cast_sides(variant);
    const int lo = (int)(WX_FACE_C - RIM_R) & ~1, hi = ((int)(WX_FACE_C + RIM_R) + 1) & ~1;
    int o = 0;
    for (int s = 0; s < l->sides; s++) {
        wx_clouds_t cl;
        wx_cast_clouds(variant, s, &cl);
        wx_box_t b = wx_clouds_box(&cl);
        b = (wx_box_t){b.x0 < lo ? lo : b.x0, b.y0 < lo ? lo : b.y0, b.x1 > hi ? hi : b.x1, b.y1 > hi ? hi : b.y1};
        const wx_cast_sheet_t *sh = wx_cast_sheet(variant, s);
        l->cloud[s] = b;
        l->box[s] = box_union((wx_box_t){b.x0 - WX_CAST_BREATHE_PX, b.y0, b.x1 + WX_CAST_BREATHE_PX, b.y1}, sh->box);
        int w = box_w(sh->box);
        l->ramp_h[s] = (int)ceilf(WX_CAST_RAMP * (float)box_h(sh->box));
        l->cloud_at[s] = o;
        o += box_w(b) * box_h(b) * 3;
        l->strip[s] = o;
        o += w * 2 * sh->h;
        l->top[s] = o;
        o += w * l->ramp_h[s];
        l->bottom[s] = o;
        o += w * l->ramp_h[s];
    }
    l->total = o;
}

void wx_paint_cast_sheet(uint8_t *a8, const wx_cast_sheet_t *sh, bool snow)
{
    const int w = box_w(sh->box), period = 2 * sh->h;
    memset(a8, 0, (size_t)w * period);
    for (int t = 0; t < 2; t++)
        for (int n = 0; n < sh->n; n++) {
            const wx_streak_t *d = t ? &sh->b[n] : &sh->a[n];
            float ax = d->x - (float)sh->box.x0, ay = d->y + (float)(t * sh->h);
            float bx = ax - d->slant, by = ay + d->len;
            float cx = (ax + bx) / 2, cy = (ay + by) / 2, dr = sh->w * 0.75f, hw = sh->w / 2;
            float ext = (snow ? dr : hw) + 2;
            float lo_y = snow ? cy - ext : ay - ext, hi_y = snow ? cy + ext : by + ext;
            float lo_x = (snow ? cx : fminf(ax, bx)) - ext, hi_x = (snow ? cx : fmaxf(ax, bx)) + ext;
            for (int wrap = -1; wrap <= 1; wrap++) {
                float oy = (float)(wrap * period);
                int ys = (int)floorf(lo_y + oy), ye = (int)ceilf(hi_y + oy), xs = (int)floorf(lo_x), xe = (int)ceilf(hi_x);
                if (ys < 0) ys = 0;
                if (ye > period) ye = period;
                if (xs < 0) xs = 0;
                if (xe > w) xe = w;
                for (int y = ys; y < ye; y++)
                    for (int x = xs; x < xe; x++) {
                        float px = (float)x + 0.5f, py = (float)y + 0.5f - oy;
                        float c = snow ? cov(hypotf(px - cx, py - cy) - dr) : cov(seg_d(px, py, ax, ay, bx, by) - hw);
                        put_a(&a8[y * w + x], c * d->op);
                    }
            }
        }
}

void wx_paint_cast_ramp(uint8_t *a8, const wx_cast_sheet_t *sh, bool bottom, int ramp_h)
{
    int w = box_w(sh->box), h = box_h(sh->box);
    for (int k = 0; k < ramp_h; k++) {
        int row = bottom ? h - ramp_h + k : k;
        float t = ((float)row + 0.5f) / (float)h;
        float m = t < WX_CAST_RAMP ? t / WX_CAST_RAMP : t > 1 - WX_CAST_RAMP ? (1 - t) / WX_CAST_RAMP : 1;
        memset(a8 + (size_t)k * w, (int)lroundf((1 - clamp01(m)) * 255.0f), (size_t)w);
    }
}

#define DROP_TAIL 15.0f
#define DROP_HEAD 7.0f

const float wx_drop_hw[WX_DROP_HW_N] = {
    0.0000f, 0.0653f, 0.1364f, 0.2133f, 0.2960f, 0.3847f, 0.4794f, 0.5801f, 0.6868f, 0.7995f, 0.9183f, 1.0429f,
    1.1734f, 1.3096f, 1.4513f, 1.5984f, 1.7507f, 1.9078f, 2.0695f, 2.2353f, 2.4050f, 2.5780f, 2.7538f, 2.9320f,
    3.1120f, 3.2932f, 3.4750f, 3.6568f, 3.8379f, 4.0178f, 4.1958f, 4.3714f, 4.5439f, 4.7128f, 4.8777f, 5.0379f,
    5.1932f, 5.3431f, 5.4873f, 5.6255f, 5.7574f, 5.8828f, 6.0017f, 6.1138f, 6.2191f, 6.3175f, 6.4090f, 6.4936f,
    6.5713f, 6.6422f, 6.7064f, 6.7639f, 6.8149f, 6.8593f, 6.8975f, 6.9294f, 6.9551f, 6.9750f, 6.9890f, 6.9973f,
    7.0000f
};

static float drop_hw_at(float x)
{
    float f = (x + DROP_TAIL) / DROP_TAIL * (float)(WX_DROP_HW_N - 1);
    if (f <= 0) return wx_drop_hw[0];
    if (f >= WX_DROP_HW_N - 1) return wx_drop_hw[WX_DROP_HW_N - 1];
    int i = (int)f;
    return wx_drop_hw[i] + (wx_drop_hw[i + 1] - wx_drop_hw[i]) * (f - (float)i);
}

wx_box_t wx_paint_drop(wx_img_t *im, float deg)
{
    float hx, hy;
    wx_cast_ring_xy(deg, &hx, &hy);
    im->x0 = (int)floorf(hx) - WX_DROP_BOX / 2;
    im->y0 = (int)floorf(hy) - WX_DROP_BOX / 2;
    wx_paint_clear(im);
    const float a = deg * DEG, ca = cosf(a), sa = sinf(a);
    const uint16_t blue = wx_rgb565(WX_CAST_RGB);
    for (int y = 0; y < im->h; y++) {
        row(im);
        for (int x = 0; x < im->w; x++) {
            float dx = (float)(im->x0 + x) + 0.5f - hx, dy = (float)(im->y0 + y) + 0.5f - hy;
            float lx = dx * ca + dy * sa, ly = -dx * sa + dy * ca;
            float d = hypotf(lx, ly) - DROP_HEAD;
            if (lx < -DROP_TAIL) d = fminf(d, hypotf(lx + DROP_TAIL, ly));
            else if (lx <= 0) d = fminf(d, fabsf(ly) - drop_hw_at(lx));
            int i = y * im->w + x;
            put(im, i, 0, cov(d - 1));
            put(im, i, blue, cov(d + 1));
        }
    }
    return (wx_box_t){im->x0, im->y0, im->x0 + im->w, im->y0 + im->h};
}

void wx_paint_ring_wind(wx_ring_t *r, bool grey, uint16_t lut[WX_RING_LUT])
{
    for (int i = 0; i <= RING_STEPS; i++)
        lut[i] = wx_rgb565(grey ? 0x262626 : wx_wind_rgb((int)lroundf((float)WX_WIND_GAUGE_KMH * (float)i / RING_STEPS)));
    ring_raster(r, lut);
}

void wx_paint_pole(wx_img_t *im, int level)
{
    const wx_wind_geo_t *g = wx_wind_geo(level);
    const float px = g->mount.x - WX_WIND_ARM, top = g->mount.y - WX_WIND_POLE_TOP, base = WX_WIND_BASE_Y;
    const float B[4][5] = {
        {px - 3.5f, top, px + 3.5f, base - 6, 3.5f},
        {px, g->mount.y - 3, px + WX_WIND_ARM, g->mount.y + 3, 3},
        {px - 24, base - 7, px + 24, base, 3.5f},
        {px - 5.5f, top - 5.5f, px + 5.5f, top + 5.5f, 5.5f},
    };
    const uint16_t col = wx_rgb565(WX_WIND_POLE_RGB);
    int x0, y0, x1, y1;
    clip(im, px - 26, top - 7, px + WX_WIND_ARM + 2, base + 1, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        row(im);
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f, d = 1e9f;
            for (int k = 0; k < 4; k++) d = fminf(d, rbox(fx, fy, B[k]));
            put(im, y * im->w + x, col, cov(d));
        }
    }
}

#define SOCK_PAD 2
#define SOCK_SS 4

static wx_box_t even_box(float x0, float y0, float x1, float y1)
{
    wx_box_t b = {((int)floorf(x0)) & ~1, ((int)floorf(y0)) & ~1, (int)ceilf(x1), (int)ceilf(y1)};
    b.x1 += b.x1 & 1;
    b.y1 += b.y1 & 1;
    return b;
}

void wx_wind_layout(int level, wx_wind_layout_t *l)
{
    memset(l, 0, sizeof *l);
    const wx_wind_geo_t *g = wx_wind_geo(level);
    float x0 = WX_FACE, y0 = WX_FACE, x1 = 0, y1 = 0;
    for (int p = 0; p < WX_WIND_POSES; p++)
        for (int q = 0; q < WX_WIND_QUADS; q++) {
            wx_pt_t pt[4];
            wx_wind_quad(level, p, q, pt);
            for (int i = 0; i < 4; i++) {
                x0 = fminf(x0, pt[i].x);
                x1 = fmaxf(x1, pt[i].x);
                y0 = fminf(y0, pt[i].y);
                y1 = fmaxf(y1, pt[i].y);
            }
        }
    l->sock = even_box(x0 - SOCK_PAD, y0 - SOCK_PAD, x1 + SOCK_PAD, y1 + SOCK_PAD);
    float ax0 = (float)l->sock.x0, ay0 = (float)l->sock.y0, ax1 = (float)l->sock.x1, ay1 = (float)l->sock.y1;
    int o = 0, sock = box_w(l->sock) * box_h(l->sock);
    for (int p = 0; p < WX_WIND_POSES; p++) {
        l->fill[p] = o;
        o += sock;
        l->dark[p] = o;
        o += sock;
    }
    l->nrows = g->nrows;
    for (int r = 0; r < g->nrows; r++) {
        int sx = wx_wind_streak_x(level, r), sy = wx_wind_streak_y(level, r);
        l->streak_w[r] = (int)ceilf(g->row[r].len) + 2 * WX_STREAK_PAD + 1;
        l->streak[r] = o;
        o += l->streak_w[r] * WX_STREAK_H;
        ax0 = fminf(ax0, (float)(sx - 24));
        ax1 = fmaxf(ax1, (float)(sx + l->streak_w[r] + 56));
        ay0 = fminf(ay0, (float)sy);
        ay1 = fmaxf(ay1, (float)(sy + WX_STREAK_H));
    }
    l->nleaves = g->nleaves;
    for (int k = 0; g->nleaves && k < WX_LEAF_ROTS; k++) {
        l->leaf[k] = o;
        o += WX_LEAF_BOX * WX_LEAF_BOX;
    }
    for (int i = 0; i < g->nleaves; i++) {
        float cx = g->mount.x + 2, cy = g->mount.y + g->leaf[i].dy, h = WX_LEAF_BOX / 2 + 1;
        ax0 = fminf(ax0, cx - h);
        ax1 = fmaxf(ax1, cx + 140 + h);
        ay0 = fminf(ay0, cy - 10 - h);
        ay1 = fmaxf(ay1, cy + 6 + h);
    }
    l->area = even_box(ax0, ay0, ax1, ay1);
    l->total = o;
}

static bool in_quad(const wx_pt_t *p, float x, float y)
{
    bool in = false;
    for (int i = 0, j = 3; i < 4; j = i++)
        if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
    return in;
}

static void paint_sock(uint8_t *fill, uint8_t *dark, wx_box_t box, const wx_pt_t q[WX_WIND_QUADS][4], void (*yield)(void))
{
    float qb[WX_WIND_QUADS][4];
    for (int k = 0; k < WX_WIND_QUADS; k++) {
        qb[k][0] = qb[k][2] = q[k][0].x;
        qb[k][1] = qb[k][3] = q[k][0].y;
        for (int i = 1; i < 4; i++) {
            qb[k][0] = fminf(qb[k][0], q[k][i].x);
            qb[k][1] = fminf(qb[k][1], q[k][i].y);
            qb[k][2] = fmaxf(qb[k][2], q[k][i].x);
            qb[k][3] = fmaxf(qb[k][3], q[k][i].y);
        }
    }
    const int w = box_w(box), h = box_h(box);
    for (int y = 0; y < h; y++) {
        if (yield) yield();
        for (int x = 0; x < w; x++) {
            float fx = (float)(box.x0 + x), fy = (float)(box.y0 + y);
            int any = 0, odd = 0, near = 0;
            for (int k = 0; k < WX_WIND_QUADS; k++)
                if (fx + 1 >= qb[k][0] && fx <= qb[k][2] && fy + 1 >= qb[k][1] && fy <= qb[k][3]) near |= 1 << k;
            for (int s = 0; near && s < SOCK_SS * SOCK_SS; s++) {
                float sx = fx + ((float)(s % SOCK_SS) + 0.5f) / SOCK_SS, sy = fy + ((float)(s / SOCK_SS) + 0.5f) / SOCK_SS;
                for (int k = 0; k < WX_WIND_QUADS; k++) {
                    if (!(near >> k & 1) || !in_quad(q[k], sx, sy)) continue;
                    any++;
                    odd += k & 1;
                    break;
                }
            }
            int n = SOCK_SS * SOCK_SS;
            fill[y * w + x] = (uint8_t)(odd == n ? 0 : (any - odd) * 255 / (n - odd));
            dark[y * w + x] = (uint8_t)(odd * 255 / (SOCK_SS * SOCK_SS));
        }
    }
}

void wx_paint_sock(uint8_t *fill, uint8_t *dark, wx_box_t box, const wx_pt_t q[WX_WIND_QUADS][4])
{
    paint_sock(fill, dark, box, q, NULL);
}

void wx_paint_streak(uint8_t *a8, int level, int r, int w)
{
    const wx_wind_geo_t *g = wx_wind_geo(level);
    float ax = g->mount.x + g->row[r].x0 - (float)wx_wind_streak_x(level, r);
    float ay = g->mount.y + g->row[r].dy - (float)wx_wind_streak_y(level, r);
    memset(a8, 0, (size_t)w * WX_STREAK_H);
    for (int y = 0; y < WX_STREAK_H; y++)
        for (int x = 0; x < w; x++)
            a8[y * w + x] = (uint8_t)lroundf(cov(seg_d((float)x + 0.5f, (float)y + 0.5f, ax, ay, ax + g->row[r].len, ay) - 2) * 255);
}

#define LEAF_HW_N 33

static float leaf_hw(const float *hw, float u)
{
    if (u <= 0 || u >= 16) return 0;
    float f = u / 16 * (LEAF_HW_N - 1);
    int i = (int)f;
    return hw[i] + (hw[i + 1] - hw[i]) * (f - (float)i);
}

void wx_paint_leaf(uint8_t *a8, int rot)
{
    float hw[LEAF_HW_N];
    for (int i = 0; i < LEAF_HW_N; i++) {
        float u = 16.0f * (float)i / (LEAF_HW_N - 1), lo = 0, hi = 1;
        for (int it = 0; it < 24; it++) {
            float t = (lo + hi) / 2;
            if (12 * t + 12 * t * t - 8 * t * t * t < u) lo = t;
            else hi = t;
        }
        float t = (lo + hi) / 2;
        hw[i] = 15 * t * (1 - t);
    }
    const float a = (float)rot * (180.0f / WX_LEAF_ROTS) * DEG, c = cosf(a), s = sinf(a), m = WX_LEAF_BOX / 2.0f;
    for (int y = 0; y < WX_LEAF_BOX; y++)
        for (int x = 0; x < WX_LEAF_BOX; x++) {
            int hit = 0;
            for (int k = 0; k < SOCK_SS * SOCK_SS; k++) {
                float dx = (float)x + ((float)(k % SOCK_SS) + 0.5f) / SOCK_SS - m;
                float dy = (float)y + ((float)(k / SOCK_SS) + 0.5f) / SOCK_SS - m;
                float u = dx * c + dy * s + 8, v = -dx * s + dy * c;
                hit += fabsf(v) <= leaf_hw(hw, u);
            }
            a8[y * WX_LEAF_BOX + x] = (uint8_t)(hit * 255 / (SOCK_SS * SOCK_SS));
        }
}

static float ring_days_px(const void *ctx, float fx, float fy, float r2, uint16_t *col)
{
    (void)ctx;
    (void)fx;
    (void)fy;
    *col = wx_rgb565(WX_DAYS_TRACK_RGB);
    return cov(fabsf(sqrtf(r2) - WX_RING_R) - WX_DAYS_RING_HW);
}

void wx_paint_ring_days(wx_ring_t *r) { annulus(r, ring_days_px, NULL); }

typedef struct {
    wx_cloud_t cl;
    uint32_t fill, line;
} day_cloud_t;

static const day_cloud_t CLOUD_WHITE = {{false, 33, 3, {{13, 28, 7}, {21, 23, 8.5f}, {30, 28, 7}}}, 0xE6ECF2, 0xC4CCD4};
static const day_cloud_t CLOUD_GREY = {{false, 24, 3, {{10, 19, 7}, {19, 14, 9}, {29, 19, 7}}}, 0x8E96A0, 0xA8B0BA};
static const day_cloud_t CLOUD_LOW = {{false, 30, 3, {{10, 25, 7}, {19, 20, 9}, {29, 25, 7}}}, 0x8E96A0, 0xA8B0BA};
static const day_cloud_t CLOUD_DARK = {{false, 24, 3, {{10, 19, 7}, {19, 14, 9}, {29, 19, 7}}}, 0x555D68, 0x6A7480};
static const float BOLT[7][2] = {{21, 24}, {15, 32}, {19.5f, 32}, {17, 39}, {25, 30}, {20.5f, 30}, {23, 24}};

static uint32_t sun_rgb(float f)
{
    return f <= 0.6f ? lerp_rgb(0xFFE07A, 0xFFB547, f / 0.6f) : lerp_rgb(0xFFB547, 0xF08A3A, (f - 0.6f) / 0.4f);
}

static bool in_poly(const float (*p)[2], int n, float x, float y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if ((p[i][1] > y) != (p[j][1] > y) && x < (p[j][0] - p[i][0]) * (y - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0]) in = !in;
    return in;
}

static const day_cloud_t *icon_cloud(int icon)
{
    return icon == WX_ICON_PARTLY  ? &CLOUD_WHITE
           : icon == WX_ICON_CLOUD ? &CLOUD_LOW
           : icon == WX_ICON_STORM ? &CLOUD_DARK
                                   : &CLOUD_GREY;
}

const wx_cloud_t *wx_day_cloud(int icon) { return icon == WX_ICON_SUN ? NULL : &icon_cloud(icon)->cl; }

static void icon_px(wx_img_t *im, int idx, int icon, const wx_cloud_shape_t *sh, float x, float y)
{
    const uint16_t ray = wx_rgb565(0xFFB547);
    if (icon == WX_ICON_SUN) {
        for (int k = 0; k < 8; k++) {
            float a = (float)k * 45.0f * DEG, s = sinf(a), c = cosf(a);
            put(im, idx, ray, cov(seg_d(x, y, 20 + 14.5f * s, 20 - 14.5f * c, 20 + 18 * s, 20 - 18 * c) - 1.25f));
        }
        float d = hypotf(x - 20, y - 20);
        put(im, idx, wx_rgb565(sun_rgb(d / 11)), cov(d - 11));
        return;
    }
    if (icon == WX_ICON_PARTLY) {
        float d = hypotf(x - 14, y - 14);
        put(im, idx, wx_rgb565(sun_rgb(d / 9)), cov(d - 9));
    }
    if (icon == WX_ICON_RAIN)
        for (int k = 0; k < 3; k++)
            put(im, idx, wx_rgb565(WX_DAYS_POP_RGB), cov(seg_d(x, y, 13 + 7 * k, 28, 11 + 7 * k, 34) - 1.25f));
    const day_cloud_t *cl = icon_cloud(icon);
    float d = wx_cloud_sd(sh, x, y);
    put(im, idx, wx_rgb565(cl->fill), cov(d));
    put(im, idx, wx_rgb565(cl->line), cov(fabsf(d) - 1));
    if (icon == WX_ICON_FOG) {
        const uint16_t mist = wx_rgb565(0x9AA4B0);
        put(im, idx, mist, cov(seg_d(x, y, 9, 29, 31, 29) - 1.25f));
        put(im, idx, mist, cov(seg_d(x, y, 13, 34, 27, 34) - 1.25f));
    } else if (icon == WX_ICON_SNOW) {
        for (int k = 0; k < 3; k++) put(im, idx, wx_rgb565(0xD0DCE8), cov(hypotf(x - (13 + 7 * k), y - (k == 1 ? 34 : 30)) - 2));
    }
}

void wx_paint_day_icon(wx_img_t *im, int icon, float ox, float oy)
{
    int x0, y0, x1, y1;
    clip(im, ox - 1, oy - 1, ox + WX_DAYS_ICON + 1, oy + WX_DAYS_ICON + 1, &x0, &y0, &x1, &y1);
    const uint16_t bolt = wx_rgb565(0xF0D040);
    wx_cloud_shape_t sh;
    wx_cloud_shape(&icon_cloud(icon)->cl, &sh);
    for (int y = y0; y < y1; y++) {
        row(im);
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f - ox, fy = (float)(im->y0 + y) + 0.5f - oy;
            int idx = y * im->w + x;
            icon_px(im, idx, icon, &sh, fx, fy);
            if (icon != WX_ICON_STORM) continue;
            int in = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) in += in_poly(BOLT, 7, fx - 0.375f + 0.25f * sx, fy - 0.375f + 0.25f * sy);
            put(im, idx, bolt, in / 16.0f);
        }
    }
}

void wx_paint_day_row(wx_img_t *im, const wx_days_t *d, int i, const wx_days_layout_t *l)
{
    wx_paint_clear(im);
    const wx_days_cols_t *c = &l->c;
    const float cy = (float)l->y[i], hw = WX_DAYS_BAR_H / 2.0f, bx = (float)c->bar_x, bw = (float)c->bar_w;
    float s0, s1;
    wx_days_span(d, i, c->bar_w, &s0, &s1);
    s0 += bx;
    s1 += bx;
    const wx_days_row_t *r = &d->row[i];
    const uint16_t track = wx_rgb565(WX_DAYS_TRACK_RGB);
    int x0, y0, x1, y1;
    clip(im, bx - 1, cy - hw - 1, bx + bw + 1, cy + hw + 1, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        row(im);
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            int idx = y * im->w + x;
            put(im, idx, track, cov(seg_d(fx, fy, bx + hw, cy, bx + bw - hw, cy) - hw));
            float a = cov(seg_d(fx, fy, s0 + hw, cy, s1 - hw, cy) - hw);
            if (a <= 0) continue;
            float f = clamp01((fx - s0) / (s1 - s0));
            put(im, idx, wx_rgb565(wx_temp_rgb((float)r->lo + (float)(r->hi - r->lo) * f)), a);
        }
    }
    wx_paint_day_icon(im, r->icon, (float)c->icon_x, cy - WX_DAYS_ICON / 2.0f);
}

static void bake_days(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    const wx_days_t *d = &t->days;
    if (kind & WX_BAKE_RING) wx_paint_ring_days(b->ring);
    if (kind & WX_BAKE_HERO) out->hero = (wx_box_t){0, 0, 0, 0};
    if (!(kind & WX_BAKE_SCENE)) return;
    wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE, .yield = b->yield};
    wx_paint_clear(&bg);
    wx_paint_temp_dots(&bg, t);
    wx_ring_blit(b->ring, b->bg);
    clouds_none(out);
    wx_days_layout_t l;
    wx_days_layout(d, &l);
    int w = l.sprite.x1 - l.sprite.x0, h = l.sprite.y1 - l.sprite.y0;
    for (int i = 0; i < l.n; i++) {
        uint8_t *m = b->scene + l.at[i];
        wx_img_t im = {.px = (uint16_t *)m, .a = m + (size_t)w * h * 2, .w = w, .h = h, .x0 = l.sprite.x0,
                       .y0 = l.y[i] - WX_DAYS_ROW_UP, .yield = b->yield};
        wx_paint_day_row(&im, d, i, &l);
    }
}

void wx_paint_ring_sun(wx_ring_t *r, const wx_sun_t *s, uint16_t lut[WX_RING_LUT])
{
    long long a = wx_sun_start(s), b = wx_sun_end(s);
    for (int i = 0; i <= RING_STEPS; i++)
        lut[i] = wx_rgb565(s->have ? wx_sun_rgb(s, a + (long long)llround((double)(b - a) * i / RING_STEPS)) : 0x262626);
    ring_raster(r, lut);
}

void wx_paint_sun_scene(wx_img_t *im, const wx_sun_t *s)
{
    const uint16_t hz = wx_rgb565(0x9AA4B0), dash = wx_rgb565(0x5A5A5A), tick = wx_rgb565(WX_SUN_HZN_RGB);
    float tx[2][2], ty[2][2];
    for (int k = 0; k < 2; k++) {
        float a = (225.0f + 270.0f * wx_sun_frac(s, s->t[k ? WX_SUN_SET : WX_SUN_RISE])) * DEG;
        for (int e = 0; e < 2; e++) {
            float rr = e ? 210.0f : 199.0f;
            tx[k][e] = WX_FACE_C + rr * sinf(a);
            ty[k][e] = WX_FACE_C - rr * cosf(a);
        }
    }
    int x0, y0, x1, y1;
    clip(im, 120, WX_SUN_HZ_Y - 4, 346, WX_SUN_HZ_Y + 12, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        row(im);
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            int i = y * im->w + x;
            put(im, i, hz, cov(seg_d(fx, fy, 125, WX_SUN_HZ_Y, 341, WX_SUN_HZ_Y) - 2.25f));
            float d = fminf(seg_d(fx, fy, 168, WX_SUN_HZ_Y + 8, 208, WX_SUN_HZ_Y + 8), seg_d(fx, fy, 258, WX_SUN_HZ_Y + 8, 298, WX_SUN_HZ_Y + 8));
            put(im, i, dash, cov(d - 1.5f));
        }
    }
    for (int k = 0; s->have && k < 2; k++) {
        clip(im, fminf(tx[k][0], tx[k][1]) - 2, fminf(ty[k][0], ty[k][1]) - 2, fmaxf(tx[k][0], tx[k][1]) + 2,
             fmaxf(ty[k][0], ty[k][1]) + 2, &x0, &y0, &x1, &y1);
        for (int y = y0; y < y1; y++) {
            row(im);
            for (int x = x0; x < x1; x++) {
                float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
                put(im, y * im->w + x, tick, cov(seg_d(fx, fy, tx[k][0], ty[k][0], tx[k][1], ty[k][1]) - 1));
            }
        }
    }
}

static const float RAY_DIR[WX_SUN_RAYS][2] = {
    {-0.978148f, -0.207912f}, {-0.906308f, -0.422618f}, {-0.788011f, -0.615661f}, {-0.629320f, -0.777146f},
    {-0.438371f, -0.898794f}, {-0.224951f, -0.974370f}, {0.000000f, -1.000000f},  {0.224951f, -0.974370f},
    {0.438371f, -0.898794f},  {0.629320f, -0.777146f},  {0.788011f, -0.615661f},  {0.906308f, -0.422618f},
    {0.978148f, -0.207912f},
};

static void paint_sun_rays(uint8_t *a8, wx_box_t box, void (*yield)(void))
{
    const int w = box.x1 - box.x0, h = box.y1 - box.y0;
    const float cx = WX_SUN_CX, cy = WX_SUN_CY, r1 = WX_SUN_R + 12;
    memset(a8, 0, (size_t)w * h);
    for (int y = 0; y < h; y++) {
        if (yield) yield();
        float fy = (float)(box.y0 + y) + 0.5f;
        for (int x = 0; x < w; x++) {
            float fx = (float)(box.x0 + x) + 0.5f;
            float m = rim(fx, fy, HERO_FADE), rho = hypotf(fx - cx, fy - cy), g = rho / 170.0f;
            float ga = g <= 0.3f ? 0.55f : g >= 1 ? 0 : 0.55f * (1 - (g - 0.3f) / 0.7f);
            if (m <= 0 || ga <= 0 || rho < r1 - 3) continue;
            int k = (int)lroundf((atan2f(fy - cy, fx - cx) / DEG + 168.0f) * (WX_SUN_RAYS - 1) / 156.0f);
            if (k < 0 || k >= WX_SUN_RAYS) continue;
            float r2 = k % 2 ? 130.0f : 170.0f, dx = RAY_DIR[k][0], dy = RAY_DIR[k][1];
            float c = cov(seg_d(fx, fy, cx + r1 * dx, cy + r1 * dy, cx + r2 * dx, cy + r2 * dy) - (k % 2 ? 1.5f : 1.75f));
            put_a(&a8[y * w + x], c * ga * m);
        }
    }
}

void wx_paint_sun_rays(uint8_t *a8, wx_box_t box) { paint_sun_rays(a8, box, NULL); }

void wx_paint_sun(wx_img_t *im)
{
    const uint16_t gold = wx_rgb565(WX_SUN_GOLD_RGB);
    const float inner = WX_SUN_R / WX_SUN_GLOW;
    wx_paint_clear(im);
    for (int y = 0; y < im->h; y++) {
        row(im);
        float fy = (float)(im->y0 + y) + 0.5f;
        if (fy >= WX_SUN_CLIP_Y) continue;
        for (int x = 0; x < im->w; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, d = hypotf(fx - WX_SUN_CX, fy - WX_SUN_CY);
            int i = y * im->w + x;
            float g = d / WX_SUN_GLOW;
            put(im, i, gold, g <= inner ? 0.24f : g >= 1 ? 0 : 0.24f * (1 - (g - inner) / (1 - inner)));
            put(im, i, wx_rgb565(sun_rgb(d / WX_SUN_R)), 0.9f * cov(d - WX_SUN_R));
            put(im, i, gold, 0.9f * cov(fabsf(d - WX_SUN_R) - 0.75f));
        }
    }
}

static void bake_sun(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    const wx_sun_t *s = &t->sun;
    if (kind & WX_BAKE_RING) wx_paint_ring_sun(b->ring, s, b->lut);
    if (kind & WX_BAKE_HERO) out->hero = (wx_box_t){0, 0, 0, 0};
    if (!(kind & WX_BAKE_SCENE)) return;
    wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE, .yield = b->yield};
    wx_paint_clear(&bg);
    wx_paint_sun_scene(&bg, s);
    wx_paint_temp_dots(&bg, t);
    wx_ring_blit(b->ring, b->bg);
    clouds_none(out);
    wx_sun_layout_t l;
    wx_sun_layout(&l);
    paint_sun_rays(b->scene + l.rays_at, l.rays, b->yield);
    int w = l.sun.x1 - l.sun.x0, h = l.sun.y1 - l.sun.y0;
    uint8_t *m = b->scene + l.sun_at;
    wx_img_t im = {.px = (uint16_t *)m, .a = m + (size_t)w * h * 2, .w = w, .h = h, .x0 = l.sun.x0, .y0 = l.sun.y0,
                   .yield = b->yield};
    wx_paint_sun(&im);
}

typedef struct {
    float px, py, tx, ty, c, s, k;
} xf_t;

void wx_deg_cs(float deg, float *c, float *s)
{
    float d = fmodf(deg, 360.0f);
    if (d < 0) d += 360.0f;
    int k = (int)((d + 45.0f) / 90.0f), q = k & 3;
    float x = (d - 90.0f * (float)k) * DEG, x2 = x * x;
    float sn = x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
    float cn = 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56)));
    *c = q == 0 ? cn : q == 1 ? -sn : q == 2 ? -cn : sn;
    *s = q == 0 ? sn : q == 1 ? cn : q == 2 ? -sn : -cn;
}

static xf_t xf_rot(float px, float py, float deg)
{
    xf_t f = {px, py, px, py, 1, 0, 1};
    wx_deg_cs(deg, &f.c, &f.s);
    return f;
}

static void xf_in(const xf_t *f, int n, float *x, float *y)
{
    for (int i = 0; i < n; i++) {
        float dx = (*x - f[i].tx) / f[i].k, dy = (*y - f[i].ty) / f[i].k;
        *x = f[i].px + dx * f[i].c + dy * f[i].s;
        *y = f[i].py - dx * f[i].s + dy * f[i].c;
    }
}

static void xf_out(const xf_t *f, int n, float *x, float *y)
{
    for (int i = n - 1; i >= 0; i--) {
        float dx = *x - f[i].px, dy = *y - f[i].py;
        *x = f[i].tx + f[i].k * (dx * f[i].c - dy * f[i].s);
        *y = f[i].ty + f[i].k * (dx * f[i].s + dy * f[i].c);
    }
}

static float xf_k(const xf_t *f, int n)
{
    float k = 1;
    for (int i = 0; i < n; i++) k *= f[i].k;
    return k;
}

typedef float (*sd_fn)(const void *ctx, float x, float y);

typedef struct {
    uint32_t fill, line;
    bool has_fill, has_line;
    float hw, alpha;
    uint32_t (*shade)(float x, float y);
} ink_t;

static void shape(wx_img_t *im, const xf_t *f, int n, float x0, float y0, float x1, float y1, sd_fn sd, const void *ctx,
                  const ink_t *ink)
{
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for (int i = 0; i < 4; i++) {
        float x = i & 1 ? x1 : x0, y = i & 2 ? y1 : y0;
        xf_out(f, n, &x, &y);
        bx0 = fminf(bx0, x);
        by0 = fminf(by0, y);
        bx1 = fmaxf(bx1, x);
        by1 = fmaxf(by1, y);
    }
    const float k = xf_k(f, n), pad = (ink->has_line ? ink->hw : 0) * k + 1;
    int cx0, cy0, cx1, cy1;
    clip(im, bx0 - pad, by0 - pad, bx1 + pad, by1 + pad, &cx0, &cy0, &cx1, &cy1);
    const uint16_t fill = wx_rgb565(ink->fill), line = wx_rgb565(ink->line);
    for (int y = cy0; y < cy1; y++) {
        row(im);
        for (int x = cx0; x < cx1; x++) {
            float lx = (float)(im->x0 + x) + 0.5f, ly = (float)(im->y0 + y) + 0.5f;
            xf_in(f, n, &lx, &ly);
            float d = sd(ctx, lx, ly) * k;
            int i = y * im->w + x;
            if (ink->has_fill) put(im, i, ink->shade ? wx_rgb565(ink->shade(lx, ly)) : fill, ink->alpha * cov(d));
            if (ink->has_line) put(im, i, line, ink->alpha * cov(fabsf(d) - ink->hw * k));
        }
    }
}

typedef struct {
    float cx, cy, rx, ry;
} ell_t;

static float ell_sd(const void *ctx, float x, float y)
{
    const ell_t *e = ctx;
    float ux = (x - e->cx) / e->rx, uy = (y - e->cy) / e->ry, vx = ux / e->rx, vy = uy / e->ry;
    float k0 = sqrtf(ux * ux + uy * uy), k1 = sqrtf(vx * vx + vy * vy);
    return k1 > 1e-6f ? k0 * (k0 - 1) / k1 : -fminf(e->rx, e->ry);
}

typedef struct {
    int n;
    const float (*p)[2];
    bool closed;
} poly_t;

static float poly_sd(const void *ctx, float x, float y)
{
    const poly_t *q = ctx;
    float d = 1e9f;
    bool in = false;
    for (int i = 0, j = q->n - 1; i < q->n; j = i++) {
        if (i || q->closed) d = fminf(d, seg_d(x, y, q->p[j][0], q->p[j][1], q->p[i][0], q->p[i][1]));
        if (q->closed && (q->p[i][1] > y) != (q->p[j][1] > y) &&
            x < (q->p[j][0] - q->p[i][0]) * (y - q->p[i][1]) / (q->p[j][1] - q->p[i][1]) + q->p[i][0])
            in = !in;
    }
    return in ? -d : d;
}

static void box_of(const float (*p)[2], int n, float *x0, float *y0, float *x1, float *y1)
{
    *x0 = *y0 = 1e9f;
    *x1 = *y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        *x0 = fminf(*x0, p[i][0]);
        *y0 = fminf(*y0, p[i][1]);
        *x1 = fmaxf(*x1, p[i][0]);
        *y1 = fmaxf(*y1, p[i][1]);
    }
}

static void paint_poly(wx_img_t *im, const xf_t *f, int n, const poly_t *q, const ink_t *ink)
{
    float x0, y0, x1, y1;
    box_of(q->p, q->n, &x0, &y0, &x1, &y1);
    shape(im, f, n, x0, y0, x1, y1, poly_sd, q, ink);
}

#define BEZ_STEPS 24

static void bez_at(const float (*b)[2], float t, float *x, float *y)
{
    float u = 1 - t, w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
    *x = w0 * b[0][0] + w1 * b[1][0] + w2 * b[2][0] + w3 * b[3][0];
    *y = w0 * b[0][1] + w1 * b[1][1] + w2 * b[2][1] + w3 * b[3][1];
}

static float bez_sd(const void *ctx, float x, float y)
{
    const float (*b)[2] = ctx;
    float px = b[0][0], py = b[0][1], d = 1e9f;
    for (int i = 1; i <= BEZ_STEPS; i++) {
        float nx, ny;
        bez_at(b, (float)i / BEZ_STEPS, &nx, &ny);
        d = fminf(d, seg_d(x, y, px, py, nx, ny));
        px = nx;
        py = ny;
    }
    return d;
}

typedef struct {
    float len, wid;
} lens_t;

static float lens_sd(const void *ctx, float x, float y)
{
    const lens_t *l = ctx;
    float s = x / l->len, dh = 1.5f * l->wid * (1 - 2 * s) / l->len;
    return (fabsf(y) - 1.5f * l->wid * s * (1 - s)) / sqrtf(1 + dh * dh);
}

static void paint_ellipse(wx_img_t *im, const xf_t *f, int n, float cx, float cy, float rx, float ry, const ink_t *ink)
{
    ell_t e = {cx, cy, rx, ry};
    shape(im, f, n, cx - rx, cy - ry, cx + rx, cy + ry, ell_sd, &e, ink);
}

static void stroke(wx_img_t *im, const xf_t *f, int n, float ax, float ay, float bx, float by, float w, uint32_t rgb)
{
    const float p[2][2] = {{ax, ay}, {bx, by}};
    poly_t q = {2, p, false};
    ink_t ink = {.line = rgb, .has_line = true, .hw = w / 2, .alpha = 1};
    paint_poly(im, f, n, &q, &ink);
}

static void stem(wx_img_t *im, const xf_t *f, int n, const float b[4][2], uint32_t rgb)
{
    float x0, y0, x1, y1;
    box_of(b, 4, &x0, &y0, &x1, &y1);
    ink_t ink = {.line = rgb, .has_line = true, .hw = 2.5f, .alpha = 1};
    shape(im, f, n, x0, y0, x1, y1, bez_sd, b, &ink);
}

static void leaf(wx_img_t *im, const xf_t *f, int n, float x, float y, float deg, float len, float wid, uint32_t fill, uint32_t line,
                 bool rib)
{
    xf_t g[3];
    for (int i = 0; i < n; i++) g[i] = f[i];
    g[n] = (xf_t){0, 0, x, y, 1, 0, 1};
    wx_deg_cs(deg, &g[n].c, &g[n].s);
    lens_t l = {len, wid};
    ink_t ink = {.fill = fill, .line = line, .has_fill = true, .has_line = true, .hw = 1.5f, .alpha = 1};
    shape(im, g, n + 1, 0, -0.375f * wid, len, 0.375f * wid, lens_sd, &l, &ink);
    if (rib) stroke(im, g, n + 1, 4, 0, len - 6, 0, 2, line);
}

static void disc_ink(wx_img_t *im, const xf_t *f, int n, float cx, float cy, float r, uint32_t fill, uint32_t line, float lw)
{
    ink_t ink = {.fill = fill, .line = line, .has_fill = true, .has_line = lw > 0, .hw = lw / 2, .alpha = 1};
    paint_ellipse(im, f, n, cx, cy, r, r, &ink);
}

static uint32_t petal_shade(float x, float y)
{
    return lerp_rgb(0xF59AB0, 0xE87A96, (hypotf(x - 233, y - 112) / 40.0f - 0.3f) / 0.7f);
}

#define PETAL_RGB 0xC98A9A
#define PETAL_LINE 0xD9A9B4

static void petal(wx_img_t *im, const xf_t *f, int n, float cx, float cy, float rx, float ry, float deg, float px, float py,
                  const ink_t *ink)
{
    xf_t g[3];
    for (int i = 0; i < n; i++) g[i] = f[i];
    g[n] = xf_rot(px, py, deg);
    paint_ellipse(im, g, n + 1, cx, cy, rx, ry, ink);
}

static void flower_healthy(wx_img_t *im)
{
    const float st[4][2] = {{233, 204}, {230, 176}, {237, 150}, {233, 126}};
    stem(im, NULL, 0, st, 0x4E8F45);
    leaf(im, NULL, 0, 233, 188, -22, 40, 20, 0x7BC86C, 0x5FAE55, true);
    leaf(im, NULL, 0, 233, 170, -158, 34, 18, 0x7BC86C, 0x5FAE55, true);
    ink_t ink = {.line = 0xF4B6C6, .has_fill = true, .has_line = true, .hw = 1.25f, .alpha = 1, .shade = petal_shade};
    for (int k = 0; k < 6; k++) petal(im, NULL, 0, 233, 93, 12.5f, 20, 60.0f * k, 233, 112, &ink);
    disc_ink(im, NULL, 0, 233, 112, 11, 0xF5C84A, 0xE3AE36, 2);
}

static const float DUST[6][3] = {{166, 128, 3}, {182, 166, 3.5f}, {196, 108, 2.5f}, {302, 176, 3}, {276, 90, 2.5f}, {156, 188, 2.5f}};
static const float FALLEN[3][3] = {{188, 202, 72}, {266, 203, 104}, {284, 201, 60}};

static void flower_wilted(wx_img_t *im)
{
    stroke(im, NULL, 0, 178, 206, 292, 206, 2, 0x2E3236);
    ink_t pe = {.fill = PETAL_RGB, .line = PETAL_LINE, .has_fill = true, .has_line = true, .hw = 0.75f, .alpha = 1};
    for (int i = 0; i < 3; i++) petal(im, NULL, 0, FALLEN[i][0], FALLEN[i][1], 4.5f, 8, FALLEN[i][2], FALLEN[i][0], FALLEN[i][1], &pe);
    xf_t g[2] = {xf_rot(226, 204, 18), xf_rot(226, 122, 32)};
    const float st[4][2] = {{226, 204}, {222, 176}, {230, 150}, {226, 132}};
    stem(im, g, 1, st, 0x7C8A50);
    leaf(im, g, 1, 226, 188, 20, 36, 17, 0x9CA35A, 0x7F8646, true);
    leaf(im, g, 1, 226, 172, 165, 30, 15, 0x9CA35A, 0x7F8646, true);
    ink_t head = {.fill = PETAL_RGB, .line = PETAL_LINE, .has_fill = true, .has_line = true, .hw = 1.25f, .alpha = 1};
    static const int KEPT[4] = {0, 2, 3, 5};
    for (int k = 0; k < 4; k++) petal(im, g, 2, 226, 104, 11.5f, 18, 60.0f * KEPT[k], 226, 122, &head);
    disc_ink(im, g, 2, 226, 122, 10, 0xC9A84A, 0xA98A3A, 2);
    for (int i = 0; i < 6; i++) {
        ink_t dust = {.fill = 0x8A8A8A, .has_fill = true, .alpha = 0.6f};
        paint_ellipse(im, NULL, 0, DUST[i][0], DUST[i][1], DUST[i][2], DUST[i][2], &dust);
    }
    static const float SIGN[3][2] = {{318, 108}, {340, 146}, {296, 146}};
    poly_t tri = {3, SIGN, true};
    ink_t sign = {.fill = 0, .line = 0xFF5050, .has_fill = true, .has_line = true, .hw = 2, .alpha = 1};
    paint_poly(im, NULL, 0, &tri, &sign);
    stroke(im, NULL, 0, 318, 121, 318, 133, 4, 0xFF5050);
    disc_ink(im, NULL, 0, 318, 139, 2.5f, 0xFF5050, 0, 0);
}

static void air_ends(wx_img_t *im)
{
    xf_t fl = {116, 383, 116, 383, 1, 0, 1.2f};
    ink_t outline = {.line = 0x4FD6CC, .has_line = true, .hw = 1.25f, .alpha = 1};
    for (int k = 0; k < 5; k++) petal(im, &fl, 1, 116, 376, 3.5f, 6, 72.0f * k, 116, 383, &outline);
    disc_ink(im, &fl, 1, 116, 383, 3, 0xF0E641, 0, 0);
    xf_t hz = {352, 386, 349, 383, 1, 0, 1.2f};
    static const float HAZARD[3][2] = {{352, 368}, {370, 399}, {334, 399}};
    poly_t tri = {3, HAZARD, true};
    ink_t edge = {.line = 0xD03060, .has_line = true, .hw = 1.75f, .alpha = 1};
    paint_poly(im, &hz, 1, &tri, &edge);
    stroke(im, &hz, 1, 352, 378, 352, 388, 3.5f, 0xD03060);
    disc_ink(im, &hz, 1, 352, 394, 2, 0xD03060, 0, 0);
}

void wx_paint_ring_air(wx_ring_t *r, bool grey, uint16_t lut[WX_RING_LUT])
{
    for (int i = 0; i <= RING_STEPS; i++)
        lut[i] = wx_rgb565(grey ? 0x262626 : wx_air_rgb((int)lroundf((float)WX_AIR_GAUGE * (float)i / RING_STEPS)));
    ring_raster(r, lut);
}

void wx_paint_air_scene(wx_img_t *im, bool wilted)
{
    air_ends(im);
    if (wilted) flower_wilted(im);
    else flower_healthy(im);
}

void wx_paint_petal(wx_img_t *im, int rot)
{
    wx_paint_clear(im);
    im->x0 = WX_PETAL_X - WX_PETAL_BOX / 2;
    im->y0 = WX_PETAL_Y - WX_PETAL_BOX / 2;
    ink_t pe = {.fill = PETAL_RGB, .line = PETAL_LINE, .has_fill = true, .has_line = true, .hw = 0.75f, .alpha = 1};
    petal(im, NULL, 0, WX_PETAL_X, WX_PETAL_Y, 4.5f, 8, 86.0f - 50.0f + 10.0f * (float)rot, WX_PETAL_X, WX_PETAL_Y, &pe);
}

static void bake_air(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    const wx_air_t *a = &t->air;
    if (kind & WX_BAKE_RING) wx_paint_ring_air(b->ring, !a->have, b->lut);
    if (kind & WX_BAKE_HERO) out->hero = (wx_box_t){0, 0, 0, 0};
    if (!(kind & WX_BAKE_SCENE)) return;
    wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE, .yield = b->yield};
    wx_paint_clear(&bg);
    wx_ring_blit(b->ring, b->bg);
    wx_paint_air_scene(&bg, a->wilted);
    wx_paint_temp_dots(&bg, t);
    clouds_none(out);
    for (int r = 0; a->wilted && r < WX_PETAL_ROTS; r++) {
        uint8_t *m = b->scene + r * WX_PETAL_BOX * WX_PETAL_BOX * 3;
        wx_img_t im = {.px = (uint16_t *)m, .a = m + WX_PETAL_BOX * WX_PETAL_BOX * 2, .w = WX_PETAL_BOX, .h = WX_PETAL_BOX,
                       .yield = b->yield};
        wx_paint_petal(&im, r);
    }
}
