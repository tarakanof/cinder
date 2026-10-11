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
    wx_img_t face = {.w = WX_FACE, .h = WX_FACE};
    int x0, y0, x1, y1;
    clip(&face, WX_FACE_C - r_out, WX_FACE_C - r_out, WX_FACE_C + r_out, WX_FACE_C + r_out, &x0, &y0, &x1, &y1);
    r->nrun = r->npx = 0;
    for (int y = y0; y < y1; y++) {
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
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            float a = cov(hypotf(fx - cx, fy - cy) - r) * alpha;
            if (fade_from > 0) a *= rim(fx, fy, fade_from);
            put(im, y * im->w + x, c, a);
        }
}

void wx_paint_face_dots(wx_img_t *im, int face, int n)
{
    for (int k = 0; k < n; k++) {
        float x, y;
        wx_face_dot_xy(k, n, &x, &y);
        disc(im, x, y, WX_DOT_R, wx_rgb565(k == face ? WX_DOT_ON : WX_DOT_OFF), 1, 0);
    }
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
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float d = thermo_d((float)(im->x0 + x) + 0.5f, (float)(im->y0 + y) + 0.5f);
            put(im, y * im->w + x, 0, cov(d));
            put(im, y * im->w + x, stroke, cov(fabsf(d) - 2.25f));
        }
}

static void sun(wx_img_t *im)
{
    const uint16_t halo = wx_rgb565(0xFFB547);
    for (int y = 0; y < im->h; y++)
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
    scale_alpha(im, 0.88f, HERO_FADE);
}

static void moon(wx_img_t *im, float cx, float cy, float r, float glow, int phase, int illum_pct)
{
    const float gr = roundf(r * 1.6f), il = illum_pct / 100.0f, sgn = phase < 50 ? 1.0f : -1.0f;
    const float rx = fabsf(1 - 2 * il) * r;
    const float gcx = cx - sgn * r * 0.35f, gcy = cy - r * 0.2f, grr = r * 1.2f;
    const uint16_t gc = wx_rgb565(0xAFC4E0);
    for (int y = 0; y < im->h; y++)
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
        int x0, y0, x1, y1;
        clip(im, bx0, by0, bx1, cl->base + 3, &x0, &y0, &x1, &y1);
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
                float d = 1e9f;
                for (int k = 0; k < cl->n; k++) d = fminf(d, hypotf(fx - cl->c[k].x, fy - cl->c[k].y) - cl->c[k].r);
                d = fmaxf(d, fy - cl->base);
                int idx = y * im->w + x;
                put(im, idx, fill, cov(d));
                put(im, idx, line, cov(fabsf(d) - 2.25f));
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
    for (int y = 0; y < im->h; y++)
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

void wx_paint_marker(wx_img_t *im)
{
    const float m = WX_MARKER / 2.0f;
    wx_paint_clear(im);
    const uint16_t ring = wx_rgb565(0xF4F4F2);
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++) {
            float d = hypotf((float)x + 0.5f - m, (float)y + 0.5f - m);
            int i = y * im->w + x;
            put(im, i, 0, cov(d - 17));
            put(im, i, ring, cov(fabsf(d - 10) - 2.5f));
        }
}

void wx_clouds_split(const wx_clouds_t *all, wx_clouds_t *still, wx_clouds_t *drift)
{
    *still = *all;
    *drift = *all;
    drift->n = 0;
    if (all->n <= 0) return;
    still->n = all->n - 1;
    drift->n = 1;
    drift->cl[0] = all->cl[all->n - 1];
}

static int box_px(wx_box_t b) { return (b.x1 - b.x0) * (b.y1 - b.y0); }

int wx_clouds_max_px(bool drift)
{
    int m = 0;
    for (int band = 1; band <= 4; band++) {
        wx_clouds_t all, s, d;
        wx_clouds_for(band, false, &all);
        wx_clouds_split(&all, &s, &d);
        int px = box_px(wx_clouds_box(drift ? &d : &s));
        if (px > m) m = px;
    }
    return m;
}

static wx_box_t layer(uint8_t *buf, const wx_clouds_t *c)
{
    wx_box_t b = wx_clouds_box(c);
    if (!c->n || b.x1 <= b.x0) return (wx_box_t){0, 0, 0, 0};
    int w = b.x1 - b.x0, h = b.y1 - b.y0;
    wx_img_t im = {.px = (uint16_t *)buf, .a = buf + (size_t)w * h * 2, .w = w, .h = h, .x0 = b.x0, .y0 = b.y0};
    wx_paint_clouds(&im, c);
    return b;
}

static void bake_rain(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    const wx_rain_t *r = &t->rain;
    if (kind & WX_BAKE_RING) wx_paint_ring_rain(b->ring, r->intensity, !r->has_pop, b->lut);
    if (kind & WX_BAKE_HERO) out->hero = (wx_box_t){0, 0, 0, 0};
    if (!(kind & WX_BAKE_SCENE)) return;
    wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE};
    wx_paint_clear(&bg);
    wx_paint_umbrella(&bg);
    wx_paint_face_dots(&bg, t->face, WXF_N);
    wx_ring_blit(b->ring, b->bg);
    out->still = out->drift = (wx_box_t){0, 0, 0, 0};
    wx_rain_layout_t l;
    wx_rain_layout(&l);
    uint8_t *m = b->still;
    for (int s = 0; s < WX_SHEETS; s++) {
        wx_paint_sheet(m + l.tile[s], s, r);
        wx_paint_ramp(m + l.top[s], s, false, l.ramp_h[s]);
        wx_paint_ramp(m + l.bottom[s], s, true, l.ramp_h[s]);
        wx_box_t c = l.cover_box[s];
        int w = c.x1 - c.x0, h = c.y1 - c.y0;
        wx_img_t cv = {.px = (uint16_t *)(m + l.cover[s]), .a = m + l.cover[s] + (size_t)w * h * 2, .w = w, .h = h,
                       .x0 = c.x0, .y0 = c.y0};
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
    wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE};
    wx_paint_clear(&bg);
    wx_paint_face_dots(&bg, t->face, WXF_N);
    wx_ring_blit(b->ring, b->bg);
    out->still = out->drift = (wx_box_t){0, 0, 0, 0};
    wx_cast_layout_t l;
    wx_cast_layout(c->variant, &l);
    for (int s = 0; s < l.sides; s++) {
        wx_clouds_t cl;
        wx_cast_clouds(c->variant, s, &cl);
        wx_box_t cb = l.cloud[s];
        int w = cb.x1 - cb.x0, h = cb.y1 - cb.y0;
        uint8_t *m = b->still + l.cloud_at[s];
        wx_img_t im = {.px = (uint16_t *)m, .a = m + (size_t)w * h * 2, .w = w, .h = h, .x0 = cb.x0, .y0 = cb.y0};
        wx_paint_clouds(&im, &cl);
        const wx_cast_sheet_t *sh = wx_cast_sheet(c->variant, s);
        wx_paint_cast_sheet(b->still + l.strip[s], sh, c->snow);
        wx_paint_cast_ramp(b->still + l.top[s], sh, false, l.ramp_h[s]);
        wx_paint_cast_ramp(b->still + l.bottom[s], sh, true, l.ramp_h[s]);
    }
}

static void bake_wind(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    const wx_wind_t *w = &t->wind;
    if (kind & WX_BAKE_RING) wx_paint_ring_wind(b->ring, !w->have, b->lut);
    if (kind & WX_BAKE_HERO) out->hero = (wx_box_t){0, 0, 0, 0};
    if (!(kind & WX_BAKE_SCENE)) return;
    wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE};
    wx_paint_clear(&bg);
    wx_paint_pole(&bg, w->level);
    wx_paint_face_dots(&bg, t->face, WXF_N);
    wx_ring_blit(b->ring, b->bg);
    out->still = out->drift = (wx_box_t){0, 0, 0, 0};
    wx_wind_layout_t l;
    wx_wind_layout(w->level, &l);
    for (int p = 0; p < WX_WIND_POSES; p++) {
        wx_pt_t q[WX_WIND_QUADS][4];
        for (int k = 0; k < WX_WIND_QUADS; k++) wx_wind_quad(w->level, p, k, q[k]);
        wx_paint_sock(b->still + l.fill[p], b->still + l.dark[p], l.sock, q);
    }
    for (int r = 0; r < l.nrows; r++) wx_paint_streak(b->still + l.streak[r], w->level, r, l.streak_w[r]);
    for (int k = 0; l.nleaves && k < WX_LEAF_ROTS; k++) wx_paint_leaf(b->still + l.leaf[k], k);
}

void wx_bake(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out)
{
    if (kind & WX_BAKE_SCENE) kind = WX_BAKE_FULL;
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
        wx_img_t bg = {.px = b->bg, .w = WX_FACE, .h = WX_FACE};
        wx_paint_clear(&bg);
        if (t->hero == WX_HERO_SUN) wx_paint_rays(&bg);
        if (t->stars) wx_paint_stars(&bg);
        wx_paint_thermo(&bg);
        wx_paint_face_dots(&bg, t->face, WXF_N);
        wx_ring_blit(b->ring, b->bg);
        wx_clouds_t all, still, drift;
        wx_clouds_for(t->cloud_band, t->night, &all);
        wx_clouds_split(&all, &still, &drift);
        out->still = layer(b->still, &still);
        out->drift = layer(b->drift, &drift);
    }
    if (kind & WX_BAKE_HERO) {
        out->hero = (wx_box_t){0, 0, 0, 0};
        if (t->hero != WX_HERO_NONE) {
            wx_img_t h = {.px = (uint16_t *)b->hero, .a = b->hero + WX_HERO_BOX * WX_HERO_BOX * 2, .w = WX_HERO_BOX,
                          .h = WX_HERO_BOX};
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

static float canopy_d(float x, float y)
{
    float cx = (CANOPY_L + CANOPY_R) / 2, rx = (CANOPY_R - CANOPY_L) / 2;
    float d = fmaxf(ellipse_d(x, y, cx, CANOPY_RIM, rx, CANOPY_RY), y - CANOPY_RIM);
    float w = (CANOPY_R - CANOPY_L) / SCALLOPS;
    for (int k = 0; k < SCALLOPS; k++) d = fmaxf(d, -ellipse_d(x, y, CANOPY_L + w * (k + 0.5f), CANOPY_RIM, w / 2, 9));
    return d;
}

static float handle_d(float x, float y)
{
    float d = seg_d(x, y, 233, 185, 233, 210.75f);
    d = fminf(d, seg_d(x, y, 233, 336.25f, 233, 366));
    float arc = y >= 366 ? fabsf(hypotf(x - 244, y - 366) - 11) : fminf(hypotf(x - 233, y - 366), hypotf(x - 255, y - 366));
    return fminf(d, arc);
}

void wx_paint_umbrella(wx_img_t *im)
{
    const uint16_t orange = wx_rgb565(0xFF8A3D);
    int x0, y0, x1, y1;
    clip(im, CANOPY_L - 4, 76, CANOPY_R + 4, 382, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f;
            int i = y * im->w + x;
            put(im, i, orange, cov(handle_d(fx, fy) - UMB_HW));
            float d = canopy_d(fx, fy);
            put(im, i, 0, cov(d));
            put(im, i, orange, cov(fabsf(d) - UMB_HW));
            put(im, i, orange, cov(seg_d(fx, fy, 233, 93, 233, 81) - UMB_HW));
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
    for (int y = 0; y < WX_DRIP_H; y++)
        for (int x = 0; x < WX_DRIP_W; x++) {
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float px = ((float)x + (sx + 0.5f) / 4 - ox) / sc, py = ((float)y + (sy + 0.5f) / 4 - oy) / sc + 203.0f;
                    if (py < 199 || py > 208) continue;
                    float lo = 0, hi = 1;
                    for (int it = 0; it < 24; it++) {
                        float t = (lo + hi) / 2, u = 1 - t;
                        float yt = 199 * u * u + 2 * u * t * 205 + 208 * t * t;
                        if (yt < py) lo = t;
                        else hi = t;
                    }
                    if (fabsf(px) <= drip_hw((lo + hi) / 2)) hit++;
                }
            a8[y * WX_DRIP_W + x] = (uint8_t)(hit * 255 / 16);
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
    for (int y = 0; y < im->h; y++)
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
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, fy = (float)(im->y0 + y) + 0.5f, d = 1e9f;
            for (int k = 0; k < 4; k++) d = fminf(d, rbox(fx, fy, B[k]));
            put(im, y * im->w + x, col, cov(d));
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

void wx_paint_sock(uint8_t *fill, uint8_t *dark, wx_box_t box, const wx_pt_t q[WX_WIND_QUADS][4])
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
    for (int y = 0; y < h; y++)
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
            fill[y * w + x] = (uint8_t)(any * 255 / (SOCK_SS * SOCK_SS));
            dark[y * w + x] = (uint8_t)(odd * 255 / (SOCK_SS * SOCK_SS));
        }
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
