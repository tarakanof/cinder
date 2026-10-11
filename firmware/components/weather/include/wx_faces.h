#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The weather page's faces in turn order; a new face goes before WXF_N and needs its case in wx_temp_face (-Wswitch). */
typedef enum { WXF_TEMP, WXF_RAIN, WXF_CAST, WXF_WIND, WXF_N } wx_face_id_t;

#define WXF_AUTO (-1)
#define WXF_DOT_R 206.0f
#define WXF_DOT_PITCH 16.0f

/* face moved by steps through the list, wrapping both ways. */
int wx_face_step(int face, int steps);

typedef struct {
    int pick; /* WXF_AUTO until the user turns on this page open */
} wx_faces_t;

void wx_faces_open(wx_faces_t *f);
/* The face a turn steps from: the pick once the user turned; else on_screen (the baked face the canvases show, -1 while
   none is attached); else requested (the face the page resolved last frame, -1 when unknown); else the first face. */
int wx_faces_base(const wx_faces_t *f, int on_screen, int requested);
/* Steps from wx_faces_base and picks the result, even when it is the same face. True when it differs from the base. */
bool wx_faces_turn(wx_faces_t *f, int on_screen, int requested, int detents);

/* Centre of face dot k of n: on the r WXF_DOT_R arc at the bottom, WXF_DOT_PITCH apart, face coordinates. */
void wx_face_dot_xy(int k, int n, float *x, float *y);

#ifdef __cplusplus
}
#endif
