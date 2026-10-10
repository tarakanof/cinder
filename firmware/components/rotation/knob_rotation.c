#include "knob_rotation.h"

const int KR_SUPPORTED[KR_N_SUPPORTED] = {0, 180};

#define VIS_X1 (KR_FRAME_W - 1)
#define VIS_Y1 (KR_FRAME_H - 1)

bool kr_valid(int deg) { return deg == 0 || deg == 90 || deg == 180 || deg == 270; }

int kr_effective(int deg)
{
    for (int i = 0; i < KR_N_SUPPORTED; i++)
        if (KR_SUPPORTED[i] == deg) return deg;
    return 0;
}

kr_panel_t kr_panel_in(int deg, int mirror_w, int mirror_h)
{
    if (kr_effective(deg) != 180) return (kr_panel_t){0};
    kr_panel_t p = {
        .mirror_x = true,
        .mirror_y = true,
        .gap_x = mirror_w - 1 - (KR_VIS_X0 + VIS_X1),
        .gap_y = mirror_h - 1 - VIS_Y1,
    };
    p.min_x = p.gap_x < 0 ? -p.gap_x : 0;
    p.min_y = p.gap_y < 0 ? -p.gap_y : 0;
    return p;
}

kr_panel_t kr_panel(int deg) { return kr_panel_in(deg, KR_MIRROR_W, KR_MIRROR_H); }

void kr_round(const kr_panel_t *p, int *x1, int *y1, int *x2, int *y2)
{
    *x1 = (*x1 >> 1) << 1;
    *y1 = (*y1 >> 1) << 1;
    *x2 = ((*x2 >> 1) << 1) + 1;
    *y2 = ((*y2 >> 1) << 1) + 1;
    if (*x1 < p->min_x) *x1 = p->min_x;
    if (*y1 < p->min_y) *y1 = p->min_y;
    if (*x2 > VIS_X1) *x2 = VIS_X1;
    if (*y2 > VIS_Y1) *y2 = VIS_Y1;
    if (*x2 < *x1) *x2 = *x1 + 1;
    if (*y2 < *y1) *y2 = *y1 + 1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void kr_touch(int deg, int *x, int *y)
{
    if (kr_effective(deg) == 180) {
        *x = KR_VIS_X0 + VIS_X1 - *x;
        *y = VIS_Y1 - *y;
    }
    *x = clampi(*x, 0, KR_FRAME_W - 1);
    *y = clampi(*y, 0, KR_FRAME_H - 1);
}

void kr_state_init(kr_state_t *s) { *s = (kr_state_t){.backoff_ms = KR_RETRY_MIN_MS}; }

void kr_want(kr_state_t *s, int deg)
{
    deg = kr_effective(deg);
    if (deg == s->want) return;
    s->want = deg;
    s->retry_ms = 0;
    s->backoff_ms = KR_RETRY_MIN_MS;
}

kr_result_t kr_step(kr_state_t *s, int64_t now_ms, kr_orient_fn orient, void *ctx)
{
    if (s->shown == s->want || now_ms < s->retry_ms) return KR_IDLE;
    if (orient(ctx, s->want) == 0) {
        s->shown = s->touch = s->want;
        s->retry_ms = 0;
        s->backoff_ms = KR_RETRY_MIN_MS;
        return KR_APPLIED;
    }
    s->retry_ms = now_ms + s->backoff_ms;
    s->backoff_ms = s->backoff_ms * 2 > KR_RETRY_MAX_MS ? KR_RETRY_MAX_MS : s->backoff_ms * 2;
    if (s->shown != KR_UNKNOWN && orient(ctx, s->shown) == 0) {
        s->touch = s->shown;
        return KR_ROLLED_BACK;
    }
    s->shown = KR_UNKNOWN;
    return KR_LOST;
}
