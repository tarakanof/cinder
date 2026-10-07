#include <stdio.h>
#include <string.h>

#include "fail_streak.h"
#include "http_retry.h"
#include "view_wait.h"
#include "wifi_backoff.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_streak(void)
{
    fail_streak_t s;
    fs_init(&s);
    CHECK(fs_note(&s, true, 0) == FS_QUIET, "success without a streak is quiet");
    CHECK(fs_note(&s, false, 1000) == FS_STARTED, "first failure starts a streak");
    CHECK(fs_fails(&s) == 1, "one failure");
    CHECK(fs_note(&s, false, 3000) == FS_QUIET, "second failure is quiet");
    CHECK(fs_note(&s, false, 60999) == FS_QUIET, "quiet until the reminder");
    CHECK(fs_note(&s, false, 61000) == FS_STILL, "reminder after FS_REMIND_MS");
    CHECK(fs_note(&s, false, 62000) == FS_QUIET, "quiet after the reminder");
    CHECK(fs_note(&s, false, 121000) == FS_STILL, "next reminder counts from the last log");
    CHECK(fs_fails(&s) == 6, "six failures: %d", fs_fails(&s));
    CHECK(fs_note(&s, true, 125000) == FS_RECOVERED, "success ends the streak");
    CHECK(s.ended_fails == 6 && s.ended_ms == 124000, "ended %d after %lld ms", s.ended_fails, (long long)s.ended_ms);
    CHECK(fs_fails(&s) == 0, "reset");
    CHECK(fs_note(&s, true, 126000) == FS_QUIET, "quiet after recovery");
    CHECK(fs_note(&s, false, 127000) == FS_STARTED, "a new streak logs again");
    CHECK(fs_note(&s, true, 128000) == FS_RECOVERED && s.ended_fails == 1, "a one-failure streak");
}

static void test_retry(void)
{
    for (int w = HTTP_FAIL_OPEN; w <= HTTP_FAIL_BODY; w++) {
        CHECK(!http_should_retry(false, true, (http_fail_t)w), "new socket: no retry (%s)", http_fail_name((http_fail_t)w));
        CHECK(!http_should_retry(false, false, (http_fail_t)w), "new socket POST: no retry (%s)", http_fail_name((http_fail_t)w));
        CHECK(http_should_retry(true, true, (http_fail_t)w), "reused, idempotent: retry (%s)", http_fail_name((http_fail_t)w));
    }
    CHECK(http_should_retry(true, false, HTTP_FAIL_OPEN), "POST: open failed, nothing sent");
    CHECK(http_should_retry(true, false, HTTP_FAIL_CLOSED), "POST: stale socket closed before a status line");
    CHECK(!http_should_retry(true, false, HTTP_FAIL_SEND), "POST: body write failed: maybe applied");
    CHECK(!http_should_retry(true, false, HTTP_FAIL_TIMEOUT), "POST: timeout: maybe applied");
    CHECK(!http_should_retry(true, false, HTTP_FAIL_NO_STATUS), "POST: no status: maybe applied");
    CHECK(!http_should_retry(true, false, HTTP_FAIL_BODY), "POST: cut response: applied");
}

static void test_backoff(void)
{
    static const uint32_t step[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
    for (unsigned i = 0; i < sizeof step / sizeof step[0]; i++) {
        uint32_t lo = wifi_backoff_ms(i, 0), mid = wifi_backoff_ms(i, 0x80000000u), hi = wifi_backoff_ms(i, ~0u);
        CHECK(lo == step[i] - step[i] / 4, "attempt %u low: %u", i, (unsigned)lo);
        if (step[i] < WIFI_BACKOFF_MAX_MS) {
            CHECK(mid == step[i], "attempt %u mid: %u", i, (unsigned)mid);
            CHECK(hi >= step[i] && hi < step[i] + step[i] / 4, "attempt %u high: %u", i, (unsigned)hi);
        } else {
            CHECK(mid == WIFI_BACKOFF_MAX_MS * 7 / 8, "at the cap, mid: %u", (unsigned)mid);
            CHECK(hi < WIFI_BACKOFF_MAX_MS && hi >= WIFI_BACKOFF_MAX_MS - 1, "at the cap, high: %u", (unsigned)hi);
        }
    }
    CHECK(wifi_backoff_ms(1000, ~0u) < WIFI_BACKOFF_MAX_MS, "large attempt capped");
    {
        uint32_t x = 777, at_cap = 0, mn = ~0u;
        for (int i = 0; i < 1000; i++) {
            x = x * 1664525u + 1013904223u;
            uint32_t v = wifi_backoff_ms(9, x);
            at_cap += v == WIFI_BACKOFF_MAX_MS;
            mn = v < mn ? v : mn;
        }
        CHECK(at_cap == 0 && mn < 23000, "cap spread: %u at the cap, min %u", (unsigned)at_cap, (unsigned)mn);
    }
    CHECK(wifi_backoff_ms(~0u, 0) == WIFI_BACKOFF_MAX_MS * 3 / 4, "no overflow");
    uint32_t mn = ~0u, mx = 0, x = 12345;
    for (int i = 0; i < 1000; i++) {
        x = x * 1664525u + 1013904223u;
        uint32_t v = wifi_backoff_ms(2, x);
        mn = v < mn ? v : mn;
        mx = v > mx ? v : mx;
    }
    CHECK(mn >= 3000 && mn < 3100 && mx < 5000 && mx > 4900, "spread %u..%u", (unsigned)mn, (unsigned)mx);

    CHECK(wifi_disc_class(203) == WIFI_DISC_ASSOC && wifi_disc_class(201) == WIFI_DISC_NO_AP, "203, 201");
    CHECK(wifi_disc_class(15) == WIFI_DISC_AUTH && wifi_disc_class(204) == WIFI_DISC_AUTH, "15, 204");
    CHECK(wifi_disc_class(200) == WIFI_DISC_LINK_LOST && wifi_disc_class(8) == WIFI_DISC_LEAVE, "200, 8");
    CHECK(wifi_disc_class(1) == WIFI_DISC_OTHER && wifi_disc_class(-1) == WIFI_DISC_OTHER, "other");
    CHECK(!strcmp(wifi_disc_name(WIFI_DISC_ASSOC), "assoc") && !strcmp(wifi_disc_name((wifi_disc_t)99), "other"), "names");
    CHECK(wifi_reconnect_ms(0, 203, 0x80000000u) == WIFI_RETRY_FAST_MS, "203: fast first retry");
    CHECK(wifi_reconnect_ms(0, 200, 0) < WIFI_RETRY_FAST_MS && wifi_reconnect_ms(0, 8, ~0u) <= WIFI_RETRY_FAST_MS * 5 / 4,
          "200, 8: fast");
    CHECK(wifi_reconnect_ms(1, 203, 0x80000000u) == 2000, "203 again: backoff");
    CHECK(wifi_reconnect_ms(0, 201, 0x80000000u) == WIFI_RETRY_FAST_MS, "201: fast first retry");
    CHECK(wifi_reconnect_ms(0, 15, 0x80000000u) == 1000 && wifi_reconnect_ms(0, 1, 0x80000000u) == 1000,
          "15, other: normal backoff");
    CHECK(!wifi_scan_all_channels(6, 0) && !wifi_scan_all_channels(6, 1), "hint: fast scan first, twice");
    CHECK(wifi_scan_all_channels(6, 2) && wifi_scan_all_channels(0, 0) && wifi_scan_all_channels(-1, 3),
          "retry or no hint: all channels");
}

static void test_view_wait(void)
{
    CHECK(view_wait_cap("25") == 25, "cap 25");
    CHECK(view_wait_cap(NULL) == 0 && view_wait_cap("") == 0, "absent: no long-poll");
    CHECK(view_wait_cap("2x") == 0 && view_wait_cap("-5") == 0 && view_wait_cap(" 25") == 0, "invalid");
    CHECK(view_wait_cap("3601") == 0 && view_wait_cap("99999999999999") == 0, "implausible");
    CHECK(view_wait_cap("0") == 0, "zero: off");

    CHECK(view_wait_s(25, true, 60000, false) == 25, "server cap");
    CHECK(view_wait_s(60, true, 60000, false) == VIEW_WAIT_MAX_S, "knob cap");
    CHECK(view_wait_s(10, true, 60000, false) == 10, "a lower server cap");
    CHECK(view_wait_s(0, true, 60000, false) == 0, "no advertisement: plain poll");
    CHECK(view_wait_s(25, false, 60000, false) == 0, "no cached view: plain poll");
    CHECK(view_wait_s(25, true, 7900, false) == 7, "never past the next checkin (whole seconds down)");
    CHECK(view_wait_s(25, true, 1999, false) == 0, "checkin under VIEW_WAIT_MIN_S away: plain poll");
    CHECK(view_wait_s(25, true, 2000, false) == 2, "exactly the minimum");
    CHECK(view_wait_s(25, true, -500, false) == 0, "checkin overdue: plain poll");

    CHECK(view_wait_s(25, true, 60000, true) == 0, "after a failure: plain polls until an answer");

    CHECK(view_wait_clock_sample(0, 30000), "plain poll: always");
    CHECK(view_wait_clock_sample(25, 120), "a quick waited answer is tight");
    CHECK(!view_wait_clock_sample(25, 25010), "a waited answer bounds the clock to the whole wait");

    CHECK(!http_should_retry_wait(true, true, true, HTTP_FAIL_TIMEOUT), "waited timeout: no retry");
    CHECK(!http_should_retry_wait(true, true, true, HTTP_FAIL_BODY), "waited cut body: no retry");
    CHECK(!http_should_retry_wait(true, true, true, HTTP_FAIL_NO_STATUS), "waited no status: no retry");
    CHECK(http_should_retry_wait(true, true, true, HTTP_FAIL_CLOSED), "reused socket closed: retry");
    CHECK(http_should_retry_wait(true, true, true, HTTP_FAIL_OPEN), "reused socket failed to open: retry");
    CHECK(http_should_retry_wait(true, true, true, HTTP_FAIL_SEND), "reused socket failed to send: retry");
    CHECK(!http_should_retry_wait(false, true, true, HTTP_FAIL_CLOSED), "new connection: no retry");
    CHECK(http_should_retry_wait(true, true, false, HTTP_FAIL_TIMEOUT) == http_should_retry(true, true, HTTP_FAIL_TIMEOUT),
          "not waited: the #18 rule");

    CHECK(view_wait_budget_ms(25) == 35000, "wait + slack");
    CHECK(view_wait_budget_ms(0) == VIEW_WAIT_SLACK_MS, "plain: the slack alone");

    CHECK(view_rearm_ms(200, 25, 300, 2000) == VIEW_REARM_MIN_MS, "a change: re-arm after the floor");
    CHECK(view_rearm_ms(304, 25, 25010, 2000) == 0, "the wait ran out: re-arm at once");
    CHECK(view_rearm_ms(304, 25, 40, 2000) == 2000, "a 304 that did not wait: back off");
    CHECK(view_rearm_ms(-1, 25, 100, 2000) == 2000, "network error: back off");
    CHECK(view_rearm_ms(429, 25, 10, 2000) == 2000, "429: back off");
    CHECK(view_rearm_ms(503, 25, 10, 2000) == 2000, "5xx: back off");
    CHECK(view_rearm_ms(200, 0, 30, 2000) == 2000, "plain poll: poll_ms as before");
    CHECK(view_rearm_ms(304, 0, 30, 2000) == 2000, "plain 304: poll_ms as before");
}

int main(void)
{
    test_streak();
    test_retry();
    test_backoff();
    test_view_wait();
    if (failures) {
        printf("net: %d failure(s)\n", failures);
        return 1;
    }
    printf("net: all tests passed\n");
    return 0;
}
