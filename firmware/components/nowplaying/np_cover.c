#include "np_cover.h"

#include <math.h>
#include <string.h>

#define TWO_PI 6.283185307179586

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

typedef struct {
    float r, g, b;
} rgbf_t;

static rgbf_t unpack(uint32_t c) { return (rgbf_t){(float)((c >> 16) & 0xFF), (float)((c >> 8) & 0xFF), (float)(c & 0xFF)}; }

static rgbf_t lerp(rgbf_t a, rgbf_t b, float t)
{
    return (rgbf_t){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    rgbf_t m = lerp(unpack(a), unpack(b), t);
    return (uint32_t)(m.r + 0.5f) << 16 | (uint32_t)(m.g + 0.5f) << 8 | (uint32_t)(m.b + 0.5f);
}

static uint16_t pack565(rgbf_t c)
{
    int r = (int)(c.r + 0.5f), g = (int)(c.g + 0.5f), b = (int)(c.b + 0.5f);
    r = r > 255 ? 255 : r < 0 ? 0 : r;
    g = g > 255 ? 255 : g < 0 ? 0 : g;
    b = b > 255 ? 255 : b < 0 ? 0 : b;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void np_edge_style(np_edge_style_t *st, np_ring_mode_t mode, uint32_t col)
{
    memset(st, 0, sizeof *st);
    st->track = NP_COL_TRACK;
    st->head_fill = NP_COL_HEAD;
    st->head_stroke = col;
    st->glow = col;
    st->head = true;
    if (mode == NP_RING_VOLUME) {
        st->arc0 = 0xFFFFFF;
        st->arc1 = mix(0xFFFFFF, col, NP_VOL_TINT);
    } else {
        st->arc0 = st->arc1 = col;
    }
}

bool np_edge_style_eq(const np_edge_style_t *a, const np_edge_style_t *b)
{
    return a->arc0 == b->arc0 && a->arc1 == b->arc1 && a->track == b->track && a->head_fill == b->head_fill &&
           a->head_stroke == b->head_stroke && a->glow == b->glow && a->head == b->head;
}

void np_glow_a8(uint8_t *a)
{
    const float c = NP_GLOW_PX / 2.0f, k = 1.0f / (NP_GLOW_SIGMA * 1.41421356f);
    for (int y = 0; y < NP_GLOW_PX; y++)
        for (int x = 0; x < NP_GLOW_PX; x++) {
            float d = hypotf(x + 0.5f - c, y + 0.5f - c);
            float v = 0.5f * erfcf((d - NP_GLOW_R) * k);
            a[y * NP_GLOW_PX + x] = (uint8_t)(clamp01(v) * 255 + 0.5f);
        }
}

void np_edge_render(const np_ring_t *ring, uint16_t *buf, int w, int h, np_rect_t rect, float frac,
                    const np_edge_style_t *st, const uint8_t *glow)
{
    int x0 = rect.x < 0 ? 0 : rect.x, y0 = rect.y < 0 ? 0 : rect.y;
    int x1 = rect.x + rect.w > w ? w : rect.x + rect.w, y1 = rect.y + rect.h > h ? h : rect.y + rect.h;
    frac = clamp01(frac);
    const float in = NP_EDGE_IN, edge = ring->hw + 0.5f, end = (float)(TWO_PI * frac);
    float ex, ey, sx, sy;
    np_ring_point(ring, frac, &ex, &ey);
    np_ring_point(ring, 0, &sx, &sy);
    const int gx0 = (int)lroundf(ex) - NP_GLOW_PX / 2, gy0 = (int)lroundf(ey) - NP_GLOW_PX / 2;
    const rgbf_t track = unpack(st->track), a0 = unpack(st->arc0), a1 = unpack(st->arc1);
    const rgbf_t gcol = unpack(st->glow), stroke = unpack(st->head_stroke), fill = unpack(st->head_fill);
    const float ho = NP_HEAD_R + 0.5f, hi = NP_HEAD_R - NP_HEAD_STROKE + 0.5f;
    for (int y = y0; y < y1; y++) {
        float dy = y + 0.5f - ring->cy;
        float xi = dy * dy < in * in ? sqrtf(in * in - dy * dy) : -1;
        uint16_t *row = buf + (size_t)y * w;
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - ring->cx;
            if (xi > 0 && fabsf(dx) < xi) {
                int skip = (int)ceilf(ring->cx + xi - 0.5f);
                if (skip > x) x = skip - 1;
                continue;
            }
            float d = sqrtf(dx * dx + dy * dy);
            rgbf_t c = {0, 0, 0};
            float t = clamp01(edge - fabsf(d - ring->r));
            if (t > 0) {
                float a = 0, param = 0;
                if (frac >= 1) {
                    a = t;
                    float phi = atan2f(dx, -dy);
                    param = (phi < 0 ? phi + (float)TWO_PI : phi) / (float)TWO_PI;
                } else if (frac > 0) {
                    float phi = atan2f(dx, -dy);
                    if (phi < 0) phi += (float)TWO_PI;
                    float ce = clamp01(edge - hypotf(x + 0.5f - ex, y + 0.5f - ey));
                    float cs = clamp01(edge - hypotf(x + 0.5f - sx, y + 0.5f - sy));
                    if (phi <= end) {
                        a = t;
                        param = phi / (float)TWO_PI;
                    } else {
                        param = cs > ce ? 0 : frac;
                    }
                    if (ce > a) a = ce;
                    if (cs > a) a = cs;
                }
                rgbf_t arc = lerp(a0, a1, param);
                float k = t * (1 - a);
                c = (rgbf_t){track.r * k + arc.r * a, track.g * k + arc.g * a, track.b * k + arc.b * a};
            }
            if (st->head) {
                int gx = x - gx0, gy = y - gy0;
                if (glow && gx >= 0 && gy >= 0 && gx < NP_GLOW_PX && gy < NP_GLOW_PX) {
                    float g = glow[gy * NP_GLOW_PX + gx] * (NP_GLOW_OPA / 255.0f);
                    if (g > 0) c = lerp(c, gcol, g);
                }
                float dh = hypotf(x + 0.5f - ex, y + 0.5f - ey);
                if (dh < ho) {
                    c = lerp(c, stroke, clamp01(ho - dh));
                    c = lerp(c, fill, clamp01(hi - dh));
                }
            }
            row[x] = pack565(c);
        }
    }
}

bool np_edge_dirty(const np_ring_t *ring, float f0, float f1, int w, int h, np_rect_t *out)
{
    return np_ring_dirty_m(ring, f0, f1, NP_GLOW_PX / 2 + 2, w, h, out);
}

np_ring_mode_t np_ring_mode(const np_ctl_t *c, double now)
{
    return np_ctl_volume_active(c, now) && np_ctl_volume_shown(c, now) >= 0 ? NP_RING_VOLUME : NP_RING_PROGRESS;
}

np_face_state_t np_face_state(bool have, np_play_t shown, bool offline, bool cover_held)
{
    if (offline) return cover_held ? NPF_OFFLINE : NPF_OFFLINE_EMPTY;
    if (!have) return NPF_WAITING;
    if (shown == NP_NONE) return NPF_NOTHING;
    return shown == NP_PLAYING ? NPF_PLAYING : NPF_PAUSED;
}

bool np_face_live(np_face_state_t s) { return s == NPF_PLAYING || s == NPF_PAUSED; }

bool np_return_step(np_return_t *r, bool offline, double now, double min_s)
{
    if (offline) {
        if (!r->off && !r->back) r->off_t = now;
        r->off = true;
        r->back = false;
        return false;
    }
    if (r->off) {
        r->off = false;
        r->back = true;
        r->on_t = now;
    }
    if (!r->back || now - r->on_t < NP_RETURN_DWELL_S) return false;
    r->back = false;
    if (r->on_t - r->off_t < min_s || (r->woke && now - r->wake_t < NP_RETURN_CAP_S)) return false;
    r->woke = true;
    r->wake_t = now;
    return true;
}

void np_face_look(np_face_state_t s, np_ring_mode_t m, uint32_t accent, np_face_look_t *out)
{
    memset(out, 0, sizeof *out);
    out->ring = accent;
    out->time_col = 0xD6D6D6;
    switch (s) {
    case NPF_PAUSED:
        out->shade = 115;
        out->ring = NP_COL_PAUSED;
        out->pause_icon = true;
        out->avatar_grey = true;
        break;
    case NPF_OFFLINE:
        out->shade = 77;
        out->ring = NP_COL_OFFLINE;
        out->time_col = NP_COL_PAUSED;
        out->offline_badge = true;
        out->avatar_grey = true;
        break;
    default:
        break;
    }
    if (m == NP_RING_VOLUME && np_face_live(s)) {
        out->shade = 153;
        out->ring = accent;
        out->pause_icon = false;
    }
}

uint8_t np_scrim_opa(bool light, uint8_t shade)
{
    int want = light ? NP_SCRIM_STRONG : NP_SCRIM_LIGHT;
    if (shade >= want) return 0;
    return (uint8_t)((255 * (want - shade) + (255 - shade) / 2) / (255 - shade));
}

bool np_face_wants_cover(const np_info_t *np) { return np->state != NP_NONE && np->cover_version[0]; }

void np_downscale565(const uint16_t *src, int sp, uint16_t *dst, int dp)
{
    const float s = (float)sp / (float)dp;
    for (int j = 0; j < dp; j++) {
        float y0 = j * s, y1 = y0 + s;
        for (int i = 0; i < dp; i++) {
            float x0 = i * s, x1 = x0 + s, r = 0, g = 0, b = 0, wsum = 0;
            for (int y = (int)y0; y < sp && y < y1; y++) {
                float wy = fminf(y + 1, y1) - fmaxf(y, y0);
                if (wy <= 0) continue;
                for (int x = (int)x0; x < sp && x < x1; x++) {
                    float wx = fminf(x + 1, x1) - fmaxf(x, x0);
                    if (wx <= 0) continue;
                    uint16_t p = src[y * sp + x];
                    float wt = wx * wy;
                    r += ((p >> 11) & 31) * wt;
                    g += ((p >> 5) & 63) * wt;
                    b += (p & 31) * wt;
                    wsum += wt;
                }
            }
            if (wsum <= 0) wsum = 1;
            dst[j * dp + i] = (uint16_t)((int)(r / wsum + 0.5f) << 11 | (int)(g / wsum + 0.5f) << 5 | (int)(b / wsum + 0.5f));
        }
    }
}

typedef struct {
    float r, hw;
    uint32_t col;
} vinyl_ring_t;

static const vinyl_ring_t VINYL[] = {
    {122, 1, 0x2A2A2A}, {52, 0.75f, 0x1C1C1C}, {61, 0.75f, 0x1C1C1C},  {70, 0.75f, 0x1C1C1C}, {79, 0.75f, 0x1C1C1C},
    {88, 0.75f, 0x1C1C1C}, {97, 0.75f, 0x1C1C1C}, {106, 0.75f, 0x1C1C1C}, {115, 0.75f, 0x1C1C1C}, {40, 1, 0x2A2A2A},
};

void np_vinyl_render(uint16_t *buf, int w, int h, float cx, float cy)
{
    const int half = NP_VINYL_BOX / 2;
    int x0 = (int)floorf(cx) - half, y0 = (int)floorf(cy) - half;
    const float hl = 0.9505f, hw = 1.5f;
    for (int y = y0 < 0 ? 0 : y0; y < y0 + NP_VINYL_BOX && y < h; y++) {
        uint16_t *row = buf + (size_t)y * w;
        float dy = y + 0.5f - cy;
        for (int x = x0 < 0 ? 0 : x0; x < x0 + NP_VINYL_BOX && x < w; x++) {
            float dx = x + 0.5f - cx, d = sqrtf(dx * dx + dy * dy);
            rgbf_t c = {0, 0, 0};
            for (size_t i = 0; i < sizeof VINYL / sizeof VINYL[0]; i++) {
                float a = clamp01(VINYL[i].hw + 0.5f - fabsf(d - VINYL[i].r));
                if (a > 0) c = lerp(c, unpack(VINYL[i].col), a);
            }
            float dot = clamp01(4.5f - d);
            if (dot > 0) c = lerp(c, unpack(0x2A2A2A), dot);
            float band = clamp01(hw + 0.5f - fabsf(d - 100));
            if (band > 0) {
                float phi = atan2f(dx, -dy);
                if (phi < 0) phi += (float)TWO_PI;
                if (phi <= hl) c = lerp(c, unpack(0x3A3A3A), band);
                else if (phi >= (float)(TWO_PI / 2) && phi <= (float)(TWO_PI / 2) + hl) c = lerp(c, unpack(0x2E2E2E), band);
            }
            row[x] = pack565(c);
        }
    }
}

void np_grey565(const uint16_t *src, uint16_t *dst, int n, float k)
{
    for (int i = 0; i < n; i++) {
        uint16_t p = src[i];
        float r = ((p >> 11) & 31) * (255.0f / 31), g = ((p >> 5) & 63) * (255.0f / 63), b = (p & 31) * (255.0f / 31);
        float y = (0.299f * r + 0.587f * g + 0.114f * b) * k;
        dst[i] = pack565((rgbf_t){y, y, y});
    }
}

np_take_t np_art_take(const char *set_version, bool set_cover, const np_info_t *now, bool view_cover)
{
    if (set_cover != view_cover) return NP_TAKE_DROP;
    np_art_plan_t p;
    np_art_plan(now, &p);
    return strcmp(set_version, p.version) == 0 ? NP_TAKE_ADOPT : NP_TAKE_DROP;
}

bool np_face_kind_cover(bool known, const np_info_t *np, int current_cover)
{
    if (known && np->cover_version[0]) return true;
    if (known && (np->album_art || np->artist_art)) return false;
    return current_cover != 0;
}

float np_ease_out(float t)
{
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    const float x1 = 0.2f, y1 = 0.8f, x2 = 0.2f, y2 = 1.0f;
    float lo = 0, hi = 1, s = t;
    for (int i = 0; i < 24; i++) {
        s = (lo + hi) / 2;
        float u = 1 - s, x = 3 * u * u * s * x1 + 3 * u * s * s * x2 + s * s * s;
        if (x < t) lo = s;
        else hi = s;
    }
    float u = 1 - s;
    return 3 * u * u * s * y1 + 3 * u * s * s * y2 + s * s * s;
}

float np_sweep_k(double dt, double dur)
{
    if (dt <= 0) return 0;
    if (dt >= dur) return 1;
    double steps = floor(dur * NP_SWEEP_FPS);
    return np_ease_out((float)(floor(dt * NP_SWEEP_FPS) / steps));
}

void np_change_at(double dt, bool from_pic, np_change_t *out)
{
    static const uint8_t IN[] = {170, 85};
    memset(out, 0, sizeof *out);
    out->keep = 1;
    if (from_pic) {
        if (dt < NP_CHG_HOLD_S) return;
        int step = (int)((dt - NP_CHG_HOLD_S) / NP_CHG_STEP_S);
        if (step < 2) {
            out->fade = (uint8_t)(85 * (step + 1));
            out->keep = (2 - step) / 3.0f;
            return;
        }
        out->keep = 0;
        out->swap = true;
        dt -= NP_CHG_HOLD_S + 2 * NP_CHG_STEP_S;
        if (dt < NP_CHG_BLACK_S) {
            out->fade = 255;
            return;
        }
        dt -= NP_CHG_BLACK_S;
    }
    out->swap = true;
    int step = dt < 0 ? 0 : (int)(dt / NP_CHG_STEP_S);
    if (step < 2) out->fade = IN[step];
    else out->done = true;
}

uint8_t np_shade_mix(uint8_t shade, uint8_t fade) { return (uint8_t)(255 - (255 - shade) * (255 - fade) / 255); }
