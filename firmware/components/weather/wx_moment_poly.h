#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The polygon painter of the print and room moment sprites: 4 x 4 supersampled fills into an RGB565A8 layer. Coordinates
   are sprite box px; x0, y0 is the layer's place in the box. */
typedef struct {
    uint16_t *px;
    uint8_t *a;
    int w, h, x0, y0;
    void (*yield)(void);
} wxp_img_t;

typedef struct {
    float x, y;
} wxp_pt_t;

void wxp_poly(wxp_img_t *im, const wxp_pt_t *p, int n, uint32_t rgb);
void wxp_rect(wxp_img_t *im, float x0, float y0, float x1, float y1, uint32_t rgb);
void wxp_rrect(wxp_img_t *im, float x0, float y0, float x1, float y1, float r, uint32_t rgb);
void wxp_disc(wxp_img_t *im, float cx, float cy, float r, uint32_t rgb);
