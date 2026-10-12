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
typedef struct {
    wx_cloud_t cl;
    int np;
    float p[5][2], box[4];
    bool large[4];
} wx_cloud_shape_t;

#define WX_CLOUD_EXACT 4.0f

/* False (an empty shape, no ink) unless 1-4 puffs, neighbours cross above the base and the outer puffs cross it. */
bool wx_cloud_shape(const wx_cloud_t *cl, wx_cloud_shape_t *s);
/* Signed distance to the cloud path, negative inside; exact within WX_CLOUD_EXACT, past it beyond. */
float wx_cloud_sd(const wx_cloud_shape_t *s, float x, float y);
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

/* The forecast variant's clouds on one side (wx_cast_sheet's side); none outside the variant. */
void wx_cast_clouds(int variant, int side, wx_clouds_t *out);

/* Forecast layers, carved out of the still-cloud buffer like the rain layers. Byte offsets; clouds RGB565A8 at cloud,
   strips (2 h rows) and ramps A8. box: the cloud over its breathe range and its sheet, one redraw area. */
typedef struct {
    int sides;
    wx_box_t cloud[WX_CAST_SIDES], box[WX_CAST_SIDES];
    int cloud_at[WX_CAST_SIDES], strip[WX_CAST_SIDES], top[WX_CAST_SIDES], bottom[WX_CAST_SIDES], ramp_h[WX_CAST_SIDES];
    int total;
} wx_cast_layout_t;

void wx_cast_layout(int variant, wx_cast_layout_t *l);
/* The 12 h dial into the ring strip: grey hour cells, the arcs lit in their intensity colour. */
void wx_paint_ring_cast(wx_ring_t *r, const wx_cast_t *c);
void wx_paint_cast_sheet(uint8_t *a8, const wx_cast_sheet_t *s, bool snow);
/* Overlay alpha of the sheet's fade (WX_CAST_RAMP of its height): the top or the bottom ramp_h rows. */
void wx_paint_cast_ramp(uint8_t *a8, const wx_cast_sheet_t *s, bool bottom, int ramp_h);
#define WX_DROP_HW_N 61
/* The drop tail's half-width in px at x = -15 + 15 i / (WX_DROP_HW_N - 1) from the head's centre (the canvas's bezier). */
extern const float wx_drop_hw[WX_DROP_HW_N];
/* The forecast drop with its head on the ring at deg into a WX_DROP_BOX square RGB565A8 im; sets and returns its box. */
wx_box_t wx_paint_drop(wx_img_t *im, float deg);

/* Gauge ring 0..WX_WIND_GAUGE_KMH in the Beaufort band colours; grey: the no-data track. */
void wx_paint_ring_wind(wx_ring_t *r, bool grey, uint16_t lut[WX_RING_LUT]);
/* The windsock's pole, arm and base for a level, into the background. */
void wx_paint_pole(wx_img_t *im, int level);

/* Wind layers, carved out of the still-cloud buffer like the rain layers. Byte offsets, all A8: per pose the sock's
   light stripes (fill) and its darker stripes over it (dark), both of the sock box; one sprite per streak row
   (streak_w x WX_STREAK_H); WX_LEAF_ROTS leaf sprites (WX_LEAF_BOX squared) when the level has leaves. area: the one
   redraw box over the sock, every streak's and leaf's travel. */
typedef struct {
    wx_box_t sock, area;
    int fill[WX_WIND_POSES], dark[WX_WIND_POSES];
    int nrows, streak[WX_WIND_ROWS], streak_w[WX_WIND_ROWS];
    int nleaves, leaf[WX_LEAF_ROTS];
    int total;
} wx_wind_layout_t;

void wx_wind_layout(int level, wx_wind_layout_t *l);
/* One pose's quads (wx_wind_quad) into its fill and dark planes of the sock box. fill is the light coverage divided by
   what dark leaves uncovered, so dark drawn over fill composites each sample once (no hard stripe edge). */
void wx_paint_sock(uint8_t *fill, uint8_t *dark, wx_box_t box, const wx_pt_t q[WX_WIND_QUADS][4]);
void wx_paint_streak(uint8_t *a8, int level, int r, int w);
void wx_paint_leaf(uint8_t *a8, int rot);

/* Next days: AN1's thin grey ring (WX_DAYS_RING_HW) into the ring strip. */
void wx_paint_ring_days(wx_ring_t *r);
/* One wx_icon_t in its WX_DAYS_ICON square at face (x, y), the canvas's 40 px viewBox. */
void wx_paint_day_icon(wx_img_t *im, int icon, float x, float y);
const wx_cloud_t *wx_day_cloud(int icon);
/* Row i's sprite (im at the row's rest position, l's sprite box): the icon, the grey track and the coloured span. */
void wx_paint_day_row(wx_img_t *im, const wx_days_t *d, int i, const wx_days_layout_t *l);

/* Sun: the gauge from the first crossing to the last in wx_sun_rgb's colours (grey without sun times). */
void wx_paint_ring_sun(wx_ring_t *r, const wx_sun_t *s, uint16_t lut[WX_RING_LUT]);
/* The horizon, its two dashes and (with sun times) the sunrise and sunset ticks on the ring. */
void wx_paint_sun_scene(wx_img_t *im, const wx_sun_t *s);
/* The ray fan's A8 alpha over box (wx_sun_layout's rays), rim fade and horizon cut baked. */
void wx_paint_sun_rays(uint8_t *a8, wx_box_t box);
/* The sun and its glow at rest into im (wx_sun_layout's sun box), nothing below WX_SUN_CLIP_Y. */
void wx_paint_sun(wx_img_t *im);

/* cos and sin of an angle in degrees, any finite value, without libm's large-argument reduction (its stack). */
void wx_deg_cs(float deg, float *c, float *s);
/* Air: AA2's gauge 0..WX_AIR_GAUGE in wx_air_rgb (grey: no data). */
void wx_paint_ring_air(wx_ring_t *r, bool grey, uint16_t lut[WX_RING_LUT]);
/* The end icons (a flower at the clean end, a hazard sign at the other) and AA2's healthy or AA4's wilted flower. */
void wx_paint_air_scene(wx_img_t *im, bool wilted);
/* AA4's falling petal at rotation step rot (wx_petal_t) into a WX_PETAL_BOX square RGB565A8 im; sets im's x0/y0 to its rest. */
void wx_paint_petal(wx_img_t *im, int rot);

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
