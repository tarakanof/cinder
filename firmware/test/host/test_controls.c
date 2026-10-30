#include <stdio.h>
#include <string.h>

#include "controls.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_bodies(void)
{
    static const struct {
        ctl_gesture_t g;
        int steps;
        const char *want;
    } C[] = {
        {CTL_PUSH, 0, "{\"gesture\":\"push\"}"},
        {CTL_LONG_PUSH, 0, "{\"gesture\":\"long_push\"}"},
        {CTL_PUSH, 3, "{\"gesture\":\"push\"}"},
        {CTL_TURN, 1, "{\"gesture\":\"turn\",\"steps\":1}"},
        {CTL_TURN, -20, "{\"gesture\":\"turn\",\"steps\":-20}"},
    };
    char b[CTL_BODY_MAX];
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        int n = ctl_body(C[i].g, C[i].steps, b, sizeof b);
        CHECK(n == (int)strlen(C[i].want) && strcmp(b, C[i].want) == 0, "body %zu: %s", i, b);
    }
    CHECK(ctl_body(CTL_TURN, 0, b, sizeof b) == -1, "a turn of 0 is no request");
    CHECK(ctl_body(CTL_TURN, 21, b, sizeof b) == -1 && ctl_body(CTL_TURN, -21, b, sizeof b) == -1, "past the server's ±20");
    CHECK(ctl_body((ctl_gesture_t)9, 0, b, sizeof b) == -1, "unknown gesture");
    CHECK(ctl_body(CTL_TURN, -20, b, 10) == -1, "short buffer");
}

static void test_batch(void)
{
    ctl_batch_t b;
    ctl_batch_init(&b);
    CHECK(ctl_batch_take(&b, 0) == 0, "nothing to send");
    ctl_batch_turn(&b, 2);
    CHECK(ctl_batch_take(&b, 0) == 2, "the first detents go at once");
    ctl_batch_turn(&b, 1);
    ctl_batch_turn(&b, 3);
    CHECK(ctl_batch_take(&b, 0.24) == 0, "held inside the throttle");
    CHECK(ctl_batch_take(&b, 0.25) == 4, "coalesced at 0.25 s");
    ctl_batch_turn(&b, 2);
    ctl_batch_turn(&b, -2);
    CHECK(ctl_batch_take(&b, 1) == 0 && b.sent_t == 0.25, "a turn back cancels: no send, no throttle");
    ctl_batch_turn(&b, 100);
    CHECK(b.unsent == CTL_STEPS_MAX && ctl_batch_take(&b, 2) == CTL_STEPS_MAX, "capped at +20");
    ctl_batch_turn(&b, -100);
    CHECK(ctl_batch_flush(&b, 2.01) == -CTL_STEPS_MAX, "capped at -20; a flush skips the wait");
    ctl_batch_turn(&b, 1);
    CHECK(ctl_batch_take(&b, 2.2) == 0, "a flush restarts the throttle");

    ctl_batch_init(&b);
    int sends = 0, steps = 0;
    for (int f = 0; f < 600; f++) {
        double t = f / 30.0;
        ctl_batch_turn(&b, 1);
        int s = ctl_batch_take(&b, t);
        sends += s != 0;
        steps += s;
    }
    CHECK(sends <= 81 && steps + b.unsent == 600, "20 s of turning: %d sends, %d steps", sends, steps);
}

static void test_queue(void)
{
    ctl_queue_t q = {0};
    ctl_queued_t e;
    CHECK(!ctl_queue_pop(&q, &e), "empty");
    ctl_queue_add(&q, CTL_TURN, 3, 10, 7);
    ctl_queue_add(&q, CTL_TURN, 2, 20, 9);
    CHECK(q.n == 1 && q.q[0].steps == 5 && q.q[0].at_ms == 20 && q.q[0].link == 7 && q.q[0].id == 1,
          "turns merge, the id and the older link stay");
    ctl_queue_add(&q, CTL_TURN, -5, 30, 9);
    CHECK(q.n == 0, "turns that cancel leave no request: steps 0 is a 400");
    ctl_queue_add(&q, CTL_TURN, 15, 40, 0);
    ctl_queue_add(&q, CTL_TURN, 10, 41, 0);
    CHECK(q.n == 2 && q.q[0].steps == 15 && q.q[1].steps == 10, "a merge past 20 starts a new request");
    ctl_queue_add(&q, CTL_PUSH, 0, 42, 0);
    ctl_queue_add(&q, CTL_TURN, 1, 43, 0);
    CHECK(q.n == 4 && q.q[3].steps == 1, "a turn does not merge across a push");
    ctl_queue_pop(&q, &e);
    CHECK(e.steps == 15 && q.n == 3 && q.q[0].steps == 10, "pop: oldest first");
    q = (ctl_queue_t){0};
    for (int i = 0; i < CTL_QUEUE + 2; i++) ctl_queue_add(&q, CTL_PUSH, 0, i, 0);
    CHECK(q.n == CTL_QUEUE && q.q[0].id == 3 && q.q[CTL_QUEUE - 1].id == CTL_QUEUE + 2, "full: drops the oldest");
    q = (ctl_queue_t){0};
    ctl_queue_add(&q, CTL_PUSH, 0, 0, 0);
    ctl_queue_add(&q, CTL_LONG_PUSH, 0, 500, 0);
    CHECK(ctl_queue_expire(&q, CTL_TTL_MS) == 0 && q.n == 2, "at the TTL it stays");
    CHECK(ctl_queue_expire(&q, CTL_TTL_MS + 1) == 1 && q.n == 1 && q.q[0].gesture == CTL_LONG_PUSH, "past it, dropped");
}

static void test_key(void)
{
    char a[CTL_KEY_MAX], b[CTL_KEY_MAX];
    ctl_key(0xdeadbeef, 7, a, sizeof a);
    ctl_key(0xdeadbeef, 7, b, sizeof b);
    CHECK(strcmp(a, "deadbeef-7") == 0 && strcmp(a, b) == 0, "same gesture, same key: %s", a);
    ctl_key(0xdeadbeef, 8, b, sizeof b);
    CHECK(strcmp(a, b) != 0, "next gesture, new key");
    ctl_key(0xffffffff, 0xffffffff, a, sizeof a);
    CHECK(strcmp(a, "ffffffff-4294967295") == 0, "the longest key fits: %s", a);
}

static void test_reply(void)
{
    char t[CTL_TARGET_MAX] = "M4";
    CHECK(ctl_reply(202, "{\"status\":\"queued\",\"target\":\"Studio\"}", t, sizeof t) == CTL_RES_SENT && strcmp(t, "Studio") == 0, "202 names the Mac");
    CHECK(ctl_reply(200, "{\"status\":\"duplicate\"}", t, sizeof t) == CTL_RES_SENT && strcmp(t, "Studio") == 0, "200 duplicate: sent, Mac kept");
    CHECK(ctl_reply(202, "{\"status\":\"queued\"}", t, sizeof t) == CTL_RES_SENT && strcmp(t, "Studio") == 0, "no target: kept");
    CHECK(ctl_reply(202, "{\"target\":\"\"}", t, sizeof t) == CTL_RES_SENT && strcmp(t, "Studio") == 0, "empty target: kept");
    CHECK(ctl_reply(202, "{\"target\":5}", t, sizeof t) == CTL_RES_SENT && strcmp(t, "Studio") == 0, "bad target: kept");
    CHECK(ctl_reply(202, "not json", t, sizeof t) == CTL_RES_SENT && strcmp(t, "Studio") == 0, "bad body: still queued");
    CHECK(ctl_reply(202, NULL, t, sizeof t) == CTL_RES_SENT, "no body");
    CHECK(ctl_reply(503, "{\"error\":\"x\"}", t, sizeof t) == CTL_RES_NO_MAC, "503");
    CHECK(ctl_reply(429, "", t, sizeof t) == CTL_RES_TOO_FAST, "429");
    CHECK(ctl_reply(403, "", t, sizeof t) == CTL_RES_PAGE_OFF, "403");
    CHECK(ctl_reply(401, "", t, sizeof t) == CTL_RES_NOT_PAIRED, "401");
    CHECK(ctl_reply(500, "", t, sizeof t) == CTL_RES_ERROR, "500");
    CHECK(ctl_reply(400, "", t, sizeof t) == CTL_RES_ERROR, "400");
    CHECK(ctl_reply(-1, "junk", t, sizeof t) == CTL_RES_NO_CONN, "network");
    CHECK(strcmp(t, "Studio") == 0, "failures keep the Mac");

    char small[6];
    ctl_reply(202, "{\"target\":\"caf\xc3\xa9s\"}", small, sizeof small);
    CHECK(strcmp(small, "caf\xc3\xa9") == 0, "fits exactly: %s", small);
    ctl_reply(202, "{\"target\":\"abcd\xc3\xa9\"}", small, sizeof small);
    CHECK(strcmp(small, "abcd") == 0, "cut before a split character: %s", small);
}

static void test_retry(void)
{
    CHECK(ctl_retry_ms("1") == 1000 && ctl_retry_ms("7") == 7000, "seconds");
    CHECK(ctl_retry_ms("3600") == CTL_RETRY_MAX_S * 1000, "capped");
    CHECK(ctl_retry_ms("0") == 1000 && ctl_retry_ms("-3") == 1000, "at least 1 s");
    CHECK(ctl_retry_ms("Fri, 30 Oct 2026 10:00:00 GMT") == 1000 && ctl_retry_ms("") == 1000 && ctl_retry_ms("2x") == 1000 && ctl_retry_ms(NULL) == 1000, "not seconds: 1 s");
}

static void test_note(void)
{
    CHECK(strcmp(ctl_note(CTL_RES_SENT, 0, false), "Sent") == 0, "sent");
    CHECK(strcmp(ctl_note(CTL_RES_NO_MAC, 2.4, false), "No Mac listening") == 0, "503");
    CHECK(strcmp(ctl_note(CTL_RES_TOO_FAST, 0, false), "Too fast") == 0, "429");
    CHECK(strcmp(ctl_note(CTL_RES_OFFLINE, 0, false), "Offline") == 0, "dropped offline");
    CHECK(strcmp(ctl_note(CTL_RES_SENT, 0, true), "Offline") == 0, "offline wins");
    CHECK(strcmp(ctl_note(CTL_RES_NONE, 9, true), "Offline") == 0, "offline stays");
    CHECK(strcmp(ctl_note(CTL_RES_NO_MAC, CTL_NOTE_S, false), "") == 0, "gone after the note time");
    CHECK(strcmp(ctl_note(CTL_RES_NONE, 0, false), "") == 0, "nothing yet");
    for (int r = CTL_RES_SENT; r <= CTL_RES_ERROR; r++) CHECK(ctl_note((ctl_res_t)r, 0, false)[0], "result %d has a line", r);
}

int main(void)
{
    test_bodies();
    test_batch();
    test_queue();
    test_key();
    test_reply();
    test_retry();
    test_note();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("controls: ok\n");
    return 0;
}
