#include <math.h>
#include <string.h>

#include "day.h"

static const int DIGIT_PX[] = {30, 36, 48, 60, 78, 96};
static const int WORD_PX[] = {24, 30, 48};
static const int CAP_PX[] = {24, 30};

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static int snap(float px, const int *set, int n)
{
    int best = set[0];
    for (int i = 1; i < n; i++)
        if (fabsf((float)set[i] - px) < fabsf((float)best - px)) best = set[i];
    return best;
}

static bool digits_only(const char *s)
{
    if (!*s) return false;
    for (; *s; s++)
        if (!strchr("0123456789:- ", *s)) return false;
    return true;
}

static uint32_t mix(uint32_t a, uint32_t b, float k)
{
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
        out |= (uint32_t)(ca + (cb - ca) * k + 0.5f) << s;
    }
    return out;
}

void day_panel_pose(const day_panel_t *p, float u, float fade, day_pose_t *out)
{
    const float th = DAY_DRUM_DEG * (float)M_PI / 180.0f;
    float a = clampf(u, -1.6f, 1.6f), d = fabsf(u), dd = d < 1.0f ? d : 1.0f;
    float side = u < 0 ? 1.0f - 0.45f * dd : 1.0f;
    float op = side * clampf((1.6f - d) / 0.5f, 0, 1) * clampf(fade, 0, 1);
    int big = p->big ? p->big : 96;
    float gap0 = big == 48 ? 36.0f : 44.0f;
    out->val_y = DAY_DRUM_Y + (int)lroundf((float)DAY_DRUM_SLOT / sinf(th) * sinf(a * th));
    out->val_px = digits_only(p->val) ? snap((float)big + (30.0f - (float)big) * dd, DIGIT_PX, 6)
                                      : snap((float)big + (30.0f - (float)big) * dd, WORD_PX, 3);
    out->cap_px = snap(30.0f - 6.0f * dd, CAP_PX, 2);
    out->cap_y = out->val_y + (int)lroundf(gap0 + (28.0f - gap0) * dd);
    out->rgb = p->rgb ? p->rgb : mix(DAY_TEXT, DAY_FAR, dd);
    out->opa = (uint8_t)lroundf(op * 255.0f);
}

static bool same_group(const day_model_t *a, const day_model_t *b)
{
    if (a->face == b->face) return true;
    bool ap = a->face == DAY_FACE_ON || a->face == DAY_FACE_PAST, bp = b->face == DAY_FACE_ON || b->face == DAY_FACE_PAST;
    return ap && bp && a->turning && b->turning;
}

void day_roll_start(day_roll_t *r, const day_model_t *prev, const day_model_t *next, bool first, double t)
{
    if (first) {
        r->kind = DAY_ROLL_NONE;
        return;
    }
    day_roll_kind_t k = DAY_ROLL_NONE;
    if (!same_group(prev, next)) k = DAY_ROLL_FACE;
    else if (!prev->turning && next->turning) k = DAY_ROLL_TURN;
    else if (prev->turning && !next->turning) k = DAY_ROLL_BACK;
    if (k == DAY_ROLL_NONE) return;
    r->kind = k;
    r->t0 = t;
    r->top = prev->top;
    r->mid = prev->mid;
    r->low = prev->low;
    r->had_top = prev->turning;
}

static int put(day_slot_t *out, int n, const day_panel_t *p, float u, float fade)
{
    if (!p->val[0] && !p->cap[0]) return n;
    out[n] = (day_slot_t){.p = *p, .u = u, .fade = fade};
    return n + 1;
}

int day_roll_slots(day_roll_t *r, const day_model_t *m, double t, day_slot_t out[DAY_DRUM_SLOTS])
{
    float p = r->kind == DAY_ROLL_NONE ? 1.0f : clampf((float)((t - r->t0) / DAY_ROLL_S), 0, 1);
    if (p >= 1.0f) r->kind = DAY_ROLL_NONE;
    float e = 1.0f - powf(1.0f - p, 3.0f);
    int n = 0;
    switch (r->kind) {
    case DAY_ROLL_TURN:
        n = put(out, n, &m->top, -e, 1);
        n = put(out, n, &m->mid, 1 - e, 1);
        n = put(out, n, &m->low, 2 - e, 1);
        break;
    case DAY_ROLL_BACK:
        n = put(out, n, &m->mid, -1 + e, 1);
        n = put(out, n, &m->low, e, 1);
        n = put(out, n, &r->low, 1 + e, 1);
        break;
    case DAY_ROLL_FACE:
        if (r->had_top) n = put(out, n, &r->top, -1 - e, 1 - e);
        n = put(out, n, &r->mid, -e, 1 - e);
        n = put(out, n, &r->low, 1 - e, 1 - p / 0.1f);
        n = put(out, n, &m->mid, 1 - e, (p - 0.1f) / 0.15f);
        n = put(out, n, &m->low, 2 - e, 1);
        break;
    default:
        if (m->turning) n = put(out, n, &m->top, -1, 1);
        n = put(out, n, &m->mid, 0, 1);
        n = put(out, n, &m->low, 1, 1);
        break;
    }
    return n;
}
