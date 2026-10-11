#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_face.h"
#include "wx_temp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WXM_FPS 15
#define WXM_FRAMES 68
#define WXM_TEXT_IN 2
#define WXM_SPRITE_FROM 2
#define WXM_SPRITE_IN 8
#define WXM_HOLD_END 58
#define WXM_SPRITE_OUT 64
#define WXM_TEXT_OUT 65
#define WXM_RISE 7
#define WXM_K 0.9f
#define WXM_BOX_X 158
#define WXM_BOX_Y 62
#define WXM_BOX_W 154
#define WXM_BOX_H 90
#define WXM_TEXT_X 86
#define WXM_TEXT_Y 310
#define WXM_TEXT_W 300
#define WXM_TEXT_H 36
#define WXM_TEXT_MAX 40
#define WXM_LAYERS 5
#define WXM_ELEMS 8
#define WXM_ARENA 98304
#define WXM_GAP_S 300.0
#define WXM_SAME_S 1800.0
#define WXM_WAIT_S 1800.0
#define WXM_SETTLE_S 3.0
#define WXM_PRE_S 0.5
#define WXM_HOT_C 28
#define WXM_HOT_KEEP_C 26
#define WXM_FROST_C 0
#define WXM_FROST_KEEP_C 2
#define WXM_BLUE 0x5C9CE0
#define WXM_STORM_RGB 0xF0D040

/* Change moments rank in this order (STORM highest); GLANCE and NIGHT are only periodic. */
typedef enum {
    WXM_NONE,
    WXM_GLANCE,
    WXM_NIGHT,
    WXM_FROST,
    WXM_HEAT,
    WXM_WIND,
    WXM_AFTERRAIN,
    WXM_SNOW,
    WXM_RAIN,
    WXM_STORM,
    WXM_KINDS
} wxm_kind_t;

/* The bot's mood as a moment sees it: idle, sleepy and done rest; waiting and error are busy. */
typedef enum { WXM_REST, WXM_WORKING, WXM_BUSY } wxm_mood_t;

typedef struct {
    wxm_kind_t kind;
    bool quiet;         /* working: the sprite and the text only, the eyes keep working */
    bool sun;           /* GLANCE, FROST: the sun behind the cloud (day, not overcast or fog) */
    bool moon;          /* NIGHT: the moon is above the horizon (Ember's moon.up) */
    int moon_phase, moon_illum;
    char text[WXM_TEXT_MAX];
    uint32_t rgb;
} wxm_moment_t;

/* What a change is measured on. precip: 0 dry, 1 rain, 2 snow, 3 storm (wx_rain_from); hot from WXM_HOT_C, kept to
   WXM_HOT_KEEP_C; frost from WXM_FROST_C, kept to WXM_FROST_KEEP_C; windy as the Wind face's auto rule. */
typedef struct {
    bool have;
    int precip;
    bool hot, frost, windy;
} wxm_sig_t;

/* prev: the last signature (its flags are kept within their hysteresis) or NULL. have false without fresh data. */
void wxm_sig_from(const wx_obs_t *o, const wxm_sig_t *prev, wxm_sig_t *s);
/* The highest-ranked moment a change from a to b calls for, WXM_NONE for none (also when either lacks data). */
wxm_kind_t wxm_change(const wxm_sig_t *a, const wxm_sig_t *b);
/* Whether a change moment still fits the weather s (a queued one is dropped when not). */
bool wxm_holds(wxm_kind_t k, const wxm_sig_t *s);
/* The periodic moment for the weather now: RAIN, SNOW or STORM while it falls, NIGHT on a clear night, else GLANCE;
   WXM_NONE without fresh data. t: scratch. */
wxm_kind_t wxm_now_kind(const wx_obs_t *o, wx_temp_t *t);
/* Fills m for kind from the observation; false without fresh data. t: scratch. */
bool wxm_moment_from(wxm_kind_t kind, const wx_obs_t *o, bool quiet, wx_temp_t *t, wxm_moment_t *m);

typedef struct {
    bool base;
    wxm_sig_t sig;
    wxm_kind_t pending;
    double pending_at;
    double last_at, next_every;
    double kind_at[WXM_KINDS];
    char last_text[WXM_TEXT_MAX];
    bool shown;
    wxm_mood_t mood;
    double steady_at;
    wx_temp_t scratch;
} wxm_trigger_t;

typedef struct {
    double t;            /* monotonic s */
    const wx_obs_t *obs; /* NULL: none */
    wxm_mood_t mood;
    bool shown;          /* the bot page is on screen */
    bool blocked;        /* a glint chase, quiet hours or a moment still playing */
    bool on;             /* bot.weather_moments */
    int every_s;         /* the periodic moment's interval, 0 none */
} wxm_in_t;

void wxm_init(wxm_trigger_t *tr);
/* Every frame. Returns true and fills m when a moment starts now; it then counts as shown for the cooldowns. */
bool wxm_tick(wxm_trigger_t *tr, const wxm_in_t *in, wxm_moment_t *m);

/* Layers: RGB565A8 images in the arena at byte offset at, box px (the sprite box at WXM_BOX_X/Y). Elements: a layer
   drawn at x, y at rest, in draw order; one layer may be drawn by several elements (the raindrops, the flakes). */
typedef struct {
    int x, y, w, h, at;
} wxm_layer_t;

typedef struct {
    int layer, x, y;
} wxm_place_t;

typedef struct {
    int nl, ne, total;
    wxm_layer_t layer[WXM_LAYERS];
    wxm_place_t el[WXM_ELEMS];
} wxm_layout_t;

void wxm_layout(const wxm_moment_t *m, wxm_layout_t *l);
/* Paints every layer of l into the arena (l->total bytes). Pure, any task. */
void wxm_paint(const wxm_moment_t *m, const wxm_layout_t *l, uint8_t *arena);

typedef struct {
    int x, y, opa;
} wxm_el_t;

typedef struct {
    int opa;  /* the sprite, 0..255 */
    int dy;   /* the sprite, px below its rest (the rise) */
    int text_opa;
    bool icon_off; /* quiet: the tool icon steps aside for the text line */
    wxm_el_t el[WXM_ELEMS];
} wxm_frame_t;

/* Frame k (WXM_FPS from the moment's start, 0..WXM_FRAMES-1). Element x, y: box px, the rise included. */
void wxm_frame(const wxm_moment_t *m, const wxm_layout_t *l, int k, wxm_frame_t *f);
enum { WXM_DIRTY_SPRITE = 1, WXM_DIRTY_TEXT = 2 };
/* What b changes against the frame on screen a (NULL: nothing shown): the sprite box, the text line (with the tool icon
   in the quiet variant). Frames in order, or cut short by wxm_abort_frame, never change both. */
int wxm_diff(const wxm_layout_t *l, const wxm_frame_t *a, const wxm_frame_t *b);
/* Where a moment cut short at frame k goes on: the same opacity on the way out. */
int wxm_abort_frame(int k);

typedef enum { WXM_GLIDE, WXM_SACCADE, WXM_LID, WXM_BLINK, WXM_EYES, WXM_HOP, WXM_END } wxm_op_t;
typedef enum { WXM_EYES_MOOD, WXM_EYES_ROUND, WXM_EYES_HAPPY } wxm_eyes_t;

/* GLIDE/SACCADE to gaze (a, b); LID to a over b s; BLINK at speed a; EYES to eyes; HOP; END back to the viewer (a 1:
   glide, else a saccade) with the lid back over b s. */
typedef struct {
    float t;
    wxm_op_t op;
    float a, b;
    wxm_eyes_t eyes;
} wxm_cue_t;

/* The full variant's eye acting, cues in time order ending with END at WXM_HOLD_END; NULL (n 0) when quiet. */
const wxm_cue_t *wxm_script(const wxm_moment_t *m, int *n);

#ifdef __cplusplus
}
#endif
