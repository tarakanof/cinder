#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "room_face.h"
#include "wx_moment.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The bot's room moments: stale air (WXM_ROOM_STALE) and fresh air after it (WXM_ROOM_FRESH). Pure; the glue is in
   main/moments.c, the sprite and the timeline are the weather moments'. */

typedef struct {
    bool stale;         /* the air is stale: from ROOM_AQI_POOR, kept until ROOM_AQI_CLEAR */
    bool owed;          /* a stale-air moment played while it lasts: fresh air follows when it ends */
    bool fresh;         /* a fresh-air moment is queued */
    double fresh_at;
    double stale_at;    /* the last stale-air moment, -INFINITY none */
    bool shown_was;
    int mood_was, bot_mood_was;
    double steady_at;
} rm_trigger_t;

typedef struct {
    double t;           /* monotonic s */
    bool valid;         /* an air reading under 60 s old with the ENS160's validity normal */
    int aqi;            /* 1..5 while valid */
    wxm_mood_t mood;
    int bot_mood;       /* the bot's own mood: any change restarts the settle */
    bool shown;         /* the bot page is on screen and the bot is awake */
    bool blocked;       /* a moment plays or bakes, a music moment plays or played < 5 min ago, a chase, quiet hours */
    bool on;            /* room.enabled and room.moments */
    bool weather_first; /* a weather change moment could play now (wxm_change_due): it goes first */
    double last_at;     /* the last moment of any trigger (wxm_trigger_t.last_at) */
} rm_in_t;

typedef struct {
    wxm_kind_t kind;    /* WXM_ROOM_STALE or WXM_ROOM_FRESH */
    bool quiet;
    int aqi;
} rm_out_t;

void rm_init(rm_trigger_t *tr);
/* Every frame. True and out filled when a moment is due now; rm_shown records it, so one that could not start is
   offered again. Off resets the state. */
bool rm_tick(rm_trigger_t *tr, const rm_in_t *in, rm_out_t *out);
void rm_shown(rm_trigger_t *tr, const rm_out_t *out, double t);

#ifdef __cplusplus
}
#endif
