#include <stdio.h>
#include <string.h>

#include "pages.h"
#include "shown.h"
#include "wx_faces.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *query(shown_t *s)
{
    static char q[SHOWN_QUERY_MAX];
    CHECK(shown_query(s, q, sizeof q), "query fits");
    return q;
}

static bool ember_pattern(const char *q)
{
    if (strncmp(q, "shown=", 6) != 0) return false;
    const char *v = q + 6, *c = strchr(v, ':');
    size_t page = c ? (size_t)(c - v) : strlen(v);
    if (page < 1 || page > SHOWN_PAGE_MAX || v[0] < 'a' || v[0] > 'z') return false;
    for (size_t i = 1; i < page; i++)
        if (!((v[i] >= 'a' && v[i] <= 'z') || (v[i] >= '0' && v[i] <= '9') || v[i] == '_')) return false;
    if (!c) return true;
    size_t face = strlen(c + 1);
    if (face < 1 || face > 8) return false;
    for (size_t i = 0; i < face; i++)
        if (c[1 + i] < 'a' || c[1 + i] > 'z') return false;
    return true;
}

static void test_query(void)
{
    shown_t s = SHOWN_INIT;
    CHECK(!strcmp(query(&s), ""), "nothing before the first page");
    CHECK(shown_due_ms(&s, 0) == -1, "nothing due before the first page");
    CHECK(!shown_note(&s, SHOWN_WEATHER, -1, 0), "weather before its face resolves is no value");
    CHECK(!strcmp(query(&s), ""), "still nothing: %s", query(&s));
    CHECK(shown_note(&s, "bot", 3, 0), "bot is a value");
    CHECK(!strcmp(query(&s), "shown=bot"), "no face off weather: %s", query(&s));
    const char *want[WXF_N] = {"shown=weather:temp", "shown=weather:rain", "shown=weather:cast", "shown=weather:wind",
                               "shown=weather:days", "shown=weather:sun",  "shown=weather:air"};
    for (int f = 0; f < WXF_N; f++) {
        CHECK(shown_note(&s, SHOWN_WEATHER, f, 0), "face %d is a change", f);
        const char *q = query(&s);
        CHECK(!strcmp(q, want[f]), "face %d: %s", f, q);
        CHECK(ember_pattern(q), "face %d matches Ember's pattern: %s", f, q);
    }
    CHECK(!shown_note(&s, SHOWN_WEATHER, -1, 0), "an unresolved face keeps the last one");
    CHECK(!shown_note(&s, SHOWN_WEATHER, WXF_N, 0), "a face out of range keeps the last one");
    CHECK(!shown_note(&s, NULL, 0, 0), "no page keeps the last one");
    CHECK(!strcmp(query(&s), "shown=weather:air"), "kept: %s", query(&s));
    CHECK(shown_note(&s, "nowplaying", 2, 0) && !shown_note(&s, "nowplaying", 5, 0), "the face is ignored off weather");
    for (int i = 0; i < PAGES_N; i++) {
        shown_t p = SHOWN_INIT;
        shown_note(&p, PAGES[i].id, WXF_TEMP, 0);
        const char *q = query(&p);
        CHECK(ember_pattern(q), "page %s matches Ember's pattern: %s", PAGES[i].id, q);
    }
    shown_t w = SHOWN_INIT;
    shown_note(&w, SHOWN_WEATHER, WXF_SUN, 0);
    char small[sizeof "shown=weather:su"];
    CHECK(!shown_query(&w, small, sizeof small) && !small[0], "too small: empty, false");
    CHECK(shown_due_ms(&w, 1000) == 0, "a failed query sends nothing");
}

static void test_debounce(void)
{
    shown_t s = SHOWN_INIT;
    shown_note(&s, "bot", -1, 1000);
    CHECK(shown_due_ms(&s, 1000) == SHOWN_DEBOUNCE_MS, "due after the debounce: %d", shown_due_ms(&s, 1000));
    CHECK(shown_due_ms(&s, 1200) == 100, "100 ms left: %d", shown_due_ms(&s, 1200));
    CHECK(shown_due_ms(&s, 1300) == 0, "due at 300 ms");
    CHECK(shown_due_ms(&s, 900) == SHOWN_DEBOUNCE_MS, "a clock before the change waits the full debounce");
    shown_note(&s, "pomodoro", -1, 1250);
    CHECK(shown_due_ms(&s, 1300) == 250, "a new change restarts the debounce: %d", shown_due_ms(&s, 1300));
    CHECK(!strcmp(query(&s), "shown=pomodoro"), "the latest value goes out");
    CHECK(shown_due_ms(&s, 5000) == -1, "sent: nothing due");
    CHECK(!shown_note(&s, "pomodoro", -1, 6000), "the same page every frame is no change");
    CHECK(shown_due_ms(&s, 7000) == -1, "still nothing due");
    CHECK(shown_note(&s, "bot", -1, 8000) && shown_note(&s, "pomodoro", -1, 8100), "away and back");
    CHECK(shown_due_ms(&s, 9000) == -1, "back to the sent value: no poll");
}

static void test_idle(void)
{
    shown_t s = SHOWN_INIT;
    int slice = 1000;
    CHECK(!shown_idle(&s, 0, &slice) && slice == 1000, "nothing to send: slice kept");
    shown_note(&s, SHOWN_WEATHER, WXF_TEMP, 0);
    slice = 1000;
    CHECK(!shown_idle(&s, 100, &slice) && slice == 200, "debouncing: slice cut to %d", slice);
    slice = 50;
    CHECK(!shown_idle(&s, 100, &slice) && slice == 50, "a shorter slice stays");
    CHECK(shown_idle(&s, 300, &slice), "settled: the long-poll ends");
    query(&s);
    slice = 1000;
    CHECK(!shown_idle(&s, 400, &slice) && slice == 1000, "sent: the long-poll stays");
}

static void test_url(void)
{
    char u[128];
    CHECK(shown_view_url(u, sizeof u, "http://e/v", 0, "", "") && !strcmp(u, "http://e/v"), "bare: %s", u);
    CHECK(shown_view_url(u, sizeof u, "http://e/v", 25, NULL, NULL) && !strcmp(u, "http://e/v?wait=25"), "wait: %s", u);
    CHECK(shown_view_url(u, sizeof u, "http://e/v", 0, "", "shown=bot") && !strcmp(u, "http://e/v?shown=bot"), "shown: %s", u);
    CHECK(shown_view_url(u, sizeof u, "http://e/v", 0, "demo_id=a&demo_state=playing", "") &&
              !strcmp(u, "http://e/v?demo_id=a&demo_state=playing"),
          "demo: %s", u);
    CHECK(shown_view_url(u, sizeof u, "http://e/v", 25, "", "shown=weather:wind") && !strcmp(u, "http://e/v?wait=25&shown=weather:wind"),
          "wait and shown: %s", u);
    CHECK(shown_view_url(u, sizeof u, "http://e/v", 9, "demo_id=a&demo_state=done", "shown=weather:rain") &&
              !strcmp(u, "http://e/v?wait=9&demo_id=a&demo_state=done&shown=weather:rain"),
          "all three: %s", u);
    CHECK(shown_view_url(u, sizeof u, "http://e/v", 0, "demo_id=a&demo_state=done", "shown=bot") &&
              !strcmp(u, "http://e/v?demo_id=a&demo_state=done&shown=bot"),
          "demo and shown: %s", u);
    char small[sizeof "http://e/v?wait=25&shown=bo"];
    CHECK(!shown_view_url(small, sizeof small, "http://e/v", 25, "", "shown=bot") && !small[0], "too small: empty, false");
}

typedef struct {
    int64_t at;
    const char *page;
    int face;
} sim_ev_t;

typedef struct {
    int64_t at;
    char q[SHOWN_QUERY_MAX];
} sim_poll_t;

#define SIM_FRAME_MS 16
#define SIM_WAIT_MS 25000

static int sim(const sim_ev_t *ev, int n_ev, int64_t end, sim_poll_t *polls, int max)
{
    shown_t s = SHOWN_INIT;
    const char *page = ev[0].page;
    int face = ev[0].face, next = 1, n = 0;
    shown_note(&s, page, face, 0);
    int64_t poll_end = -1, idle_at = -1;
    for (int64_t t = 0; t <= end; t++) {
        bool wake = false;
        while (next < n_ev && ev[next].at == t) {
            page = ev[next].page;
            face = ev[next].face;
            next++;
        }
        if (t % SIM_FRAME_MS == 0) wake = shown_note(&s, page, face, t);
        bool start = poll_end < 0 || t >= poll_end;
        if (!start && (wake || t >= idle_at)) {
            int slice = 1000;
            start = shown_idle(&s, t, &slice);
            idle_at = t + slice;
        }
        if (!start) continue;
        if (n < max) {
            polls[n].at = t;
            CHECK(shown_query(&s, polls[n].q, sizeof polls[n].q), "query fits");
        }
        n++;
        poll_end = t + SIM_WAIT_MS;
        idle_at = t + 1000;
    }
    return n;
}

static void test_sim(void)
{
    sim_poll_t p[16];
    const sim_ev_t still[] = {{0, SHOWN_WEATHER, WXF_TEMP}};
    int n = sim(still, 1, 20000, p, 16);
    CHECK(n == 1 && !strcmp(p[0].q, "shown=weather:temp"), "nothing changes: one poll in 20 s (%d)", n);

    const sim_ev_t turns[] = {{0, SHOWN_WEATHER, WXF_TEMP},
                              {5000, SHOWN_WEATHER, WXF_RAIN},
                              {5100, SHOWN_WEATHER, WXF_CAST},
                              {5200, SHOWN_WEATHER, WXF_WIND}};
    n = sim(turns, 4, 20000, p, 16);
    CHECK(n == 2, "three quick turns: one more poll (%d)", n);
    if (n == 2) {
        CHECK(!strcmp(p[1].q, "shown=weather:wind"), "the settled face: %s", p[1].q);
        CHECK(p[1].at >= 5200 + SHOWN_DEBOUNCE_MS && p[1].at <= 5000 + 500 + SIM_FRAME_MS, "within ~0.5 s of the first turn: %lld",
              (long long)p[1].at);
    }

    const sim_ev_t slow[] = {{0, "bot", -1}, {3000, SHOWN_WEATHER, -1}, {3016, SHOWN_WEATHER, WXF_RAIN}, {8000, SHOWN_WEATHER, WXF_SUN},
                             {12000, "pomodoro", -1}};
    n = sim(slow, 5, 20000, p, 16);
    CHECK(n == 4, "one poll per settled change (%d)", n);
    if (n == 4) {
        CHECK(!strcmp(p[1].q, "shown=weather:rain") && !strcmp(p[2].q, "shown=weather:sun") && !strcmp(p[3].q, "shown=pomodoro"),
              "values: %s %s %s", p[1].q, p[2].q, p[3].q);
        CHECK(p[2].at - 8000 <= SHOWN_DEBOUNCE_MS + SIM_FRAME_MS && p[3].at - 12000 <= SHOWN_DEBOUNCE_MS + SIM_FRAME_MS,
              "each right after its debounce: %lld %lld", (long long)p[2].at, (long long)p[3].at);
    }

    const sim_ev_t back[] = {{0, SHOWN_WEATHER, WXF_TEMP}, {4000, SHOWN_WEATHER, WXF_RAIN}, {4100, SHOWN_WEATHER, WXF_TEMP}};
    n = sim(back, 3, 20000, p, 16);
    CHECK(n == 1, "turned away and back inside the debounce: no poll (%d)", n);
}

int main(void)
{
    test_query();
    test_debounce();
    test_idle();
    test_url();
    test_sim();
    if (failures) {
        printf("%d shown check(s) failed\n", failures);
        return 1;
    }
    printf("shown: all checks passed\n");
    return 0;
}
