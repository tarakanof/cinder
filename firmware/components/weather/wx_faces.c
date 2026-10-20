#include "wx_faces.h"

#include <math.h>
#include <string.h>

#include "wx_clock.h"
#include "wx_temp.h"

void wx_face_list_set(wx_face_list_t *l, const uint8_t *ids, int n)
{
    memset(l, 0, sizeof *l);
    unsigned seen = 0;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (ids[i] >= WXF_N || (seen & 1u << ids[i])) continue;
        seen |= 1u << ids[i];
        l->id[k++] = ids[i];
    }
    bool all = k == WXF_N;
    for (int i = 0; i < k; i++) all &= l->id[i] == i;
    l->n = all ? 0 : (uint8_t)k;
}

int wx_face_list_n(const wx_face_list_t *l) { return l->n ? l->n : WXF_N; }

static int face_at(const wx_face_list_t *l, int k) { return l->n ? l->id[k] : k; }

int wx_face_list_first(const wx_face_list_t *l) { return face_at(l, 0); }

int wx_face_list_pos(const wx_face_list_t *l, int face)
{
    for (int k = 0; k < wx_face_list_n(l); k++)
        if (face_at(l, k) == face) return k;
    return -1;
}

int wx_face_list_step(const wx_face_list_t *l, int face, int steps)
{
    int n = wx_face_list_n(l), pos = wx_face_list_pos(l, face);
    int k = ((pos < 0 ? 0 : pos) + steps % n) % n;
    return face_at(l, k < 0 ? k + n : k);
}

int wx_face_step(int face, int steps)
{
    static const wx_face_list_t ALL;
    return wx_face_list_step(&ALL, face, steps);
}

void wx_faces_open(wx_faces_t *f) { f->pick = WXF_AUTO; }

static bool known(int face) { return face >= 0 && face < WXF_N; }

int wx_faces_base(const wx_faces_t *f, int on_screen, int requested)
{
    if (known(f->pick)) return f->pick;
    if (known(on_screen)) return on_screen;
    return known(requested) ? requested : wx_face_list_first(&f->list);
}

bool wx_faces_turn(wx_faces_t *f, int on_screen, int requested, int detents)
{
    if (!detents) return false;
    int base = wx_faces_base(f, on_screen, requested);
    int next = wx_face_list_step(&f->list, base, detents);
    f->pick = next;
    return next != base;
}

void wx_face_dot_xy(int k, int n, float *x, float *y)
{
    wx_arc_dot_xy(k, n, WXF_DOT_R, WXF_DOT_PITCH, WX_FACE_C, WX_FACE_C, x, y);
}
