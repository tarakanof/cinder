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

bool wx_faces_turn(wx_faces_t *f, int shown, int detents)
{
    if (!detents) return false;
    int next = wx_face_step(shown, detents);
    f->pick = next;
    return next != shown;
}

void wx_face_dot_xy(int k, int n, float *x, float *y)
{
    float a = ((float)k - (float)(n - 1) / 2) * WXF_DOT_PITCH / WXF_DOT_R;
    *x = WX_FACE_C + WXF_DOT_R * sinf(a);
    *y = WX_FACE_C + WXF_DOT_R * cosf(a);
}
