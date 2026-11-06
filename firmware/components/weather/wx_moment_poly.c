#include "wx_moment_poly.h"

#include <math.h>

#include "wx_paint.h"

#define SS 4
#define MAX_PTS 32

static inline uint16_t mix565(uint16_t d, uint16_t s, float a)
{
    int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
    int r = dr + (int)lroundf((float)(sr - dr) * a), g = dg + (int)lroundf((float)(sg - dg) * a),
        b = db + (int)lroundf((float)(sb - db) * a);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void put(wxp_img_t *im, int i, uint16_t c, float a)
{
    if (!(a > 0.002f)) return;
    if (a > 1) a = 1;
    float da = im->a[i] / 255.0f;
    float oa = a + da * (1 - a);
    im->px[i] = da <= 0 ? c : mix565(im->px[i], c, a / oa);
    im->a[i] = (uint8_t)lroundf(oa * 255.0f);
}

static bool inside(const wxp_pt_t *p, int n, float x, float y)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
    return in;
}

void wxp_poly(wxp_img_t *im, const wxp_pt_t *p, int n, uint32_t rgb)
{
    const uint16_t c = wx_rgb565(rgb);
    float minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
    for (int i = 1; i < n; i++) {
        minx = fminf(minx, p[i].x), maxx = fmaxf(maxx, p[i].x);
        miny = fminf(miny, p[i].y), maxy = fmaxf(maxy, p[i].y);
    }
    int x0 = (int)floorf(minx) - im->x0, x1 = (int)ceilf(maxx) - im->x0;
    int y0 = (int)floorf(miny) - im->y0, y1 = (int)ceilf(maxy) - im->y0;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > im->w) x1 = im->w;
    if (y1 > im->h) y1 = im->h;
    for (int y = y0; y < y1; y++) {
        if (im->yield) im->yield();
        for (int x = x0; x < x1; x++) {
            int hit = 0;
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++)
                    hit += inside(p, n, (float)(im->x0 + x) + (sx + 0.5f) / SS, (float)(im->y0 + y) + (sy + 0.5f) / SS);
            put(im, y * im->w + x, c, (float)hit / (SS * SS));
        }
    }
}

void wxp_rect(wxp_img_t *im, float x0, float y0, float x1, float y1, uint32_t rgb)
{
    const wxp_pt_t p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    wxp_poly(im, p, 4, rgb);
}

void wxp_rrect(wxp_img_t *im, float x0, float y0, float x1, float y1, float r, uint32_t rgb)
{
    wxp_pt_t p[MAX_PTS];
    int n = 0;
    const float cx[4] = {x1 - r, x1 - r, x0 + r, x0 + r}, cy[4] = {y0 + r, y1 - r, y1 - r, y0 + r};
    for (int q = 0; q < 4; q++)
        for (int i = 0; i <= 4; i++) {
            float a = ((float)q * 90.0f - 90.0f + (float)i * 22.5f) * 0.0174532925f;
            p[n++] = (wxp_pt_t){cx[q] + r * cosf(a), cy[q] + r * sinf(a)};
        }
    wxp_poly(im, p, n, rgb);
}

void wxp_disc(wxp_img_t *im, float cx, float cy, float r, uint32_t rgb)
{
    wxp_pt_t p[24];
    for (int i = 0; i < 24; i++) {
        float a = (float)i * 6.2831853f / 24.0f;
        p[i] = (wxp_pt_t){cx + r * cosf(a), cy + r * sinf(a)};
    }
    wxp_poly(im, p, 24, rgb);
}

