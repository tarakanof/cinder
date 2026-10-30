#include "controls.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

void ctl_batch_init(ctl_batch_t *b) { *b = (ctl_batch_t){.sent_t = -1e9}; }

void ctl_batch_turn(ctl_batch_t *b, int detents)
{
    int v = b->unsent + detents;
    b->unsent = v < -CTL_STEPS_MAX ? -CTL_STEPS_MAX : v > CTL_STEPS_MAX ? CTL_STEPS_MAX : v;
}

int ctl_batch_flush(ctl_batch_t *b, double now)
{
    int steps = b->unsent;
    if (!steps) return 0;
    b->unsent = 0;
    b->sent_t = now;
    return steps;
}

int ctl_batch_take(ctl_batch_t *b, double now) { return now - b->sent_t < CTL_SEND_S ? 0 : ctl_batch_flush(b, now); }

void ctl_queue_add(ctl_queue_t *q, ctl_gesture_t g, int steps, int64_t at_ms, uint32_t link)
{
    ctl_queued_t *last = q->n ? &q->q[q->n - 1] : NULL;
    if (g == CTL_TURN && last && last->gesture == CTL_TURN && abs(last->steps + steps) <= CTL_STEPS_MAX) {
        last->steps += steps;
        last->at_ms = at_ms;
        if (!last->steps) q->n--;
        return;
    }
    if (q->n == CTL_QUEUE) {
        memmove(&q->q[0], &q->q[1], sizeof q->q[0] * (CTL_QUEUE - 1));
        q->n--;
    }
    q->q[q->n++] = (ctl_queued_t){.gesture = g, .steps = steps, .at_ms = at_ms, .link = link, .id = ++q->next_id};
}

bool ctl_queue_pop(ctl_queue_t *q, ctl_queued_t *out)
{
    if (!q->n) return false;
    *out = q->q[0];
    memmove(&q->q[0], &q->q[1], sizeof q->q[0] * (CTL_QUEUE - 1));
    q->n--;
    return true;
}

int ctl_queue_expire(ctl_queue_t *q, int64_t now_ms)
{
    int k = 0, dropped = 0;
    for (int i = 0; i < q->n; i++) {
        if (now_ms - q->q[i].at_ms > CTL_TTL_MS) dropped++;
        else q->q[k++] = q->q[i];
    }
    q->n = k;
    return dropped;
}

const char *ctl_gesture_name(ctl_gesture_t g)
{
    switch (g) {
    case CTL_PUSH: return "push";
    case CTL_LONG_PUSH: return "long_push";
    case CTL_TURN: return "turn";
    default: return NULL;
    }
}

int ctl_body(ctl_gesture_t g, int steps, char *buf, int cap)
{
    const char *name = ctl_gesture_name(g);
    if (!name || cap <= 0 || (g == CTL_TURN && (!steps || abs(steps) > CTL_STEPS_MAX))) return -1;
    int n = g == CTL_TURN ? snprintf(buf, cap, "{\"gesture\":\"%s\",\"steps\":%d}", name, steps)
                          : snprintf(buf, cap, "{\"gesture\":\"%s\"}", name);
    return n >= 0 && n < cap ? n : -1;
}

void ctl_key(uint32_t boot, uint32_t id, char *buf, size_t cap) { snprintf(buf, cap, "%08" PRIx32 "-%" PRIu32, boot, id); }

static void copy_target(const char *s, char *out, size_t cap)
{
    size_t n = strlen(s);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    }
    memcpy(out, s, n);
    out[n] = 0;
}

ctl_res_t ctl_reply(int status, const char *body, char *target, size_t cap)
{
    if (status < 0) return CTL_RES_NO_CONN;
    if (status == 503) return CTL_RES_NO_MAC;
    if (status == 429) return CTL_RES_TOO_FAST;
    if (status == 403) return CTL_RES_PAGE_OFF;
    if (status == 401) return CTL_RES_NOT_PAIRED;
    if (status < 200 || status >= 300) return CTL_RES_ERROR;
    if (status == 202 && body) {
        cJSON *r = cJSON_Parse(body);
        const char *t = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "target"));
        if (t && t[0] && cap) copy_target(t, target, cap);
        cJSON_Delete(r);
    }
    return CTL_RES_SENT;
}

int ctl_retry_ms(const char *value)
{
    char *end = NULL;
    long s = value ? strtol(value, &end, 10) : 0;
    if (!value || end == value || *end || s < 1) return 1000;
    return (s > CTL_RETRY_MAX_S ? CTL_RETRY_MAX_S : (int)s) * 1000;
}

const char *ctl_note(ctl_res_t res, double age_s, bool offline)
{
    if (offline) return "Offline";
    if (age_s >= CTL_NOTE_S) return "";
    switch (res) {
    case CTL_RES_SENT: return "Sent";
    case CTL_RES_NO_MAC: return "No Mac listening";
    case CTL_RES_TOO_FAST: return "Too fast";
    case CTL_RES_OFFLINE: return "Offline";
    case CTL_RES_PAGE_OFF: return "Page off in Ember";
    case CTL_RES_NOT_PAIRED: return "Not paired";
    case CTL_RES_NO_CONN: return "No connection";
    case CTL_RES_ERROR: return "Ember error";
    default: return "";
    }
}
