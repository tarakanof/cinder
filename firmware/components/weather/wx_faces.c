#include "wx_faces.h"

#include <math.h>

#include "wx_temp.h"

int wx_face_step(int face, int steps)
{
    if (face < 0 || face >= WXF_N) face = 0;
    int k = (face + steps % WXF_N) % WXF_N;
    return k < 0 ? k + WXF_N : k;
}

void wx_faces_open(wx_faces_t *f) { f->pick = WXF_AUTO; }

static bool known(int face) { return face >= 0 && face < WXF_N; }

int wx_faces_base(const wx_faces_t *f, int on_screen, int requested)
{
    if (known(f->pick)) return f->pick;
    if (known(on_screen)) return on_screen;
    return known(requested) ? requested : WXF_TEMP;
}

bool wx_faces_turn(wx_faces_t *f, int on_screen, int requested, int detents)
{
    if (!detents) return false;
    int base = wx_faces_base(f, on_screen, requested);
    int next = wx_face_step(base, detents);
    f->pick = next;
    return next != base;
}

void wx_face_dot_xy(int k, int n, float *x, float *y)
{
    float a = ((float)k - (float)(n - 1) / 2) * WXF_DOT_PITCH / WXF_DOT_R;
    *x = WX_FACE_C + WXF_DOT_R * sinf(a);
    *y = WX_FACE_C + WXF_DOT_R * cosf(a);
}
