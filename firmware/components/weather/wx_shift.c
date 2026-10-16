#include "wx_shift.h"

#include <math.h>

static const wx_shift_pt_t SQUARE[] = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};

static const wx_shift_pt_t ORBIT[] = {
    {0, 0},   {2, 0},   {4, 0},   {4, 2},   {2, 2},   {0, 2},   {-2, 2},  {-4, 2},  {-4, 4},  {-2, 4},  {0, 4},   {2, 4},
    {4, 4},   {2, 4},   {0, 4},   {-2, 4},  {-4, 4},  {-4, 2},  {-2, 2},  {0, 2},   {2, 2},   {4, 2},   {4, 0},   {2, 0},
    {0, 0},   {-2, 0},  {-4, 0},  {-4, -2}, {-2, -2}, {0, -2},  {2, -2},  {4, -2},  {4, -4},  {2, -4},  {0, -4},  {-2, -4},
    {-4, -4}, {-2, -4}, {0, -4},  {2, -4},  {4, -4},  {4, -2},  {2, -2},  {0, -2},  {-2, -2}, {-4, -2}, {-4, 0},  {-2, 0},
};

const wx_shift_path_t WX_SHIFT_SQUARE = {SQUARE, (int)(sizeof SQUARE / sizeof SQUARE[0]), WX_SHIFT_S};
const wx_shift_path_t WX_SHIFT_ORBIT = {ORBIT, (int)(sizeof ORBIT / sizeof ORBIT[0]), WX_SHIFT_S};

static int wrap(int i, int n) { return n > 0 ? ((i % n) + n) % n : 0; }

bool wx_shift_tick(wx_shift_t *s, const wx_shift_path_t *p, double dt)
{
    if (isfinite(dt) && dt > 0) s->t += dt;
    if (!isfinite(s->t) || s->t < 0) s->t = 0;
    if (s->t < p->period_s) return false;
    s->t = 0;
    s->i = wrap(s->i + 1, p->n);
    return true;
}

void wx_shift_xy(const wx_shift_t *s, const wx_shift_path_t *p, int *dx, int *dy)
{
    const wx_shift_pt_t *q = &p->pt[wrap(s->i, p->n)];
    *dx = q->x;
    *dy = q->y;
}
