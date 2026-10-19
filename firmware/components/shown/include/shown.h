#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHOWN_DEBOUNCE_MS 300
#define SHOWN_PAGE_MAX 24
#define SHOWN_QUERY_MAX (sizeof "shown=:" + SHOWN_PAGE_MAX + 8)
#define SHOWN_WEATHER "weather"

typedef struct {
    const char *page;
    int face;
    int64_t at_ms;
    const char *sent_page;
    int sent_face;
    bool sent;
} shown_t;

#define SHOWN_INIT {.face = -1, .sent_face = -1}

/* page: an id in static storage; face: wx_face_id_t, read on SHOWN_WEATHER only. A NULL page, or SHOWN_WEATHER with a face
   out of range (none resolved yet), keeps the last value. True when the value changed. */
bool shown_note(shown_t *s, const char *page, int face, int64_t now_ms);
/* ms until the value settles unsent (0: poll now), -1 when nothing new is to be sent. */
int shown_due_ms(const shown_t *s, int64_t now_ms);
/* Long-poll idle slice: true ends the request; else *slice_ms is cut to the debounce left. */
bool shown_idle(const shown_t *s, int64_t now_ms, int *slice_ms);
/* "shown=<page>[:<face>]" for the next view request ("" before any page) and marks it sent. False when cap is too small. */
bool shown_query(shown_t *s, char *out, size_t cap);
/* view_url, then wait (> 0), demo_q and shown_q ("" or NULL: left out) as its query. False when cap is too small. */
bool shown_view_url(char *out, size_t cap, const char *view_url, int wait, const char *demo_q, const char *shown_q);

#ifdef __cplusplus
}
#endif
