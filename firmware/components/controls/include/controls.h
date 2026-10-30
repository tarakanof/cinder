#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CTL_SEND_S 0.25
#define CTL_STEPS_MAX 20
#define CTL_QUEUE 6
#define CTL_TTL_MS 3000
#define CTL_NOTE_S 2.5
#define CTL_BODY_MAX 40
#define CTL_KEY_MAX 24
#define CTL_TARGET_MAX 48
#define CTL_RETRY_MAX_S 60

typedef enum { CTL_PUSH, CTL_LONG_PUSH, CTL_TURN } ctl_gesture_t;

typedef enum {
    CTL_RES_NONE,
    CTL_RES_SENT,
    CTL_RES_NO_MAC,
    CTL_RES_TOO_FAST,
    CTL_RES_OFFLINE,
    CTL_RES_PAGE_OFF,
    CTL_RES_NOT_PAIRED,
    CTL_RES_NO_CONN,
    CTL_RES_ERROR,
} ctl_res_t;

/* Detents wait here until CTL_SEND_S after the last send; past ±CTL_STEPS_MAX they are dropped. */
typedef struct {
    int unsent;
    double sent_t;
} ctl_batch_t;

void ctl_batch_init(ctl_batch_t *b);
void ctl_batch_turn(ctl_batch_t *b, int detents);
/* Steps to send at now, 0 for none. */
int ctl_batch_take(ctl_batch_t *b, double now);
/* As ctl_batch_take without the wait: the steps go before a push that follows them. */
int ctl_batch_flush(ctl_batch_t *b, double now);

/* link: ember_client_link_word() at the gesture. id names the Idempotency-Key; a request resent on a new socket keeps it. */
typedef struct {
    ctl_gesture_t gesture;
    int steps;
    int64_t at_ms;
    uint32_t link;
    uint32_t id;
} ctl_queued_t;

typedef struct {
    ctl_queued_t q[CTL_QUEUE];
    int n;
    uint32_t next_id;
} ctl_queue_t;

/* A turn merges into a queued turn while the sum fits ±CTL_STEPS_MAX and leaves the queue at 0; the merged entry keeps the older link stamp. A full queue drops the oldest. */
void ctl_queue_add(ctl_queue_t *q, ctl_gesture_t g, int steps, int64_t at_ms, uint32_t link);
bool ctl_queue_pop(ctl_queue_t *q, ctl_queued_t *out);
/* Drops entries older than CTL_TTL_MS; returns how many. */
int ctl_queue_expire(ctl_queue_t *q, int64_t now_ms);

const char *ctl_gesture_name(ctl_gesture_t g);
/* Bytes written, or -1 for a turn outside ±1..CTL_STEPS_MAX, an unknown gesture or a short buffer. */
int ctl_body(ctl_gesture_t g, int steps, char *buf, int cap);
void ctl_key(uint32_t boot, uint32_t id, char *buf, size_t cap);

/* status: HTTP status, -1 network error. target gets a 202's Mac name, whole UTF-8 characters only, and is left alone otherwise. */
ctl_res_t ctl_reply(int status, const char *body, char *target, size_t cap);
/* Milliseconds from a Retry-After in seconds, 1 to CTL_RETRY_MAX_S; anything else is 1 s. */
int ctl_retry_ms(const char *value);
/* The result line: "Offline" while offline, else the result for CTL_NOTE_S, else "". */
const char *ctl_note(ctl_res_t res, double age_s, bool offline);

#ifdef __cplusplus
}
#endif
