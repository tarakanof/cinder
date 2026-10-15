/* The np_cover now-playing face (M1, M2, M3, M7, M10). All functions: LVGL task only. */
#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "np.h"
#include "np_cover.h"
#include "nowplaying_client.h"

#define NPC_ROUND_OPA 150

typedef enum { NPC_BADGE_NONE, NPC_BADGE_NUMBER, NPC_BADGE_ICON, NPC_BADGE_ERROR } npc_badge_t;

typedef struct {
    double now;
    np_face_state_t state;
    np_ring_mode_t ring;
    const np_info_t *np;
    bool text_fresh;
    long long pos_ms, dur_ms;
    int vol;
    npc_badge_t badge;
    const char *badge_text;
    bool dim;
    bool controls;
    int dx, dy;
    int text_out;
} npc_frame_t;

/* Once, at np_view_create. */
bool npc_init(void);
void npc_build(lv_obj_t *root);
/* Frees the pictures; the caller deletes the objects with root's children. */
void npc_destroy(void);
/* Takes cover and avatar out of in. */
void npc_adopt(np_art_set_t *in);
bool npc_has_cover(void);
/* M15 is on screen. */
bool npc_vinyl(void);
void npc_update(const npc_frame_t *f);
const char *npc_time_text(void);
/* A TS_CTL_PX dark disc on the touch-control row, dx from its centre, hidden; child 0 is its centred label (Montserrat 30). The controls and both faces' badge use it. */
lv_obj_t *npc_round(lv_obj_t *root, int dx);
void npc_round_lit(lv_obj_t *o, bool lit);
void npc_fit_text(char *txt, const lv_font_t *font, int max_w);
