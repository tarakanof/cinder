#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "weather_face.h"
#include "wx_clock.h"
#include "wx_rain.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_DAYS_ROW_Y 228
#define WX_DAYS_PITCH 85
#define WX_DAYS_DAY_W 60
#define WX_DAYS_ICON 40
#define WX_DAYS_TEMP_W 54
#define WX_DAYS_BAR_W 104
#define WX_DAYS_BAR_H 10
#define WX_DAYS_GAP 10
#define WX_DAYS_ROW_UP 22
#define WX_DAYS_ROW_H 74
#define WX_DAYS_POP_DY 22
#define WX_DAYS_SLIDE 12
#define WX_DAYS_STAGGER_S 0.08
#define WX_DAYS_IN_S 0.42
#define WX_DAYS_LO_OPA 204
#define WX_DAYS_SUB_Y 372
#define WX_DAYS_RING_HW 3.0f
#define WX_DAYS_TRACK_RGB 0x262626
#define WX_DAYS_NAME_RGB 0xBDBDBD
#define WX_DAYS_POP_RGB 0x5C9CE0

typedef enum { WX_ICON_SUN, WX_ICON_PARTLY, WX_ICON_CLOUD, WX_ICON_FOG, WX_ICON_RAIN, WX_ICON_SNOW, WX_ICON_STORM, WX_ICONS } wx_icon_t;

typedef struct {
    signed char wday; /* 0 Sunday .. 6 Saturday */
    signed char icon; /* wx_icon_t */
    signed char pop;  /* the chance shown under the icon, %, -1 none */
    short lo, hi;     /* whole degrees C, lo <= hi */
} wx_days_row_t;

typedef struct {
    bool on;   /* the Next days face */
    bool have; /* at least one row */
    int n;
    int amin, amax; /* the rows' shared axis, amin < amax */
    wx_days_row_t row[WX_DAYS];
} wx_days_t;

/* Weekday of a day starting at local midnight t (Unix s); without the offset, t's UTC noon-rounded date. */
int wx_weekday(long long t, bool has_offset, int utc_offset);
wx_icon_t wx_days_icon(int face);
bool wx_days_wet(int icon);
/* The rows: Ember's days that have not ended by the knob's clock (all of them when it is unknown). */
void wx_days_from(const wx_obs_t *o, wx_days_t *d);
bool wx_days_same(const wx_days_t *a, const wx_days_t *b);

/* Centre y of row i of n, face coordinates at rest. */
int wx_days_row_y(int n, int i);
/* Row i's coloured span on the shared axis, px from the bar's left end; at least WX_DAYS_BAR_H wide, inside 0..bar_w. */
void wx_days_span(const wx_days_t *d, int i, int bar_w, float *x0, float *x1);
/* Widths in px of a temperature in font_face_36 ("−5°") and of a weekday name in Montserrat 30. */
int wx_days_deg_w(int v);
int wx_days_name_w(int wday);

typedef struct {
    int x0, x1;          /* the row's extent */
    int day_x, day_w;    /* name, left aligned */
    int icon_x;          /* WX_DAYS_ICON square */
    int lo_x, lo_w;      /* low, right aligned in its column */
    int bar_x, bar_w;
    int hi_x, hi_w;      /* high, left aligned */
} wx_days_cols_t;

/* AN1's columns, widened for the widest name and temperatures shown; the bar gives up the space, so the row keeps AN1's
   352 px (the bar stays 34 px or more for any name and -99..99). */
void wx_days_cols(const wx_days_t *d, wx_days_cols_t *c);

typedef struct {
    wx_days_cols_t c;
    int n, y[WX_DAYS];
    wx_box_t sprite;     /* each row's icon and bar sprite: x in face coordinates, y from the row's top */
    int at[WX_DAYS];     /* byte offsets of the RGB565A8 sprites in the still-cloud buffer */
    wx_box_t area;       /* one redraw box over every row and its slide */
    int total;
} wx_days_layout_t;

/* Rows sit WX_DAYS_ROW_UP above their centre and are WX_DAYS_ROW_H tall. */
void wx_days_layout(const wx_days_t *d, wx_days_layout_t *l);

/* Row i's arrival t s after the face showed: opacity 0..255 and the slide, px down. */
void wx_days_arrive(int i, double t, int *opa, int *dy);

typedef struct {
    int opa[WX_DAYS], dy[WX_DAYS]; /* wx_days_arrive */
    int lo[WX_DAYS], hi[WX_DAYS];  /* the count-up values shown */
} wx_days_frame_t;

/* Every row's state t s after the face showed (the device paints it at 30 fps). */
void wx_days_frame(const wx_days_t *d, double t, wx_days_frame_t *f);
/* Row i's container at slide dy: as wide as l's redraw box, WX_DAYS_ROW_H tall from WX_DAYS_ROW_UP above its centre. */
wx_box_t wx_days_row_box(const wx_days_layout_t *l, int i, int dy);

#ifdef __cplusplus
}
#endif
