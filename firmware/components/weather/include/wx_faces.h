#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The weather page's faces in turn order; a new face goes before WXF_N. */
typedef enum { WXF_TEMP, WXF_RAIN, WXF_N } wx_face_id_t;

#define WXF_AUTO (-1)
#define WXF_DOT_R 206.0f
#define WXF_DOT_PITCH 16.0f

/* face moved by steps through the list, wrapping both ways. */
int wx_face_step(int face, int steps);

typedef struct {
    int pick; /* WXF_AUTO until the user turns on this page open */
} wx_faces_t;

void wx_faces_open(wx_faces_t *f);
/* shown: the face the page shows. True when the turn picked another face. */
bool wx_faces_turn(wx_faces_t *f, int shown, int detents);

/* Centre of face dot k of n: on the r WXF_DOT_R arc at the bottom, WXF_DOT_PITCH apart, face coordinates. */
void wx_face_dot_xy(int k, int n, float *x, float *y);

#ifdef __cplusplus
}
#endif
