#include "music_moment.h"

#include <math.h>
#include <string.h>

static uint32_t fnv(uint32_t h, const char *s)
{
    for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return (h ^ 0xFFu) * 16777619u;
}

uint32_t mm_track_key(const char *title, const char *artist, const char *album, const char *source)
{
    if (!title || !*title) return 0;
    uint32_t h = fnv(fnv(fnv(fnv(2166136261u, title), artist), album), source);
    return h ? h : 1;
}

void mm_init(mm_trigger_t *tr)
{
    memset(tr, 0, sizeof *tr);
    tr->last_at = -INFINITY;
    tr->stop_at = -1;
    tr->steady_at = INFINITY;
    tr->mood = -1;
}

bool mm_tick(mm_trigger_t *tr, const mm_in_t *in)
{
    double t = in->t;
    if (in->bot_mood != tr->mood || in->shown != tr->was_shown) tr->steady_at = t;
    tr->mood = in->bot_mood;
    tr->was_shown = in->shown;
    bool playing = in->have && in->playing && in->track;
    if (!in->have) {
        tr->base = false;
        tr->pending = 0;
        return false;
    }
    if (!tr->base) {
        tr->base = true;
        tr->cur = in->track;
        tr->stop_at = playing ? -1 : t;
        tr->pending = 0;
        return false;
    }
    if (!playing) {
        tr->pending = 0;
        if (tr->stop_at < 0) tr->stop_at = t;
        if (in->track) tr->cur = in->track;
        return false;
    }
    bool start = in->track != tr->cur || (tr->stop_at >= 0 && t - tr->stop_at >= MM_RESUME_S);
    tr->cur = in->track;
    tr->stop_at = -1;
    if (start) tr->pending = t - tr->last_at < MM_SAME_S ? 0 : in->track;
    if (!tr->pending) return false;
    return in->rest && in->shown && !in->blocked && t - tr->steady_at >= MM_SETTLE_S && t - in->other_at >= MM_GAP_S;
}

void mm_shown(mm_trigger_t *tr, double t)
{
    tr->pending = 0;
    tr->last_at = t;
}

bool mm_recent(const mm_trigger_t *tr, double t) { return t - tr->last_at < MM_GAP_S; }

static float bezier(float x1, float y1, float x2, float y2, float x)
{
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    float lo = 0, hi = 1, u = x;
    for (int i = 0; i < 24; i++) {
        u = (lo + hi) / 2;
        float bx = 3 * (1 - u) * (1 - u) * u * x1 + 3 * (1 - u) * u * u * x2 + u * u * u;
        if (bx < x) lo = u;
        else hi = u;
    }
    return 3 * (1 - u) * (1 - u) * u * y1 + 3 * (1 - u) * u * u * y2 + u * u * u;
}

static float arrive(float x) { return bezier(0.2f, 0.8f, 0.2f, 1, x); }
static float ease_out(float x) { return bezier(0, 0, 0.58f, 1, x); }
static float ease_in_out(float x) { return bezier(0.42f, 0, 0.58f, 1, x); }

static float beat(float u, float down, float up)
{
    if (u < 0.25f) return down * ease_in_out(u / 0.25f);
    if (u < 0.55f) return down + (up - down) * ease_in_out((u - 0.25f) / 0.3f);
    return up * (1 - ease_in_out((u - 0.55f) / 0.45f));
}

static const struct {
    int x, y, dx, start;
} NOTES[MM_NOTES] = {{107, 200, -16, 0}, {363, 196, 18, 48}, {121, 190, -8, 24}, {351, 186, 10, 12}};

static int phones_opa_in(int s) { return (int)lroundf(255 * arrive((s + 1) / 8.0f)); }
static int phones_opa_out(int s) { return (int)lroundf(255 * arrive(1 - (s + 1) / 6.0f)); }

void mm_frame(int j, mm_frame_t *f)
{
    memset(f, 0, sizeof *f);
    if (j < 0 || j >= MM_TICKS) return;
    int s = j / 2;
    if (j < MM_IN) {
        float e = arrive((s + 1) / 8.0f);
        f->phones_opa = phones_opa_in(s);
        f->phones_dy = -(int)lroundf(MM_DROP * (1 - e));
    } else if (j < MM_OUT) {
        f->phones_opa = 255;
        float u = (float)(((j - MM_IN) % MM_BEAT) / MM_BOB_STEP * MM_BOB_STEP) / MM_BEAT;
        f->eye_px = beat(u, 4, -5);
        f->cup_dy = (int)lroundf(beat(u, MM_CUP_BOB, -MM_CUP_BOB));
    } else {
        int g = (j - MM_OUT) / 2;
        float e = arrive(1 - (g + 1) / 6.0f);
        f->phones_opa = phones_opa_out(g);
        f->phones_dy = -(int)lroundf(MM_DROP * (1 - e));
    }
    if (j < MM_IN || j >= MM_OUT) return;
    int k = (j - MM_IN) & ~1;
    for (int i = 0; i < MM_NOTES; i++) {
        int a = k - NOTES[i].start;
        if (a < 0) continue;
        int c = a / MM_NOTE_CYCLE;
        int at = MM_IN + NOTES[i].start + c * MM_NOTE_CYCLE;
        if (at + MM_NOTE_CYCLE > MM_OUT) continue;
        float u = (float)(a % MM_NOTE_CYCLE) / MM_NOTE_CYCLE, e = ease_out(u);
        float o = u < 0.15f ? ease_out(u / 0.15f) : 1 - ease_out((u - 0.15f) / 0.85f);
        mm_note_t *n = &f->note[i];
        n->opa = (int)lroundf(255 * o);
        n->x = NOTES[i].x - MM_NOTE_OX + (int)lroundf(NOTES[i].dx * e);
        n->y = NOTES[i].y - MM_NOTE_OY - (int)lroundf(MM_NOTE_RISE * e);
    }
}

static void add(mm_box_t *b, bool *any, int x0, int y0, int x1, int y1)
{
    if (!*any) {
        *b = (mm_box_t){x0, y0, x1, y1};
        *any = true;
        return;
    }
    if (x0 < b->x0) b->x0 = x0;
    if (y0 < b->y0) b->y0 = y0;
    if (x1 > b->x1) b->x1 = x1;
    if (y1 > b->y1) b->y1 = y1;
}

static void phones_box(const mm_frame_t *f, mm_box_t *b, bool *any)
{
    if (f->phones_opa <= 0) return;
    add(b, any, MM_CUP_L_X, MM_BAND_Y + f->phones_dy, MM_CUP_R_X + MM_CUP_W - 1, MM_CUP_Y + MM_CUP_H - 1);
    add(b, any, MM_BOB_X0, MM_BOB_Y0, MM_BOB_X1, MM_BOB_Y1);
}

static void note_box(const mm_note_t *n, mm_box_t *b, bool *any)
{
    if (n->opa <= 0) return;
    add(b, any, n->x, n->y, n->x + MM_NOTE_W - 1, n->y + MM_NOTE_H - 1);
}

bool mm_diff(const mm_frame_t *a, const mm_frame_t *b, mm_box_t *box)
{
    static const mm_frame_t none;
    if (!a) a = &none;
    bool any = false;
    if (a->phones_dy != b->phones_dy || a->phones_opa != b->phones_opa) {
        phones_box(a, box, &any);
        phones_box(b, box, &any);
    }
    if (a->cup_dy != b->cup_dy || a->eye_px != b->eye_px) add(box, &any, MM_BOB_X0, MM_BOB_Y0, MM_BOB_X1, MM_BOB_Y1);
    for (int i = 0; i < MM_NOTES; i++) {
        const mm_note_t *p = &a->note[i], *q = &b->note[i];
        if (p->opa == q->opa && (q->opa <= 0 || (p->x == q->x && p->y == q->y))) continue;
        note_box(p, box, &any);
        note_box(q, box, &any);
    }
    if (!any) return false;
    box->x0 &= ~1;
    box->y0 &= ~1;
    box->x1 |= 1;
    box->y1 |= 1;
    return true;
}

void mm_extent(mm_box_t *box)
{
    bool any = false;
    for (int j = 0; j < MM_TICKS; j++) {
        mm_frame_t f;
        mm_frame(j, &f);
        phones_box(&f, box, &any);
        if (f.phones_opa > 0) {
            int cy = f.phones_dy + f.cup_dy;
            add(box, &any, MM_CUP_L_X, MM_CUP_Y + cy, MM_CUP_R_X + MM_CUP_W - 1, MM_CUP_Y + MM_CUP_H - 1 + cy);
        }
        for (int i = 0; i < MM_NOTES; i++) note_box(&f.note[i], box, &any);
    }
    box->x0 &= ~1;
    box->y0 &= ~1;
    box->x1 |= 1;
    box->y1 |= 1;
}

int mm_abort_tick(int j)
{
    if (j >= MM_OUT) return j;
    if (j < 0) return MM_TICKS;
    int opa = j < MM_IN ? phones_opa_in(j / 2) : 255;
    int g = 0;
    while (g < MM_OUT_LEN / 2 - 1 && phones_opa_out(g) > opa) g++;
    return MM_OUT + 2 * g;
}

float mm_eye_settle(float from, int ticks)
{
    int steps = ticks / MM_BOB_STEP;
    return steps >= MM_SETTLE_TICKS ? 0 : from * (float)(MM_SETTLE_TICKS - steps) / (MM_SETTLE_TICKS + 1);
}

static const mm_cue_t SCRIPT[] = {{0.0f, MM_HAPPY}, {(float)MM_OUT / MM_FPS, MM_END}};

const mm_cue_t *mm_script(int *n)
{
    *n = (int)(sizeof SCRIPT / sizeof SCRIPT[0]);
    return SCRIPT;
}

static float cover(float d)
{
    float c = 0.5f - d;
    return c < 0 ? 0 : c > 1 ? 1 : c;
}

static float rrect_sd(float px, float py, float x, float y, float w, float h, float r)
{
    float qx = fabsf(px - (x + w / 2)) - (w / 2 - r), qy = fabsf(py - (y + h / 2)) - (h / 2 - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

static float seg_sd(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float h = (wx * vx + wy * vy) / (vx * vx + vy * vy);
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    float dx = wx - vx * h, dy = wy - vy * h;
    return sqrtf(dx * dx + dy * dy);
}

static void paint_band(uint8_t *a)
{
    const float r = MM_BAND_R, hw = MM_BAND_HW, cx = MM_BAND_CX, cy = MM_BAND_CY;
    const float lo = (r - hw - 1) * (r - hw - 1), hi = (r + hw + 1) * (r + hw + 1);
    for (int y = 0; y < MM_BAND_H; y++) {
        float py = MM_BAND_Y + y + 0.5f, dy = py - cy;
        for (int x = 0; x < MM_BAND_W; x++) {
            float px = MM_BAND_X + x + 0.5f, dx = px - cx, d;
            if (dy <= 0) {
                float d2 = dx * dx + dy * dy;
                if (d2 < lo || d2 > hi) {
                    a[y * MM_BAND_W + x] = 0;
                    continue;
                }
                d = fabsf(sqrtf(d2) - r) - hw;
            } else {
                float ex = fabsf(dx) - r;
                d = sqrtf(ex * ex + dy * dy) - hw;
            }
            a[y * MM_BAND_W + x] = (uint8_t)lroundf(255 * cover(d));
        }
    }
}

static uint16_t rgb565(float r, float g, float b)
{
    return (uint16_t)(((int)lroundf(r) >> 3) << 11 | ((int)lroundf(g) >> 2) << 5 | ((int)lroundf(b) >> 3));
}

static void paint_cup(uint8_t *a, bool left)
{
    uint16_t *c = (uint16_t *)a;
    uint8_t *al = a + MM_CUP_W * MM_CUP_H * 2;
    const float R = (MM_RGB >> 16) & 0xFF, G = (MM_RGB >> 8) & 0xFF, B = MM_RGB & 0xFF;
    float sx = left ? 8 : 20;
    for (int y = 0; y < MM_CUP_H; y++)
        for (int x = 0; x < MM_CUP_W; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float k = 1 - 0.25f * cover(rrect_sd(px, py, sx, 12, 8, 48, 4));
            c[y * MM_CUP_W + x] = rgb565(R * k, G * k, B * k);
            al[y * MM_CUP_W + x] = (uint8_t)lroundf(255 * cover(rrect_sd(px, py, 0, 0, MM_CUP_W, MM_CUP_H, 16)));
        }
}

static void paint_note(uint8_t *a)
{
    const float cs = cosf(20 * (float)M_PI / 180), sn = sinf(20 * (float)M_PI / 180);
    float qx[9], qy[9];
    for (int i = 0; i <= 8; i++) {
        float u = i / 8.0f;
        qx[i] = (1 - u) * (1 - u) * 5.5f + 2 * (1 - u) * u * 14 + u * u * 12;
        qy[i] = (1 - u) * (1 - u) * -22 + 2 * (1 - u) * u * -18 + u * u * -9;
    }
    for (int y = 0; y < MM_NOTE_H; y++)
        for (int x = 0; x < MM_NOTE_W; x++) {
            float px = x + 0.5f - MM_NOTE_OX, py = y + 0.5f - MM_NOTE_OY;
            float lx = px * cs - py * sn, ly = px * sn + py * cs;
            float ex = lx / 6.5f, ey = ly / 5.0f, k = sqrtf(ex * ex + ey * ey);
            float gx = lx / (6.5f * 6.5f), gy = ly / (5.0f * 5.0f), g = sqrtf(gx * gx + gy * gy);
            float d = g > 0 ? (k - 1) * k / g : -5;
            float s = seg_sd(px, py, 5.5f, -2, 5.5f, -22) - 1.5f;
            if (s < d) d = s;
            for (int i = 0; i < 8; i++) {
                s = seg_sd(px, py, qx[i], qy[i], qx[i + 1], qy[i + 1]) - 1.5f;
                if (s < d) d = s;
            }
            a[y * MM_NOTE_W + x] = (uint8_t)lroundf(255 * cover(d));
        }
}

void mm_paint(uint8_t *arena)
{
    paint_band(arena + MM_BAND_AT);
    paint_cup(arena + MM_CUP_AT(0), true);
    paint_cup(arena + MM_CUP_AT(1), false);
    paint_note(arena + MM_NOTE_AT);
}
