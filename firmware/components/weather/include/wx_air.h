#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "weather_face.h"
#include "wx_rain.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WX_AIR_MAX 999
#define WX_AIR_GAUGE 100
#define WX_AIR_POOR 60
#define WX_AIR_HYST 5
#define WX_AIR_BANDS 6
#define WX_AIR_UNIT_RGB 0x9A9A9A
#define WX_AIR_FPS 15
#define WX_PETAL_BOX 20
#define WX_PETAL_ROTS 6
#define WX_PETAL_X 250
#define WX_PETAL_Y 202

typedef struct {
    bool on;     /* the Air face */
    bool have;   /* fresh data with Ember's aqi */
    int aqi;     /* European AQI, 0..WX_AIR_MAX */
    int band;    /* 0 Good .. 5 Extremely poor */
    bool wilted; /* the poor-day flower (AA4), else the healthy one (AA2) */
    float frac;  /* the marker on the 0..WX_AIR_GAUGE gauge */
} wx_air_t;

/* AA2's continuous gauge colour at an AQI (band centres 10, 30, 50, 70, 90). */
uint32_t wx_air_rgb(int aqi);
/* European AQI bands, upper edge inclusive: 20 Good, 40 Fair, 60 Moderate, 80 Poor, 100 Very poor, above Extremely poor. */
int wx_air_band(int aqi);
const char *wx_air_word(int band);
uint32_t wx_air_band_rgb(int band);
/* Poor and worse wilt the flower; shown: the variant on screen or -1 (0 healthy, 1 wilted), kept within WX_AIR_HYST. */
bool wx_air_wilted(int aqi, int shown);
/* shown: the air on screen or NULL (its flower is kept by wx_air_wilted). */
void wx_air_from(const wx_obs_t *o, const wx_air_t *shown, wx_air_t *a);
/* The auto rule: fresh data and an AQI above WX_AIR_POOR (above WX_AIR_POOR - WX_AIR_HYST while shown). */
bool wx_air_poor(const wx_obs_t *o, bool shown);
bool wx_air_same_scene(const wx_air_t *a, const wx_air_t *b);

typedef struct {
    int x, y; /* the petal sprite's top left, face coordinates */
    int rot;  /* 0..WX_PETAL_ROTS-1: -50 degrees .. 0 in 10 degree steps */
    int opa;  /* 0..255, 0 hidden */
} wx_petal_t;

/* AA4's falling petal at frame k (WX_AIR_FPS from the face's show): it drifts down from 58 px above its resting place,
   turning out of -50 degrees, lies there and fades; a 7 s loop. k < 0: hidden. */
void wx_air_petal(int k, wx_petal_t *p);
/* The one redraw box over every place the petal takes. */
wx_box_t wx_air_petal_box(void);

#ifdef __cplusplus
}
#endif
