#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ember's view block `print` (DEVICE-PROTOCOL "Print"). Text caps are UTF-8 bytes after JSON unescaping. */
#define PRINT_NAME_MAX 128
#define PRINT_MATERIAL_MAX 64
#define PRINT_PRINTER_MAX 128
#define PRINT_THUMBV_MAX 40
#define PRINT_TEMP_MAX 500
#define PRINT_FRAME_PX 466

typedef enum {
    PRINT_UNKNOWN,
    PRINT_OFFLINE,
    PRINT_IDLE,
    PRINT_HEATING,
    PRINT_PREPARING,
    PRINT_PRINTING,
    PRINT_FINISHING,
    PRINT_PAUSED,
    PRINT_ATTENTION,
    PRINT_FINISHED,
    PRINT_STOPPED,
    PRINT_ERROR,
} print_phase_t;

typedef enum { PRINT_EV_NONE, PRINT_EV_UNKNOWN, PRINT_EV_STARTED, PRINT_EV_ATTENTION, PRINT_EV_FINISHED, PRINT_EV_STOPPED, PRINT_EV_ERROR } print_ev_kind_t;

typedef struct {
    long long id;   /* Unix ms, increasing; 0 = no event */
    print_ev_kind_t kind;
    long long at;   /* Unix s */
} print_event_t;

typedef struct {
    print_phase_t phase;
    bool has_progress;
    int progress;                 /* whole percent 0-100 */
    long long ends_at, started_at, change_at;   /* Unix s; 0 absent */
    bool has_temps;
    int nozzle, nozzle_target, bed, bed_target;   /* whole degrees, all four or none */
    char name[PRINT_NAME_MAX + 1];
    char material[PRINT_MATERIAL_MAX + 1];
    char printer[PRINT_PRINTER_MAX + 1];
    char thumb_version[PRINT_THUMBV_MAX + 1];   /* "" absent; [0-9A-Za-z._-] only, so it goes into a URL as is */
    int thumb_top, thumb_bottom;  /* rows [top, bottom) of the 466 px picture; set only with thumb_version */
    print_event_t event;
} print_block_t;

struct cJSON;
/* False with *out zeroed when o is not an object or has no string phase. An unknown phase is PRINT_UNKNOWN; a key of the
   wrong type or out of range is absent; text over its cap is cut at a UTF-8 boundary; thumb_version without valid
   thumb_rows (0 <= top < bottom <= 466) is dropped, and so are the rows without it. */
bool print_parse(const struct cJSON *o, print_block_t *out);
print_phase_t print_phase_from_wire(const char *s);
const char *print_phase_name(print_phase_t p);
print_ev_kind_t print_ev_kind_from_wire(const char *s);

/* Seconds left at server time now; -1 without ends_at. Never negative otherwise. */
long long print_left_s(const print_block_t *b, double now);
/* Whole minutes left, rounded up, so "0 min" shows only at the end; -1 without ends_at. The value changes on a server
   minute boundary of ends_at - now. */
int print_left_min(const print_block_t *b, double now);
/* "1:42" for 102 min, "42" for 42; unit "h left" or "min left". */
void print_left_text(int min, char *val, size_t vcap, const char **unit);
/* "HH:MM" of the Unix second t in the zone utc_offset (s). */
void print_hhmm(long long t, int utc_offset, char *buf, size_t cap);
/* "2 h 14 m" or "14 min" for a duration in seconds. */
void print_duration_text(long long s, char *buf, size_t cap);
/* The job's length for the Done face: event.at - started_at while the newest event is the finish; -1 unknown. */
long long print_took_s(const print_block_t *b);

typedef enum { PRINT_FACE_K1, PRINT_FACE_K2, PRINT_FACES } print_face_t;
#define PRINT_FACE_AUTO (-1)

/* The phase's look, drawn on either face. */
typedef enum {
    PRINT_LOOK_NONE,      /* no block: dim "Printer off" */
    PRINT_LOOK_OFFLINE,
    PRINT_LOOK_IDLE,
    PRINT_LOOK_HEATING,
    PRINT_LOOK_PROGRESS,  /* preparing (with an estimate) and printing */
    PRINT_LOOK_FINISHING,
    PRINT_LOOK_PAUSED,
    PRINT_LOOK_ATTENTION,
    PRINT_LOOK_DONE,
    PRINT_LOOK_STOPPED,
    PRINT_LOOK_ERROR,
} print_look_t;

print_look_t print_look(const print_block_t *b);
/* The face a page open shows: K1 (the spec's first face). */
print_face_t print_face_open(const print_block_t *b);
/* The user's pick holds until the next open; pick is PRINT_FACE_AUTO until a turn. */
typedef struct {
    int pick;
} print_faces_t;
void print_faces_open(print_faces_t *f);
print_face_t print_faces_shown(const print_faces_t *f, const print_block_t *b);
/* Steps from the shown face, wrapping both ways; true when the face changed. */
bool print_faces_turn(print_faces_t *f, const print_block_t *b, int detents);

/* The picture's clip row: ghost in [top, clip), solid in [clip, bottom). */
int print_clip_row(int top, int bottom, int progress);
/* The ring's share 0..1: the heat-up while heating, else progress; 0 for no block, offline and idle. */
float print_ring_frac(const print_block_t *b);

#define PRINT_COL_TEXT 0xF4F4F2
#define PRINT_COL_SUB 0x9A9A96
#define PRINT_COL_SMALL 0x8A8A86
#define PRINT_COL_DIMWORD 0x5A5A5A
#define PRINT_COL_ACCENT 0xFF8A3D
#define PRINT_COL_HEAT 0xFF5A3A
#define PRINT_COL_WAIT 0xFFC14D
#define PRINT_COL_DONE 0x4FA9FF
#define PRINT_COL_ERR 0xFF3A3A
#define PRINT_COL_GREY 0x9AA3AD
#define PRINT_COL_FIL 0xFF6A2A
#define PRINT_COL_TRACK 0x1D1D1D

uint32_t print_mix(uint32_t a, uint32_t b, float k);
uint32_t print_scale(uint32_t c, float k);

typedef struct {
    uint32_t col;
    bool head;
    bool pulse;   /* the hero pulses (attention, error) */
} print_ring_look_t;
void print_ring_look(print_look_t l, print_ring_look_t *out);

/* val "" = no big value; word "" = no state word. Colours RGB888. */
typedef struct {
    char val[16];
    char unit[16];
    char word[24];
    char sub[64];
    char small[2 * PRINT_NAME_MAX + 8];
    uint32_t val_col, unit_col, word_col, sub_col, small_col;
} print_text_t;

/* now: server Unix s (0 unknown); has_off: utc_offset is Ember's zone (day block, else weather). The face's text; name, material and
   printer are copied raw (the view folds them to ASCII). */
void print_text(const print_block_t *b, print_face_t face, double now, bool has_off, int utc_offset, print_text_t *out);
/* Before the first view: "Connecting"; offline: the last face with "offline" as its sub-line and no file line. */
void print_text_link(print_text_t *t, bool fed, bool offline);

typedef struct {
    int x, y, w, h;
} print_rect_t;

typedef struct {
    float cx, cy, sc;   /* the drawing's centre and scale, face px; the unscaled drawing is 120 x 112 */
} print_hero_t;

typedef struct {
    print_look_t look;
    float progress;     /* 0..1 */
    float heat;         /* 0..1, heating */
    float head_x;       /* -20..20 unscaled px */
    float pulse;        /* 0.35..1 */
} print_pose_t;

/* The hero's box in face px, even-aligned. */
print_rect_t print_hero_box(const print_hero_t *h);
/* The head and nozzle alone at head_x (unscaled px), even-aligned. */
print_rect_t print_hero_head_at(const print_hero_t *h, float head_x);
/* The bed and nozzle at their heating place: the only pixels the heat-up changes. */
print_rect_t print_hero_heat_box(const print_hero_t *h);
print_rect_t print_rect_union(print_rect_t a, print_rect_t b);
/* Sweeps only while printing; centred while heating, parked otherwise. */
float print_head_x(print_phase_t phase, double t);
/* 0.35..1 for attention and error at t, else 1. */
float print_pulse(print_look_t l, double t);
/* Paints the hero into buf (w x h RGB565) inside clip, over black. */
void print_hero_render(const print_hero_t *h, const print_pose_t *p, uint16_t *buf, int w, int h_px, print_rect_t clip);

/* K2's picture rows, x PRINT_PIC_X0 .. PRINT_PIC_X1 of the 466 px frame (Ember's hero box). */
#define PRINT_PIC_X0 93
#define PRINT_PIC_X1 373
#define PRINT_PIC_W (PRINT_PIC_X1 - PRINT_PIC_X0)
/* Hot line: pixels of row y where the ghost has the model, lit from the left up to share k (0..1). */
void print_hot_line(const uint16_t *ghost_row, uint16_t *dst_row, int w, float k);
/* The Done glint over a solid row: brightened by a (0..1). */
void print_glint_row(const uint16_t *src, uint16_t *dst, int w, float a);

#define PRINT_DOT_R 200.0f
#define PRINT_DOT_PITCH 16.0f
#define PRINT_DOT_SIZE 5.0f
#define PRINT_COL_DOT_ON 0xF4F4F2
#define PRINT_COL_DOT_OFF 0x4A4A4A
/* Centre of face dot k of n on the bottom arc, around (cx, cy). */
void print_dot_xy(int k, int n, float cx, float cy, float *x, float *y);
/* An anti-aliased disc over what is in buf; returns its even-aligned box. */
print_rect_t print_dot_render(uint16_t *buf, int w, int h, float cx, float cy, float r, uint32_t rgb);

#ifdef __cplusplus
}
#endif
