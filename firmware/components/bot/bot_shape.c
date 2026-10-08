#include "bot_shape.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define EXT_RAM_BSS_ATTR
#endif

#define REST_X 0.67
#define TRI_TABLE 720

static EXT_RAM_BSS_ATTR double s_tri[TRI_TABLE];
static bool s_tri_ready;

void bot_shape_init(void)
{
    if (s_tri_ready) return;
    const double R = 1.1, SAG = 0.11;
    static EXT_RAM_BSS_ATTR double raw[TRI_TABLE];
    double vx[3], vy[3];
    for (int k = 0; k < 3; k++) {
        double a = M_PI / 2 + k * 2 * M_PI / 3;
        vx[k] = R * cos(a); vy[k] = R * sin(a);
    }
    for (int i = 0; i < TRI_TABLE; i++) raw[i] = 0;
    for (int k = 0; k < 3; k++) {
        double ax = vx[k], ay = vy[k], bx = vx[(k + 1) % 3], by = vy[(k + 1) % 3];
        double c = hypot(bx - ax, by - ay), rho = (c * c / 4 + SAG * SAG) / (2 * SAG);
        double mx = (ax + bx) / 2, my = (ay + by) / 2, nl = hypot(mx, my), nx = mx / nl, ny = my / nl;
        for (int i = 0; i <= 2000; i++) {
            double t = i / 2000.0, x = (t - 0.5) * c;
            double off = sqrt(rho * rho - x * x) - (rho - SAG);
            double px = ax + (bx - ax) * t + nx * off, py = ay + (by - ay) * t + ny * off;
            double ang = atan2(py, px);
            if (ang < 0) ang += 2 * M_PI;
            int bin = (int)(ang / (2 * M_PI) * TRI_TABLE) % TRI_TABLE;
            double r = hypot(px, py);
            if (r > raw[bin]) raw[bin] = r;
        }
    }
    for (int i = 0; i < TRI_TABLE; i++) {
        if (raw[i] == 0) raw[i] = raw[(i + TRI_TABLE - 1) % TRI_TABLE];
    }
    double sig = 1.2 / 360.0 * TRI_TABLE;
    int w = (int)ceil(3 * sig);
    for (int i = 0; i < TRI_TABLE; i++) {
        double acc = 0, ws = 0;
        for (int d = -w; d <= w; d++) {
            double g = exp(-(double)(d * d) / (2 * sig * sig));
            acc += g * raw[(i + d + TRI_TABLE) % TRI_TABLE];
            ws += g;
        }
        s_tri[i] = acc / ws;
    }
    s_tri_ready = true;
}

static double tri_radius(double a)
{
    assert(s_tri_ready);
    double x = fmod(a / (2 * M_PI) * TRI_TABLE, TRI_TABLE);
    if (x < 0) x += TRI_TABLE;
    int i = (int)x;
    double f = x - i;
    return s_tri[i % TRI_TABLE] * (1 - f) + s_tri[(i + 1) % TRI_TABLE] * f;
}

void bot_body_ring(double k, double xs[BOT_RING_POINTS], double ys[BOT_RING_POINTS])
{
    for (int i = 0; i < BOT_RING_POINTS; i++) {
        double a = (double)i / BOT_RING_POINTS * 2 * M_PI;
        double r = 1 + (tri_radius(a) - 1) * k;
        xs[i] = r * cos(a);
        ys[i] = r * sin(a);
    }
}

#define ORBIT_LIMIT 0.9

static void capsule(bot_stroke_t *s, double cx, double cy, double w, double h, double angle)
{
    double half, dx, dy;
    if (h >= w) { half = (h - w) / 2; dx = -sin(angle); dy = cos(angle); s->width = w; }
    else        { half = (w - h) / 2; dx = cos(angle);  dy = sin(angle); s->width = h; }
    if (half < 0.002) half = 0.002;
    s->n = 2;
    s->x[0] = cx - dx * half; s->y[0] = cy - dy * half;
    s->x[1] = cx + dx * half; s->y[1] = cy + dy * half;
}

void bot_eye_strokes(const bot_pose_t *p, double scale, bot_stroke_t out[2])
{
    double reach = 0.6 - 0.2 * p->triangle;
    double cx = p->gaze_x * reach, cy = p->gaze_y * reach - 0.12 * p->triangle - 0.1 * p->slump;
    double limit = 0.52 - 0.15 * p->triangle, d = hypot(cx, cy);
    limit += (ORBIT_LIMIT - limit) * p->orbit;
    if (d > limit) { cx *= limit / d; cy *= limit / d; }
    double fx = sqrt(1 - 0.45 * cx * cx), fy = sqrt(1 - 0.45 * cy * cy);
    const double straight = 0.06;
    double lean = (fabs(cx) - straight) / (REST_X * 0.6 - straight);
    lean = lean < 0 ? 0 : (lean > 1 ? 1 : lean);
    double sep = (p->eyes == BOT_EYES_ROUND ? 0.5 : 0.44) * fx * (1 + (scale - 1) * 0.5);
    const double deg = M_PI / 180;

    for (int e = 0; e < 2; e++) {
        double side = e == 0 ? -1 : 1, lid = e == 0 ? p->lid_l : p->lid_r;
        double rise = p->eyes == BOT_EYES_DASH ? 0.04 * side * lean : 0;
        double ex = cx + side * sep / 2, ey = cy + rise, ffx = fx * scale, ffy = fy * scale;
#define L(a, b) ((a) + ((b) - (a)) * lid)
        bot_stroke_t *s = &out[e];
        switch (p->eyes) {
        case BOT_EYES_DASH:
            capsule(s, ex, ey, L(0.14, 0.24) * ffx, L(0.38, 0.06) * ffy, L(27, -6) * lean * deg);
            break;
        case BOT_EYES_ANGRY:
            capsule(s, ex, ey, L(0.13, 0.22) * ffx, L(0.3, 0.06) * ffy, L(55, 10) * deg * -side);
            break;
        case BOT_EYES_ROUND:
            capsule(s, ex, ey, L(0.3, 0.34) * ffx, L(0.44, 0.05) * ffy, 10 * lean * deg);
            break;
        case BOT_EYES_HAPPY: {
            double w = 0.3 * ffx, h = L(0.16, 0.03) * ffy;
            double x0 = ex - w / 2, y0 = ey - h / 2, qx = ex, qy = ey + h * 1.5, x1 = ex + w / 2, y1 = ey - h / 2;
            s->n = 7;
            for (int i = 0; i < s->n; i++) {
                double u = (double)i / (s->n - 1), v = 1 - u;
                s->x[i] = v * v * x0 + 2 * v * u * qx + u * u * x1;
                s->y[i] = v * v * y0 + 2 * v * u * qy + u * u * y1;
            }
            s->width = 0.11 * fmin(ffx, ffy);
            break;
        }
        }
#undef L
    }
}

double bot_eyes_extent(const bot_stroke_t eyes[2])
{
    double r = 0;
    for (int e = 0; e < 2; e++)
        for (int i = 0; i < eyes[e].n; i++) r = fmax(r, hypot(eyes[e].x[i], eyes[e].y[i]) + eyes[e].width / 2);
    return r;
}

double bot_orbit_gaze(const bot_pose_t *p, double eye_scale, double screen_deg, double outer)
{
    bot_pose_t q = *p;
    q.orbit = 1;
    double a = screen_deg * M_PI / 180, lo = 0, hi = 2;
    bot_stroke_t s[2];
    for (int i = 0; i < 30; i++) {
        double m = (lo + hi) / 2;
        q.gaze_x = cos(a) * m;
        q.gaze_y = -sin(a) * m;
        bot_eye_strokes(&q, eye_scale, s);
        if (bot_eyes_extent(s) > outer) hi = m;
        else lo = m;
    }
    return lo;
}
