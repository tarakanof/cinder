#include <stdio.h>
#include <string.h>

#include "bot_behavior.h"
#include "ember_legacy.h"
#include "pomo_legacy.h"
#include "press_route.h"
#include "wx_legacy.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_pomo(void)
{
    pomo_state_t s;
    CHECK(pomo_legacy_parse("{\"phase\":\"focus\",\"running\":true,\"paused\":false,\"remaining_sec\":1499,"
                            "\"planned_sec\":1500,\"round\":2}", &s), "full");
    CHECK(s.phase == POMO_PHASE_FOCUS && s.running && !s.paused && s.remaining_sec == 1499 && s.planned_sec == 1500 &&
              s.round == 2, "full fields");
    CHECK(pomo_legacy_parse("{\"phase\":\"short_break\",\"paused\":true}", &s), "sparse");
    CHECK(s.phase == POMO_PHASE_SHORT_BREAK && !s.running && s.paused && s.remaining_sec == 0 && s.planned_sec == 0 &&
              s.round == 0, "sparse defaults");
    CHECK(pomo_legacy_parse("{\"phase\":\"warmup\",\"running\":1,\"remaining_sec\":\"9\"}", &s), "odd types");
    CHECK(s.phase == POMO_PHASE_UNKNOWN && !s.running && s.remaining_sec == 0, "odd types ignored");
    CHECK(pomo_legacy_parse("{\"phase\":\"\"}", &s) && s.phase == POMO_PHASE_IDLE, "empty phase idle");

    pomo_state_t keep = {.phase = POMO_PHASE_LONG_BREAK, .round = 7};
    s = keep;
    CHECK(!pomo_legacy_parse("{\"running\":true}", &s), "no phase");
    CHECK(!pomo_legacy_parse("{\"phase\":3}", &s), "phase not string");
    CHECK(!pomo_legacy_parse("{\"phase\":\"focus\"", &s), "truncated");
    CHECK(!pomo_legacy_parse("", &s), "empty");
    CHECK(s.phase == POMO_PHASE_LONG_BREAK && s.round == 7, "failure leaves out untouched");
}

static void drop(const char *in, const char *want)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", in);
    wx_legacy_drop_hourly(buf);
    CHECK(strcmp(buf, want) == 0, "drop_hourly(%s) = %s, want %s", in, buf, want);
}

static void test_drop_hourly(void)
{
    drop("{\"hourly\":[{\"t\":1},{\"t\":2}],\"a\":1}", "{\"hourly\":[],\"a\":1}");
    drop("{\"a\":{\"hourly\":[1,2]},\"b\":{\"hourly\":[3]}}", "{\"a\":{\"hourly\":[]},\"b\":{\"hourly\":[]}}");
    drop("{\"hourly\":[],\"a\":1}", "{\"hourly\":[],\"a\":1}");
    drop("{\"a\":[1,2],\"b\":2}", "{\"a\":[1,2],\"b\":2}");
    drop("", "");
    drop("{\"hourly\":[{\"p\":[1,[2,3]]},{\"q\":[]}],\"current\":{\"t\":[4]}}", "{\"hourly\":[],\"current\":{\"t\":[4]}}");
    drop("{\"hourly\":[{\"c\":\"a]b[c\"}],\"x\":\"]\"}", "{\"hourly\":[],\"x\":\"]\"}");
    drop("{\"hourly\":[{\"c\":\"q\\\"]\\\\\"}],\"x\":1}", "{\"hourly\":[],\"x\":1}");
    drop("{\"s\":\"\\\"hourly\\\":[1]\",\"hourly\":[2]}", "{\"s\":\"\\\"hourly\\\":[1]\",\"hourly\":[]}");
    drop("{\"s\":\"x\\\\\",\"hourly\":[2]}", "{\"s\":\"x\\\\\",\"hourly\":[]}");
    drop("{\"hourly\":[{\"p\":[1]}", "{\"hourly\":[{\"p\":[1]}");
    drop("{\"hourly\":[\"a]", "{\"hourly\":[\"a]");
    drop("{\"hourly\":[\"a\\", "{\"hourly\":[\"a\\");
    drop("{\"hourly\": [1]}", "{\"hourly\": [1]}");

    char body[] = "{\"enabled\":true,\"hourly\":[{\"temp_c\":[1,2]},{\"c\":\"]\"}],"
                  "\"current\":{\"condition\":\"rain\",\"temp_c\":4.5}}";
    wx_legacy_drop_hourly(body);
    wx_obs_t o;
    CHECK(wx_legacy_parse(body, &o) && o.valid && o.has_temp && o.temp_c == 4.5f && strcmp(o.condition, "rain") == 0,
          "nested hourly still parses: %s", body);
}

static void test_weather(void)
{
    wx_obs_t o;
    CHECK(wx_legacy_parse("{\"enabled\":true,\"provider\":\"met_no\",\"generated_at\":\"2026-10-08T14:05:00+02:00\","
                          "\"sun\":{\"sunrise\":\"2026-10-08T07:31:00+02:00\",\"sunset\":\"2026-10-08T18:47:00+02:00\"},"
                          "\"current\":{\"stale\":true,\"severe\":true,\"condition\":\"rain\","
                          "\"condition_code\":\"heavyrain\",\"temp_c\":-3.25},\"hourly\":[]}",
                          &o),
          "full");
    CHECK(o.enabled && strcmp(o.provider, "met_no") == 0 && o.now_min == 14 * 60 + 5, "top level");
    CHECK(o.rise_min == 7 * 60 + 31 && o.set_min == 18 * 60 + 47, "sun");
    CHECK(o.valid && o.stale && o.severe && strcmp(o.condition, "rain") == 0 && strcmp(o.code, "heavyrain") == 0,
          "current");
    CHECK(o.has_temp && o.temp_c == -3.25f, "temp");
    CHECK(!o.has_night && o.age_s == 0, "untouched fields zero");

    memset(&o, 0x5a, sizeof o);
    CHECK(wx_legacy_parse("{}", &o), "empty object parses");
    CHECK(!o.valid && !o.enabled && !o.has_temp && o.provider[0] == 0 && o.now_min == -1 && o.rise_min == -1 &&
              o.set_min == -1 && o.condition[0] == 0 && o.code[0] == 0,
          "empty object defaults");

    CHECK(wx_legacy_parse("{\"enabled\":1,\"provider\":7,\"generated_at\":\"bad\",\"sun\":[1],"
                          "\"current\":{\"temp_c\":\"4\",\"condition\":null}}",
                          &o),
          "odd types");
    CHECK(!o.enabled && o.provider[0] == 0 && o.now_min == -1 && o.rise_min == -1 && o.valid && !o.has_temp &&
              o.condition[0] == 0,
          "odd types ignored");

    CHECK(wx_legacy_parse("{\"provider\":\"a_very_long_provider_name\",\"current\":{\"condition_code\":"
                          "\"0123456789012345678901234567890123456789XYZ\"}}",
                          &o),
          "long strings");
    CHECK(strlen(o.provider) == sizeof o.provider - 1 && strncmp(o.provider, "a_very_long_pro", 15) == 0,
          "provider truncated: %s", o.provider);
    CHECK(strlen(o.code) == sizeof o.code - 1, "code truncated");

    CHECK(wx_legacy_parse("{\"current\":5}", &o) && !o.valid, "current not object");
    CHECK(!wx_legacy_parse("{\"current\":", &o), "truncated");
    CHECK(!wx_legacy_parse("", &o), "empty");
}

static void test_ember(void)
{
    ember_host_session_t sess[4];
    ember_host_info_t h = {.text = "keep", .color = 5};

    CHECK(ember_legacy_parse("{\"render\":{\"waiting\":1,\"errors\":1,\"running\":1,\"done\":1,\"source\":\"mac\"}}", sess,
                             4, &h) == BOT_WAITING,
          "waiting wins");
    CHECK(strcmp(h.text, "MAC") == 0 && h.color == -1 && h.tool == 0, "render source label: %s", h.text);
    CHECK(ember_legacy_parse("{\"render\":{\"errors\":2,\"running\":1}}", sess, 4, &h) == BOT_ERROR, "error");
    CHECK(ember_legacy_parse("{\"render\":{\"running\":1,\"done\":3}}", sess, 4, &h) == BOT_WORKING, "running");
    CHECK(ember_legacy_parse("{\"render\":{\"done\":1}}", sess, 4, &h) == BOT_DONE, "done");
    CHECK(ember_legacy_parse("{\"render\":{\"waiting\":\"1\"}}", sess, 4, &h) == BOT_IDLE, "string count ignored");
    CHECK(h.text[0] == 0, "no sessions, no label");

    const char *two = "{\"render\":{\"running\":2},\"sessions\":["
                      "{\"source\":\"alpha\",\"state\":\"running\",\"updated_at\":\"2026-10-03T01:31:24Z\"},"
                      "{\"source\":\"alpha\",\"state\":\"running\",\"updated_at\":\"2026-10-03T01:31:30Z\"}]}";
    CHECK(ember_legacy_parse(two, sess, 4, &h) == BOT_WORKING && strcmp(h.text, "ALPHA") == 0, "one host: %s", h.text);

    const char *mixed = "{\"render\":{\"running\":2},\"sessions\":["
                        "{\"source\":\"alpha\",\"state\":\"running\",\"updated_at\":\"2026-10-03T01:31:24Z\"},"
                        "{\"source\":\"beta\",\"state\":\"running\",\"updated_at\":\"2026-10-03T01:31:30Z\"}]}";
    CHECK(ember_legacy_parse(mixed, sess, 4, &h) == BOT_WORKING && h.text[0] == 0, "two hosts tie: %s", h.text);
    CHECK(ember_legacy_parse(mixed, sess, 1, &h) == BOT_WORKING && strcmp(h.text, "ALPHA") == 0,
          "max caps sessions: %s", h.text);

    const char *src_wins = "{\"render\":{\"running\":1,\"source\":\"\"},\"sessions\":["
                           "{\"source\":\"alpha\",\"state\":\"running\",\"updated_at\":\"2026-10-03T01:31:24Z\"}]}";
    CHECK(ember_legacy_parse(src_wins, sess, 4, &h) == BOT_WORKING && h.text[0] == 0, "render source beats sessions");

    snprintf(h.text, sizeof h.text, "keep");
    h.color = 5;
    CHECK(ember_legacy_parse("{\"sessions\":[]}", sess, 4, &h) == -1, "no render");
    CHECK(ember_legacy_parse("{\"render\":[]}", sess, 4, &h) == -1, "render not object");
    CHECK(ember_legacy_parse("{\"render\":", sess, 4, &h) == -1, "truncated");
    CHECK(strcmp(h.text, "keep") == 0 && h.color == 5, "failure leaves host untouched");
}

static void test_press_route(void)
{
    CHECK(pr_turn(true, false) == PR_TURN_DROP && pr_turn(true, true) == PR_TURN_DROP, "reset gesture eats the turn");
    CHECK(pr_turn(false, true) == PR_TURN_PAGE, "held turn pages");
    CHECK(pr_turn(false, false) == PR_TURN_DETENT, "free turn is a detent");

    CHECK(pr_release(false, false, false) == PR_PRESS_PUSH, "short press");
    CHECK(pr_release(false, false, true) == PR_PRESS_LONG, "long press");
    CHECK(pr_release(false, true, false) == PR_PRESS_NONE, "page turn spends the press");
    CHECK(pr_release(false, true, true) == PR_PRESS_NONE, "hold then turn is a page change, not a long push");
    CHECK(pr_release(true, false, false) == PR_PRESS_NONE && pr_release(true, false, true) == PR_PRESS_NONE,
          "reset gesture press");
}

int main(void)
{
    test_pomo();
    test_drop_hourly();
    test_weather();
    test_ember();
    test_press_route();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("legacy: all tests passed\n");
    return 0;
}
