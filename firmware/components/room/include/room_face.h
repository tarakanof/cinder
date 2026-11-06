#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sens_room.h"
#include "wx_paint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The room page's faces in turn order, one per chip: Climate (AHT2x), Air (ENS160), Light (BH1750). */
typedef enum { ROOM_FACE_CLIMATE, ROOM_FACE_AIR, ROOM_FACE_LIGHT, ROOM_FACE_N } room_face_id_t;

#define ROOM_FACE_AUTO (-1)
#define ROOM_AQI_POOR 4
#define ROOM_AQI_CLEAR 2
#define ROOM_TEMP_LO 16
#define ROOM_TEMP_HI 28
#define ROOM_ECO2_LO 400
#define ROOM_ECO2_HI 2000
#define ROOM_LUX_HI 2000
#define ROOM_VALUE_MAX 19999
#define ROOM_GREY_RGB 0x8E8E93
#define ROOM_UNIT_RGB 0xD8D8DC
#define ROOM_TEXT_RGB 0xF4F4F2
#define ROOM_TRACK_RGB 0x262626

/* Bit f: face f shows. From the ROOM_HAS_* chips found; the radar and the BMP280 give no face. */
unsigned room_faces_mask(uint32_t found);
int room_faces_n(unsigned mask);
/* Position of face among the mask's faces (its dot), -1 when it is not there. */
int room_face_pos(unsigned mask, int face);
/* face moved by steps through the mask's faces, wrapping both ways; a face outside it enters at the first face going
   forward and the last going back (the first for 0 steps). -1 for an empty mask. */
int room_face_step(unsigned mask, int face, int steps);

/* A fresh, normal air reading with an AQI of 1-5: poor from ROOM_AQI_POOR, and while shown (Air on screen by the auto
   rule) until it falls to ROOM_AQI_CLEAR. No such reading: not poor. now_ms: the snapshot's esp_timer ms. */
bool room_air_poor(const room_t *r, int64_t now_ms, bool shown);
/* Air while poor and found, else Climate when found, else the mask's first face; -1 for an empty mask. */
int room_face_auto(unsigned mask, bool poor);

typedef struct {
    int pick; /* ROOM_FACE_AUTO until a turn on this page open */
    int face; /* the face resolved last frame, -1 before the first */
} room_faces_t;

void room_faces_open(room_faces_t *f);
/* This frame's face: the pick while the mask has it, else the auto face (Air resolved by the auto rule last frame keeps
   it within the hysteresis); recorded in f->face. -1 for an empty mask. */
int room_faces_resolve(room_faces_t *f, unsigned mask, const room_t *r, int64_t now_ms);
/* Steps from the pick, else the face resolved last frame, else the mask's first face, and picks the result. */
void room_faces_turn(room_faces_t *f, unsigned mask, int detents);

typedef struct {
    int face;
    bool have;        /* a fresh reading: the value in colour, the marker; else grey "−−", a grey ring, no marker */
    bool warm;        /* Air: a fresh reading while the ENS160 is not normal */
    int value;        /* whole number, clamped so its text fits the 300 px band */
    int aqi;          /* Air with have: 1..5 */
    float frac;       /* the marker on the gauge, 0..1 */
    char line2[24];   /* 30 px under the value */
    uint32_t line2_rgb;
    char line3[24];   /* 24 px under that; empty: none */
    uint32_t line3_rgb;
} room_view_t;

/* face's content for the snapshot at now_ms. A reading is fresh under ROOM_FRESH_MS (room_report). panel_level: the
   panel's 0-255 level, -1 unknown (Light then has no Panel line). */
void room_view(const room_t *r, int64_t now_ms, int face, int panel_level, room_view_t *v);
/* The value text at a counted value: Climate "22°" or "−5°", the others "612"; without a reading "−−°" (Climate) or "−−". */
void room_value_text(const room_view_t *v, int value, char *buf, size_t n);
/* The value colour at a counted value: Climate the temperature scale, Air the AQI band, Light the gauge; grey without a reading. */
uint32_t room_value_rgb(const room_view_t *v, int value);
/* The gauge colour at frac 0..1: Climate 16-28° on the temperature scale, Air 400-2000 ppm, Light log 1-2000 lux. */
uint32_t room_ring_rgb(int face, float frac);
/* The face's gauge colours for wx_paint_ring_lut; grey: the no-data track. */
void room_ring_lut(int face, bool grey, uint16_t lut[WX_RING_LUT]);
/* AQI 1..5: "Excellent air" .. "Unhealthy air" and their band colours; "" and grey outside. */
const char *room_air_word(int aqi);
uint32_t room_air_rgb(int aqi);
/* Lux on the log gauge: 0 at 1 lux and below, 1 from ROOM_LUX_HI. */
float room_lux_frac(float lux);
/* The panel level 0-255 as a whole percentage. */
int room_panel_pct(int level);

#ifdef __cplusplus
}
#endif
