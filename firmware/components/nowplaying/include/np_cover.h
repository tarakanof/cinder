#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "np.h"
#include "np_ctl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NP_DISC_R 208
#define NP_EDGE_R 222
#define NP_EDGE_HW 6
/* The ring layer owns every pixel from this radius out; inside it is the cover. */
#define NP_EDGE_IN 209
#define NP_HEAD_R 8
#define NP_HEAD_STROKE 2
#define NP_GLOW_R 13
#define NP_GLOW_SIGMA 4
#define NP_GLOW_PX 52
#define NP_GLOW_OPA 0.55f
#define NP_AVATAR_PX 44

#define NP_COL_TRACK 0x262626
#define NP_COL_HEAD 0xF4F4F2
#define NP_COL_PAUSED 0x6A6A6A
#define NP_COL_OFFLINE 0x5A5A5A
#define NP_ACCENT_DEFAULT 0xF4F4F2
#define NP_VOL_TINT 0.6f

typedef enum { NP_RING_PROGRESS, NP_RING_VOLUME } np_ring_mode_t;

typedef struct {
    uint32_t arc0, arc1;
    uint32_t track;
    uint32_t head_fill, head_stroke, glow;
    bool head;
} np_edge_style_t;

/* progress: arc, stroke and glow in col; volume: white at 12 o'clock to NP_VOL_TINT of col at a full turn. */
void np_edge_style(np_edge_style_t *st, np_ring_mode_t mode, uint32_t col);
bool np_edge_style_eq(const np_edge_style_t *a, const np_edge_style_t *b);
/* NP_GLOW_PX x NP_GLOW_PX A8: a disc of NP_GLOW_R blurred by NP_GLOW_SIGMA. */
void np_glow_a8(uint8_t *a);
/* Writes only pixels at r >= NP_EDGE_IN inside rect; the rest (the cover) stays. */
void np_edge_render(const np_ring_t *ring, uint16_t *buf, int w, int h, np_rect_t rect, float frac,
                    const np_edge_style_t *st, const uint8_t *glow);
/* One even-aligned box holding the ring, head and glow between f0 and f1. */
bool np_edge_dirty(const np_ring_t *ring, float f0, float f1, int w, int h, np_rect_t *out);

/* Volume while a turn is recent (NP_VOL_SHOW_S) and the level is known, else progress. */
np_ring_mode_t np_ring_mode(const np_ctl_t *c, double now);

typedef enum { NPF_WAITING, NPF_NOTHING, NPF_PLAYING, NPF_PAUSED, NPF_OFFLINE, NPF_OFFLINE_EMPTY } np_face_state_t;
/* have: Ember sent a block; shown: np_ctl_play_shown; cover_held: a cover picture or the no-cover look is on screen. */
np_face_state_t np_face_state(bool have, np_play_t shown, bool offline, bool cover_held);
bool np_face_live(np_face_state_t s);

typedef struct {
    uint8_t shade;      /* black over the cover disc, 0..255 */
    uint32_t ring;
    uint32_t time_col;
    bool pause_icon;
    bool offline_badge;
    bool avatar_grey;
} np_face_look_t;
void np_face_look(np_face_state_t s, np_ring_mode_t m, uint32_t accent, np_face_look_t *out);

#define NP_SCRIM_LIGHT 102
#define NP_SCRIM_STRONG 191
/* Black at the text rows over the cover: NP_SCRIM_STRONG for a light cover, else NP_SCRIM_LIGHT; with the state's shade
   under it, only what tops the shade up to that level, so the two never stack darker than the larger. */
uint8_t np_scrim_opa(bool light, uint8_t shade);

bool np_face_wants_cover(const np_info_t *np);

#define NP_VINYL_R 122
#define NP_VINYL_DY (-47)
/* M3's still vinyl outline on black, centred at cx, cy; writes only its NP_VINYL_BOX square. */
#define NP_VINYL_BOX 252
void np_vinyl_render(uint16_t *buf, int w, int h, float cx, float cy);

#define NP_ACCENT_VINYL 0xFFB547
#define NP_V15_CX 233
#define NP_V15_CY 200
#define NP_V15_R 135
#define NP_V15_LABEL_R 52
#define NP_V15_FRAMES 8
#define NP_V15_FPS 4
/* M15, the cover face without a cover: record, label turned frame * 45 degrees, tonearm, on black, in face px
   (NP_COVER_PX square). Writes only pixels inside rect and below NP_EDGE_IN, so the ring band stays. */
void np_v15_render(uint16_t *buf, int w, int h, np_rect_t rect, int frame, uint32_t accent);
/* The even-aligned box the label's frames differ in. */
np_rect_t np_v15_label_box(void);
#define NP_V15_TITLE_R 152
#define NP_V15_ARTIST_R 178
#define NP_V15_TEXT_W 300
/* The box the curved title and artist (baselines NP_V15_TITLE_R, NP_V15_ARTIST_R, at most NP_V15_TEXT_W long) are drawn
   in, moved by the burn-in drift (dx, dy up to 8 px either way). */
np_rect_t np_v15_text_box(void);

/* cubic-bezier(0.2, 0.8, 0.2, 1), the canvases' ease-out. */
float np_ease_out(float t);
#define NP_ARRIVE_S 1.32
#define NP_REGROW_S 0.82
#define NP_SWEEP_FPS 30
/* The arrival sweep's share of the value dt seconds into a dur second sweep: eased, in NP_SWEEP_FPS steps, 1 at the end. */
float np_sweep_k(double dt, double dur);

#define NP_NOPIC_GRACE_S 4.0
#define NP_RING_HOLD_S 6.0
#define NP_CHG_HOLD_S 0.25
#define NP_CHG_STEP_S 0.05
#define NP_CHG_BLACK_S 0.0667
typedef struct {
    uint8_t fade;   /* black over the picture, 0..255 */
    float keep;     /* the ring's share before the swap: it unwinds with the fade */
    bool swap;      /* the new picture is in */
    bool done;
} np_change_t;
/* M8, dt seconds after the new picture is ready. from_pic: one was on screen (hold, 3 steps out, a black frame, 3 in);
   else the disc was black and the new one fades in over 3 steps. */
void np_change_at(double dt, bool from_pic, np_change_t *out);
/* The state's shade and the change's fade over each other. */
uint8_t np_shade_mix(uint8_t shade, uint8_t fade);

/* M2/M10's avatar: luma times k, grey. */
void np_grey565(const uint16_t *src, uint16_t *dst, int n, float k);

typedef enum { NP_TAKE_ADOPT, NP_TAKE_DROP } np_take_t;
/* A picture set the client published, against the block the view shows now: a set for an older version or for the
   other face is dropped (a face switch refetches); an empty set never asks for a refetch. */
np_take_t np_art_take(const char *set_version, bool set_cover, const np_info_t *now, bool view_cover);
/* known: a playing or paused block; current: -1 no face yet, 0 the old face, 1 the cover face. Ember sends cover_version
   with every picture to an np_cover knob, so art without it is an older Ember; a block without any art keeps the face. */
bool np_face_kind_cover(bool known, const np_info_t *np, int current_cover);

/* Area average from sp x sp RGB565 down to dp x dp (dp <= sp). */
void np_downscale565(const uint16_t *src, int sp, uint16_t *dst, int dp);

#ifdef __cplusplus
}
#endif
