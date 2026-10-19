#include "shown.h"

#include <stdio.h>
#include <string.h>

#include "demo.h"

static bool is_weather(const char *page) { return page && strcmp(page, SHOWN_WEATHER) == 0; }

static bool same(const char *a, int fa, const char *b, int fb)
{
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0 && (!is_weather(a) || fa == fb);
}

bool shown_note(shown_t *s, const char *page, int face, int64_t now_ms)
{
    if (!page || (is_weather(page) && !demo_face_id(face))) return false;
    if (!is_weather(page)) face = -1;
    if (same(page, face, s->page, s->face)) return false;
    s->page = page;
    s->face = face;
    s->at_ms = now_ms;
    return true;
}

int shown_due_ms(const shown_t *s, int64_t now_ms)
{
    if (!s->page || (s->sent && same(s->page, s->face, s->sent_page, s->sent_face))) return -1;
    int64_t left = s->at_ms + SHOWN_DEBOUNCE_MS - now_ms;
    if (left <= 0) return 0;
    return left > SHOWN_DEBOUNCE_MS ? SHOWN_DEBOUNCE_MS : (int)left;
}

const char *shown_page(const char *page, bool usage_open) { return usage_open ? SHOWN_USAGE : page; }

int shown_wait_ms(const shown_t *s, int64_t now_ms, bool online) { return online ? shown_due_ms(s, now_ms) : -1; }

bool shown_idle(const shown_t *s, int64_t now_ms, int *slice_ms)
{
    int due = shown_due_ms(s, now_ms);
    if (due == 0) return true;
    if (due > 0 && due < *slice_ms) *slice_ms = due;
    return false;
}

bool shown_query(const shown_t *s, char *out, size_t cap, shown_val_t *val)
{
    *val = (shown_val_t){.page = s->page, .face = s->face};
    if (!cap) return false;
    out[0] = 0;
    if (!s->page) return true;
    const char *face = is_weather(s->page) ? demo_face_id(s->face) : NULL;
    int n = snprintf(out, cap, "shown=%s%s%s", s->page, face ? ":" : "", face ? face : "");
    if (n < 0 || (size_t)n >= cap) {
        out[0] = 0;
        return false;
    }
    return true;
}

void shown_sent(shown_t *s, shown_val_t val)
{
    if (!val.page) return;
    s->sent = true;
    s->sent_page = val.page;
    s->sent_face = val.face;
}

bool shown_view_url(char *out, size_t cap, const char *view_url, int wait, const char *demo_q, const char *shown_q)
{
    if (!cap) return false;
    int n = snprintf(out, cap, "%s", view_url);
    char sep = '?';
    if (n >= 0 && (size_t)n < cap && wait > 0) {
        n += snprintf(out + n, cap - n, "%cwait=%d", sep, wait);
        sep = '&';
    }
    const char *parts[] = {demo_q, shown_q};
    for (int i = 0; i < 2; i++) {
        if (n < 0 || (size_t)n >= cap) break;
        if (!parts[i] || !parts[i][0]) continue;
        n += snprintf(out + n, cap - n, "%c%s", sep, parts[i]);
        sep = '&';
    }
    if (n < 0 || (size_t)n >= cap) {
        out[0] = 0;
        return false;
    }
    return true;
}
