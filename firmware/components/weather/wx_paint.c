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

void wx_paint_ring(wx_img_t *im, int lo, int hi, bool grey, uint16_t lut[WX_RING_LUT])
{
    for (int i = 0; i <= RING_STEPS; i++)
        lut[i] = wx_rgb565(grey ? 0x262626 : wx_temp_rgb((float)lo + (float)(hi - lo) * i / RING_STEPS));
    float sx, sy, ex, ey;
    wx_gauge_xy(0, &sx, &sy);
    wx_gauge_xy(1, &ex, &ey);
    const float r_in = WX_RING_R - WX_RING_HW - 1, r_out = WX_RING_R + WX_RING_HW + 1;
    int x0, y0, x1, y1;
    clip(im, WX_FACE_C - r_out, WX_FACE_C - r_out, WX_FACE_C + r_out, WX_FACE_C + r_out, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        float fy = (float)(im->y0 + y) + 0.5f, dy = fy - WX_FACE_C;
        for (int x = x0; x < x1; x++) {
            float fx = (float)(im->x0 + x) + 0.5f, dx = fx - WX_FACE_C;
            float r2 = dx * dx + dy * dy;
            if (r2 < r_in * r_in || r2 > r_out * r_out) continue;
            float th = atan2f(dx, -dy) / DEG;
            float s = th - 225.0f;
            while (s < 0) s += 360.0f;
            float d;
            int li;
            if (s <= 270.0f) {
                d = fabsf(sqrtf(r2) - WX_RING_R) - WX_RING_HW;
                li = (int)lroundf(s * RING_STEPS / 270.0f);
            } else {
                float ds = hypotf(fx - sx, fy - sy), de = hypotf(fx - ex, fy - ey);
                d = (ds < de ? ds : de) - WX_RING_HW;
                li = ds < de ? 0 : RING_STEPS;
            }
            put(im, y * im->w + x, lut[li], cov(d));
        }
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
