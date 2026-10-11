#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wx_temp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RGB565 pixels, then (a non-NULL) an A8 plane: LVGL's RGB565A8. x0, y0: face coordinates of pixel 0. */
typedef struct {
    uint16_t *px;
    uint8_t *a;
    int w, h, x0, y0;
} wx_img_t;

#define WX_HERO_BOX 164
#define WX_THERMO_X 222
#define WX_THERMO_Y 106
#define WX_THERMO_W 42
#define WX_THERMO_H 76
#define WX_MARKER 36
#define WX_RING_LUT 541
#define WX_RING_PX 19512
#define WX_RING_RUNS 888

typedef struct {
    int16_t y, x, n;
} wx_run_t;

/* The ring's annulus as painted over black: background runs, row by row, and their pixels in run order. */
typedef struct {
    int nrun, npx;
    wx_run_t run[WX_RING_RUNS];
    uint16_t px[WX_RING_PX];
} wx_ring_t;

uint16_t wx_rgb565(uint32_t rgb);

void wx_paint_clear(wx_img_t *im);
/* Gauge ring lo..hi on the temperature scale; grey: the no-data track. lut: caller scratch. */
void wx_paint_ring(wx_ring_t *r, int lo, int hi, bool grey, uint16_t lut[WX_RING_LUT]);
/* Writes the ring's runs into a WX_FACE x WX_FACE background; nothing else reaches the annulus. */
void wx_ring_blit(const wx_ring_t *r, uint16_t *bg);
void wx_paint_rays(wx_img_t *im);
void wx_paint_stars(wx_img_t *im);
void wx_paint_thermo(wx_img_t *im);
/* Hero sprite centred in im (WX_HERO_BOX), rim fade baked; returns the face box it belongs at. */
wx_box_t wx_paint_hero(wx_img_t *im, wx_hero_t hero, int moon_phase, int moon_illum);
/* Ink box of the clouds (stroke included), clipped to the face; empty when none. */
wx_box_t wx_clouds_box(const wx_clouds_t *c);
void wx_paint_clouds(wx_img_t *im, const wx_clouds_t *c);
/* Mercury, bulb and ticks into the WX_THERMO_* box; top before WX_THERMO_DY. */
void wx_paint_mercury(wx_img_t *im, float top, uint32_t rgb);
void wx_paint_marker(wx_img_t *im);
#define WX_DOT_R 4.0f
#define WX_DOT_ON 0xFFFFFF
#define WX_DOT_OFF 0x4A4A4A
/* n face dots (wx_face_dot_xy), dot face white, the others grey. */
void wx_paint_face_dots(wx_img_t *im, int face, int n);

/* drift: the last (front) cloud, the one that moves; still: the others. still under drift = the whole layer. */
void wx_clouds_split(const wx_clouds_t *all, wx_clouds_t *still, wx_clouds_t *drift);
/* Largest box over the cloud bands, in pixels: the drift sprite or the still layer. */
int wx_clouds_max_px(bool drift);

#define WX_DRIP_W 8
#define WX_DRIP_H 14
#define WX_DRIP_Y 196
#define WX_SPLASH_W 16
#define WX_SPLASH_H 12
#define WX_BOLT_W 32
#define WX_BOLT_H 44
#define WX_BOLT_X 196
#define WX_BOLT_Y 54
#define WX_RAMP 0.16f

/* Rain layers, carved out of the still-cloud buffer (a rain scene and a cloud scene never show together). Byte offsets;
   tiles, ramps, drips, splash and bolt are A8, covers RGB565A8. */
typedef struct {
    int tile[WX_SHEETS], top[WX_SHEETS], bottom[WX_SHEETS], cover[WX_SHEETS];
    int ramp_h[WX_SHEETS];
    wx_box_t cover_box[WX_SHEETS];
    int drip[WX_DRIP_POSES], splash[WX_SPLASHES], bolt, total;
} wx_rain_layout_t;

void wx_rain_layout(wx_rain_layout_t *l);
/* Face x of drip i's sprite (its y is WX_DRIP_Y plus the frame's dy). */
int wx_drip_x(int i);
/* Gauge ring 0..100 % for the rain face: from the dry track colour to the intensity colour; grey: no chance known. */
void wx_paint_ring_rain(wx_ring_t *r, int intensity, bool grey, uint16_t lut[WX_RING_LUT]);
/* Umbrella (canopy, tip, shaft stub, handle) over im; a cover's box clips it, so a cover holds no handle. */
void wx_paint_umbrella(wx_img_t *im);
/* Sheet s's strip, WX_SHEET_PERIOD rows, seamless top to bottom: streaks, or flakes for snow, by intensity. */
void wx_paint_sheet(uint8_t *a8, int s, const wx_rain_t *r);
/* Overlay alpha of sheet s's fade: 255 - mask along the box, rows 0..ramp_h-1 (top) or the last ramp_h rows. */
void wx_paint_ramp(uint8_t *a8, int s, bool bottom, int ramp_h);
void wx_paint_drip(uint8_t *a8, int pose);
void wx_paint_splash(uint8_t *a8, int n);
void wx_paint_bolt(uint8_t *a8);

typedef struct {
    uint16_t *bg;  /* WX_FACE x WX_FACE RGB565 */
    uint16_t *lut; /* WX_RING_LUT */
    wx_ring_t *ring;
    uint8_t *hero, *still, *drift; /* RGB565A8: WX_HERO_BOX squared, wx_clouds_max_px(false), wx_clouds_max_px(true) */
} wx_bake_buf_t;

typedef struct {
    wx_box_t hero, still, drift; /* face boxes; empty when not shown */
} wx_bake_out_t;

/* The WX_BAKE_* parts of t: RING the ring, HERO the hero and out->hero, SCENE all of it (background with the
   ring, clouds, out). Other buffers and fields are left as they are. Pure, any task. */
void wx_bake(const wx_temp_t *t, int kind, const wx_bake_buf_t *b, wx_bake_out_t *out);

#ifdef __cplusplus
}
#endif
