#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "usage.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define T0 1782000000LL

static usage_acct_t acct(const char *id, int tool, int p5, int p7)
{
    usage_acct_t a = {.tool = tool, .five = {.have = true, .pct = p5, .starts_at = T0, .resets_at = T0 + USAGE_FIVE_H_S},
                      .week = {.have = true, .pct = p7, .resets_at = T0 + 3 * 86400 + 9 * 3600}};
    snprintf(a.id, sizeof a.id, "%s", id);
    snprintf(a.name, sizeof a.name, "%s", id);
    return a;
}

static usage_block_t three(void)
{
    usage_block_t b = {.n = 3};
    b.a[0] = acct("a", USAGE_TOOL_CLAUDE, 34, 22);
    b.a[1] = acct("b", USAGE_TOOL_CLAUDE, 93, 71);
    b.a[2] = acct("c", USAGE_TOOL_CODEX, 18, 9);
    return b;
}

static usage_act_t in(usage_nav_t *nav, const usage_block_t *b, usage_in_t i, double t) { return usage_input(nav, b, &i, t); }

static void test_open_on_highest(void)
{
    usage_block_t b = three();
    usage_nav_t nav = {0};
    CHECK(in(&nav, &b, (usage_in_t){.pushes = 1}, 1) == USAGE_ACT_NONE && !nav.open, "a push stays the bot's");
    CHECK(in(&nav, &b, (usage_in_t){.detents = 2, .hswipes = 1, .longs = 1, .wakes = 1}, 1) == USAGE_ACT_NONE && !nav.open,
          "turns, swipes, long pushes and wakes stay the bot's");
    CHECK(in(&nav, &b, (usage_in_t){.taps = 1}, 2) == USAGE_ACT_OPEN && nav.open && nav.idx == 1 && strcmp(nav.id, "b") == 0,
          "a tap opens the highest 5-hour %%: %d", nav.idx);
    usage_nav_close(&nav);
    b.a[2].five.pct = 93;
    in(&nav, &b, (usage_in_t){.taps = 1}, 2);
    CHECK(nav.idx == 1, "a tie opens the first in Ember's order: %d", nav.idx);
    usage_nav_close(&nav);
    b.a[1].five.have = false;
    in(&nav, &b, (usage_in_t){.taps = 1}, 2);
    CHECK(nav.idx == 2, "an account without a 5-hour window ranks last: %d", nav.idx);
    usage_block_t none = {0};
    usage_nav_t n2 = {0};
    CHECK(in(&n2, &none, (usage_in_t){.taps = 1}, 2) == USAGE_ACT_NONE && !n2.open, "no accounts: the tap is the bot's");
}

static void test_step_and_wrap(void)
{
    usage_block_t b = three();
    usage_nav_t nav = {0};
    in(&nav, &b, (usage_in_t){.taps = 1}, 0);
    CHECK(in(&nav, &b, (usage_in_t){.hswipes = 1}, 1) == USAGE_ACT_STEP && nav.idx == 2, "swipe left: next");
    CHECK(in(&nav, &b, (usage_in_t){.hswipes = 1}, 2) == USAGE_ACT_STEP && nav.idx == 0, "next wraps to the first");
    CHECK(in(&nav, &b, (usage_in_t){.hswipes = -1}, 3) == USAGE_ACT_STEP && nav.idx == 2, "swipe right wraps back");
    CHECK(in(&nav, &b, (usage_in_t){.detents = 1}, 4) == USAGE_ACT_STEP && nav.idx == 0 && strcmp(nav.id, "a") == 0,
          "a turn steps like a swipe");
    in(&nav, &b, (usage_in_t){.detents = -4}, 5);
    CHECK(nav.idx == 2, "several detents back wrap: %d", nav.idx);
    in(&nav, &b, (usage_in_t){.detents = 7}, 6);
    CHECK(nav.idx == 0, "many forward: %d", nav.idx);
    CHECK(in(&nav, &b, (usage_in_t){.detents = 1, .hswipes = -1}, 7) == USAGE_ACT_NONE && nav.idx == 0, "opposite steps cancel");
    usage_block_t one = {.n = 1};
    one.a[0] = acct("solo", USAGE_TOOL_CLAUDE, 55, 30);
    usage_nav_t n1 = {0};
    in(&n1, &one, (usage_in_t){.taps = 1}, 0);
    in(&n1, &one, (usage_in_t){.hswipes = 3}, 1);
    CHECK(n1.open && n1.idx == 0, "one account: steps stay on it");
}

static void test_close_and_idle(void)
{
    usage_block_t b = three();
    const usage_in_t closers[] = {{.taps = 1}, {.pushes = 1}, {.longs = 1}};
    for (int i = 0; i < 3; i++) {
        usage_nav_t nav = {0};
        in(&nav, &b, (usage_in_t){.taps = 1}, 0);
        CHECK(in(&nav, &b, closers[i], 1) == USAGE_ACT_CLOSE && !nav.open && !nav.id[0], "closer %d goes back to the bot", i);
    }
    usage_nav_t nav = {0};
    in(&nav, &b, (usage_in_t){.taps = 1}, 100);
    CHECK(!usage_nav_tick(&nav, 119.9) && nav.open, "open under 20 s");
    CHECK(usage_nav_tick(&nav, 120.0) && !nav.open, "back to the bot after 20 s untouched");
    in(&nav, &b, (usage_in_t){.taps = 1}, 200);
    in(&nav, &b, (usage_in_t){.detents = 1}, 215);
    CHECK(!usage_nav_tick(&nav, 230) && nav.open, "a turn restarts the 20 s");
    in(&nav, &b, (usage_in_t){.wakes = 1}, 230);
    CHECK(!usage_nav_tick(&nav, 249) && usage_nav_tick(&nav, 250.5), "a touch restarts it too");
    CHECK(!usage_nav_tick(&nav, 999), "closed: nothing to do");
}

static void test_sync(void)
{
    usage_block_t b = three();
    usage_nav_t nav = {0};
    in(&nav, &b, (usage_in_t){.taps = 1}, 0);
    usage_block_t moved = {.n = 3};
    moved.a[0] = b.a[2];
    moved.a[1] = b.a[0];
    moved.a[2] = b.a[1];
    CHECK(!usage_nav_sync(&nav, &moved) && nav.idx == 2 && strcmp(nav.id, "b") == 0, "reordered: the same account: %d", nav.idx);
    usage_block_t gone = {.n = 2};
    gone.a[0] = b.a[0];
    gone.a[1] = b.a[2];
    CHECK(!usage_nav_sync(&nav, &gone) && nav.idx == 1 && strcmp(nav.id, "c") == 0, "hidden: the slot, clamped: %s", nav.id);
    usage_block_t none = {0};
    CHECK(usage_nav_sync(&nav, &none) && !nav.open, "no accounts closes");
}

static void test_hint(void)
{
    usage_block_t b = three();
    CHECK(usage_hint(&b, 0), "93%% is past 90: hint on");
    b.a[1].five.pct = 89;
    CHECK(!usage_hint(&b, 0), "89%%: off");
    b.a[1].five.pct = 90;
    CHECK(usage_hint(&b, 0), "exactly 90: on");
    b.a[1].stale = true;
    CHECK(!usage_hint(&b, 0), "a stale account never lights it");
    b.a[0].five.pct = 100;
    CHECK(usage_hint(&b, 0), "another fresh one at 100 does");
    b.a[0].five.have = false;
    CHECK(!usage_hint(&b, 0), "no 5-hour window: off");
    usage_block_t none = {0};
    CHECK(!usage_hint(&none, 0), "no accounts: off");
    usage_block_t r = three();
    long long reset = r.a[1].five.resets_at;
    CHECK(usage_hint(&r, (double)reset - 1), "93%% a second before its reset: on");
    CHECK(!usage_hint(&r, (double)reset), "its 5h window reset: off before Ember says so");
    usage_acct_t st = usage_as_stale(&r.a[1], T0 + 600);
    CHECK(st.stale && st.updated_at == T0 + 600 && st.five.pct == 93, "offline: shown stale since the last view");
    r.a[1].stale = true;
    r.a[1].updated_at = T0 + 60;
    st = usage_as_stale(&r.a[1], T0 + 600);
    CHECK(st.updated_at == T0 + 60, "Ember's own updated_at wins");
}

static usage_face_t face_at(const usage_acct_t *a, long long now, bool off)
{
    usage_face_t f;
    usage_face(a, (double)now, off, 3600, &f);
    return f;
}

static void test_reset_and_forecast(void)
{
    usage_acct_t a = acct("a", USAGE_TOOL_CLAUDE, 34, 22);
    long long now = T0 + 2 * 3600 + 45 * 60;
    usage_face_t f = face_at(&a, now, true);
    CHECK(strcmp(f.reset, "resets in 2h 15m") == 0 && f.reset_rgb == USAGE_TEXT, "behind the pace: %s", f.reset);
    CHECK(f.five_rgb == USAGE_GREEN && f.value_rgb == USAGE_GREEN && strcmp(f.value, "34") == 0, "green value");
    CHECK(f.tick && fabsf(f.tick_frac - 0.55f) < 0.001f, "pace tick at 55%%: %f", f.tick_frac);
    a.five.pct = 78;
    now = T0 + 2 * 3600;
    f = face_at(&a, now, true);
    CHECK(strcmp(f.reset, "limit by 03:33 at this pace") == 0 && f.reset_rgb == USAGE_AMBER, "ahead: forecast %s", f.reset);
    f = face_at(&a, now, false);
    CHECK(strcmp(f.reset, "limit in 33 min at this pace") == 0, "no utc_offset: relative %s", f.reset);
    a.five.pct = 40;
    f = face_at(&a, now, true);
    CHECK(strcmp(f.reset, "resets in 3h 00m") == 0, "exactly on the pace: the reset comes first: %s", f.reset);
    a.five.pct = 10;
    f = face_at(&a, T0 + 10 * 60, true);
    CHECK(strcmp(f.reset, "resets in 4h 50m") == 0, "under 15 min in: no forecast yet: %s", f.reset);
    a.five.pct = 100;
    f = face_at(&a, now, true);
    CHECK(strcmp(f.reset, "limited \xE2\x80\xA2 back at 06:00") == 0 && f.reset_rgb == USAGE_RED && f.value_rgb == USAGE_RED,
          "at 100: limited %s", f.reset);
    f = face_at(&a, now, false);
    CHECK(strcmp(f.reset, "limited \xE2\x80\xA2 back in 3h 00m") == 0, "limited, relative: %s", f.reset);
    a.five.pct = 0;
    a.five.resets_at = a.five.starts_at = 0;
    f = face_at(&a, now, true);
    CHECK(strcmp(f.reset, "new 5h window") == 0 && !f.tick && f.five_frac == 0, "past its reset: %s", f.reset);
    usage_acct_t g = acct("g", USAGE_TOOL_CLAUDE, 70, 22);
    g.five.starts_at = 0;
    f = face_at(&g, T0 + 3600, true);
    CHECK(f.tick && fabsf(f.tick_frac - 0.2f) < 0.001f && f.five_rgb == USAGE_AMBER, "no starts_at: 5 h before the reset");
}

static void test_stale(void)
{
    usage_acct_t a = acct("a", USAGE_TOOL_CODEX, 18, 9);
    a.stale = true;
    a.updated_at = T0 + 3600;
    usage_face_t f = face_at(&a, T0 + 3600 + 12 * 60 + 30, true);
    CHECK(strcmp(f.reset, "no update for 12 min") == 0 && f.reset_rgb == USAGE_DIM, "stale line: %s", f.reset);
    CHECK(!f.tick && f.five_rgb == USAGE_GREY && f.value_rgb == USAGE_GREY && f.mark_rgb == USAGE_GREY, "grey, no tick");
    CHECK(f.week_rgb == 0x484848, "the inner ring grey at 0.8: %06x", (unsigned)f.week_rgb);
    f = face_at(&a, T0 + 3 * 3600 + 5 * 60 + 1, true);
    CHECK(strcmp(f.reset, "no update for 2h 05m") == 0, "hours: %s", f.reset);
    a.updated_at = 0;
    f = face_at(&a, T0 + 3600, true);
    CHECK(strcmp(f.reset, "no update") == 0, "no updated_at: %s", f.reset);
    a.stale = false;
    f = face_at(&a, T0 + 3600, true);
    CHECK(f.mark_rgb == 0xFFFFFF && f.tick, "fresh Codex: white mark");
}

static void test_week_line(void)
{
    usage_acct_t a = acct("a", USAGE_TOOL_CLAUDE, 41, 41);
    usage_face_t f = face_at(&a, T0 + 3600, true);
    CHECK(strcmp(f.week_line, "week 41% \xE2\x80\xA2 Wed 10:00") == 0, "week with a clock: %s", f.week_line);
    f = face_at(&a, T0 + 3600, false);
    CHECK(strcmp(f.week_line, "week 41% \xE2\x80\xA2 in 3d 8h") == 0, "week relative: %s", f.week_line);
    a.n_models = 2;
    a.models[0] = (usage_model_t){"Opus", 38};
    a.models[1] = (usage_model_t){"Sonnet", 12};
    f = face_at(&a, T0 + 3600, true);
    CHECK(strcmp(f.week_line, "Opus 38% \xE2\x80\xA2 Sonnet 12%") == 0, "models replace the week: %s", f.week_line);
    a.n_models = 1;
    f = face_at(&a, T0 + 3600, true);
    CHECK(strcmp(f.week_line, "Opus 38%") == 0, "one model: %s", f.week_line);
    a.n_models = 0;
    a.week.resets_at = 0;
    f = face_at(&a, T0 + 3600, true);
    CHECK(strcmp(f.week_line, "week 41%") == 0, "no reset time: %s", f.week_line);
    a.week.have = false;
    f = face_at(&a, T0 + 3600, true);
    CHECK(!f.week_line[0] && f.week_frac == 0, "no week: empty");
    CHECK(usage_scale_rgb(69) == USAGE_GREEN && usage_scale_rgb(70) == USAGE_AMBER && usage_scale_rgb(89) == USAGE_AMBER &&
              usage_scale_rgb(90) == USAGE_RED,
          "scale bands");
    char d[16];
    usage_dur(59, d, sizeof d);
    CHECK(strcmp(d, "<1 min") == 0, "%s", d);
    usage_dur(3599, d, sizeof d);
    CHECK(strcmp(d, "59 min") == 0, "%s", d);
    usage_dur(2 * 3600 + 2 * 60, d, sizeof d);
    CHECK(strcmp(d, "2h 02m") == 0, "%s", d);
    usage_dur(2 * 86400 + 4 * 3600 + 59, d, sizeof d);
    CHECK(strcmp(d, "2d 4h") == 0, "%s", d);
}

static void test_parse_edges(void)
{
    const char *j = "{\"accounts\":[{\"tool\":\"claude\",\"name\":\"no id\"},"
                    "{\"id\":\"x:1\",\"tool\":\"gemini\",\"name\":\"Caf\xC3\xA9 \xE2\x80\x94 work\","
                    "\"five_hour\":{\"pct\":140,\"resets_at\":1782049680,\"starts_at\":1782049680},"
                    "\"seven_day\":{\"pct\":\"9\"},\"models\":[{\"name\":\"Opus\"},{\"name\":\"Sonnet\",\"pct\":3},"
                    "{\"name\":\"Haiku\",\"pct\":1}],\"stale\":\"yes\",\"updated_at\":12.5},"
                    "{\"id\":\"2\"},{\"id\":\"3\"},{\"id\":\"4\"},{\"id\":\"5\"},{\"id\":\"6\"},{\"id\":\"7\"},{\"id\":\"8\"},"
                    "{\"id\":\"9\"}]}";
    cJSON *o = cJSON_Parse(j);
    usage_block_t b;
    CHECK(usage_parse(o, &b) && b.n == USAGE_MAX, "at most 8, the one without an id skipped: %d", b.n);
    const usage_acct_t *a = &b.a[0];
    CHECK(strcmp(a->id, "x:1") == 0 && a->tool == USAGE_TOOL_OTHER, "an unknown tool is neutral");
    CHECK(strcmp(a->name, "Cafe - work") == 0, "the name folded to ASCII: %s", a->name);
    CHECK(a->five.have && a->five.pct == 100 && a->five.starts_at == 0, "pct clamped, a start at the reset dropped");
    CHECK(!a->week.have, "a string pct is no window");
    CHECK(a->n_models == 2 && strcmp(a->models[0].name, "Sonnet") == 0 && strcmp(a->models[1].name, "Haiku") == 0,
          "a model without pct is skipped, at most 2");
    CHECK(!a->stale && a->updated_at == 0, "non-boolean stale and fractional updated_at ignored");
    CHECK(strcmp(b.a[7].id, "8") == 0 && !b.a[7].five.have, "the ninth is dropped");
    cJSON_Delete(o);
    o = cJSON_Parse("{\"accounts\":[{\"id\":\"claude:0123456789abcdef0123456789abcdef0A\",\"five_hour\":{\"pct\":91}},"
                    "{\"id\":\"claude:0123456789abcdef0123456789abcdef0B\",\"five_hour\":{\"pct\":10}},"
                    "{\"id\":\"claude:0123456789abcdef0123456789abcdef0\",\"five_hour\":{\"pct\":20},"
                    "\"models\":[{\"name\":\"\",\"pct\":3},{\"name\":\" \",\"pct\":4},{\"name\":\"Opus\",\"pct\":5}]}]}");
    CHECK(usage_parse(o, &b) && b.n == 1 && strlen(b.a[0].id) == USAGE_ID_MAX && b.a[0].five.pct == 20,
          "ids over 40 bytes sharing a prefix are dropped, not truncated into one: %d", b.n);
    CHECK(b.a[0].n_models == 1 && strcmp(b.a[0].models[0].name, "Opus") == 0, "empty model names skipped");
    cJSON_Delete(o);
    CHECK(!usage_parse(NULL, &b) && b.n == 0, "no block");
    o = cJSON_Parse("{\"accounts\":{}}");
    CHECK(!usage_parse(o, &b) && b.n == 0, "accounts not an array");
    cJSON_Delete(o);
}

#define W 472
#define H 466

static uint32_t px_rgb(const uint16_t *buf, int x, int y)
{
    uint16_t p = buf[y * W + x];
    return ((uint32_t)((p >> 11) << 3) << 16) | ((uint32_t)(((p >> 5) & 63) << 2) << 8) | (uint32_t)((p & 31) << 3);
}

static bool near_rgb(uint32_t a, uint32_t b, int tol)
{
    for (int s = 0; s <= 16; s += 8)
        if (abs((int)((a >> s) & 0xFF) - (int)((b >> s) & 0xFF)) > tol) return false;
    return true;
}

static int ring_x(float r, float deg) { return (int)floorf(USAGE_CX + r * sinf(deg * 3.14159265f / 180)); }
static int ring_y(float r, float deg) { return (int)floorf(USAGE_CY - r * cosf(deg * 3.14159265f / 180)); }

static int lit(const uint16_t *buf, float cx, float cy)
{
    int n = 0;
    for (int y = (int)cy - 3; y <= (int)cy + 3; y++)
        for (int x = (int)cx - 3; x <= (int)cx + 3; x++) n += buf[y * W + x] != 0;
    return n;
}

static void test_rings_and_dots(void)
{
    uint16_t *buf = malloc(W * H * 2);
    usage_acct_t a = acct("a", USAGE_TOOL_CLAUDE, 62, 41);
    usage_face_t f = face_at(&a, T0 + 3600, true);
    usage_rings_render(buf, W, H, &f, 1, 0);
    CHECK(near_rgb(px_rgb(buf, ring_x(USAGE_R5, 90), ring_y(USAGE_R5, 90)), USAGE_GREEN, 8), "5h arc at 3 o'clock");
    CHECK(near_rgb(px_rgb(buf, ring_x(USAGE_R5, 270), ring_y(USAGE_R5, 270)), USAGE_TRACK, 8), "5h track at 9 o'clock: %06x",
          (unsigned)px_rgb(buf, ring_x(USAGE_R5, 270), ring_y(USAGE_R5, 270)));
    CHECK(near_rgb(px_rgb(buf, ring_x(USAGE_R7, 90), ring_y(USAGE_R7, 90)), f.week_rgb, 8), "7d arc at 3 o'clock");
    CHECK(near_rgb(px_rgb(buf, ring_x(USAGE_R7, 200), ring_y(USAGE_R7, 200)), USAGE_TRACK2, 8), "7d track past 41%%");
    CHECK(buf[233 * W + 236] == 0 && buf[ring_y(210, 0) * W + ring_x(210, 0)] == 0, "black between and inside the rings");
    float dx, dy;
    usage_dot_xy(0, 1, &dx, &dy);
    CHECK(lit(buf, dx, dy) == 0, "one account: no dots");
    usage_rings_render(buf, W, H, &f, 3, 1);
    for (int k = 0; k < 3; k++) {
        usage_dot_xy(k, 3, &dx, &dy);
        uint32_t c = px_rgb(buf, (int)dx, (int)dy);
        CHECK(lit(buf, dx, dy) > 20 && near_rgb(c, k == 1 ? USAGE_DOT_ON : USAGE_DOT_OFF, 8), "dot %d of 3: %06x", k, (unsigned)c);
    }
    float x0, x2, y0, y1;
    usage_dot_xy(0, 3, &x0, &y0);
    usage_dot_xy(2, 3, &x2, &y1);
    usage_dot_xy(1, 3, &dx, &dy);
    CHECK(x0 < dx && dx < x2 && fabsf(dx - USAGE_CX) < 0.01f && fabsf(y0 - y1) < 0.01f && dy > y0, "left to right, centred, on an arc");
    usage_face_t full = f;
    full.five_frac = 1;
    usage_rings_render(buf, W, H, &full, 0, 0);
    CHECK(near_rgb(px_rgb(buf, ring_x(USAGE_R5, 270), ring_y(USAGE_R5, 270)), USAGE_GREEN, 8), "100%%: the whole ring");
    free(buf);
}

static void test_band_redraw(void)
{
    uint16_t *buf = malloc(W * H * 2);
    for (int i = 0; i < W * H; i++) buf[i] = 0x1234;
    usage_acct_t a = acct("a", USAGE_TOOL_CLAUDE, 62, 41);
    usage_face_t f = face_at(&a, T0 + 3600, true);
    usage_ring5_render(buf, W, H, &f);
    CHECK(buf[233 * W + 236] == 0x1234 && buf[ring_y(USAGE_R7, 90) * W + ring_x(USAGE_R7, 90)] == 0x1234,
          "the 5h ring leaves the centre and the 7d ring alone");
    CHECK(buf[ring_y(USAGE_R5, 90) * W + ring_x(USAGE_R5, 90)] != 0x1234 && buf[ring_y(USAGE_R5 + 7.5f, 45) * W + ring_x(USAGE_R5 + 7.5f, 45)] == 0,
          "and redraws its whole band, black at the edge");
    usage_ring7_render(buf, W, H, &f);
    CHECK(near_rgb(px_rgb(buf, ring_x(USAGE_R7, 90), ring_y(USAGE_R7, 90)), f.week_rgb, 8) && buf[233 * W + 236] == 0x1234, "7d band only");
    memset(buf, 0, W * H * 2);
    usage_dots_render(buf, W, H, 3, 0);
    float x, y;
    usage_dot_xy(0, 3, &x, &y);
    CHECK(lit(buf, x, y) > 20, "three dots");
    usage_dots_render(buf, W, H, 1, 0);
    CHECK(lit(buf, x, y) == 0, "down to one account: the old dots are cleared");
    usage_rings_render(buf, W, H, &f, 1, 0);
    uint16_t *ref = malloc(W * H * 2);
    memcpy(ref, buf, W * H * 2);
    for (int i = 0; i < W * H; i++) buf[i] = 0x1234;
    memset(buf, 0, W * H * 2);
    usage_ring5_render(buf, W, H, &f);
    usage_ring7_render(buf, W, H, &f);
    CHECK(memcmp(ref, buf, W * H * 2) == 0, "the parts add up to the whole face");
    free(ref);
    free(buf);
}

static void test_arc_mask(void)
{
    int x, y, w, h;
    float cx = 236, cy = 233, r = 195.72f, hw = 4.5f;
    usage_arc_box(cx, cy, r, hw, USAGE_HINT_A0, USAGE_HINT_A1, &x, &y, &w, &h);
    CHECK(x % 2 == 0 && w % 2 == 0 && w > 0 && h > 0 && w * h < 16000, "an even box: %d,%d %dx%d", x, y, w, h);
    uint8_t *m = malloc((size_t)(w * h));
    usage_arc_mask(cx, cy, r, hw, USAGE_HINT_A0, USAGE_HINT_A1, x, y, w, h, m);
    int on = 0, edge = 0;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            on += m[j * w + i] == 255;
            if ((i == 0 || j == 0 || i == w - 1 || j == h - 1) && m[j * w + i]) edge++;
        }
    float len = r * (USAGE_HINT_A1 - USAGE_HINT_A0) * 3.14159265f / 180;
    CHECK(on > (int)(len * 7) && on < (int)((len + 10) * 10), "about 40 deg x 9 px lit: %d", on);
    CHECK(edge == 0, "the box holds the whole arc and its caps");
    int mx = ring_x(r, 320) - x, my = ring_y(r, 320) - y;
    CHECK(m[my * w + mx] == 255, "solid at the middle, 320 deg");
    CHECK(usage_arc_cover(r * sinf(350 * 3.14159265f / 180), -r * cosf(350 * 3.14159265f / 180), r, hw, 300, 340) == 0,
          "nothing at 350 deg");
    CHECK(usage_arc_cover(0, -r, r, hw, 0, 0) == 0, "an empty arc covers nothing");
    CHECK(usage_arc_cover(-3, -r, r, hw, 0, 90) > 0.5f, "a round cap reaches back past 12 o'clock");
    free(m);
}

int main(void)
{
    test_open_on_highest();
    test_step_and_wrap();
    test_close_and_idle();
    test_sync();
    test_hint();
    test_reset_and_forecast();
    test_stale();
    test_week_line();
    test_parse_edges();
    test_rings_and_dots();
    test_band_redraw();
    test_arc_mask();
    if (failures) {
        printf("usage: %d failure(s)\n", failures);
        return 1;
    }
    printf("usage: all tests passed\n");
    return 0;
}
