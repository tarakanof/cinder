#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "device_api.h"
#include "knob_settings.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SERVER_DEFAULTS \
    "{\"brightness\":{\"follow_ember\":true,\"level\":153,\"floor\":10,\"startup\":153},\"pages\":[{\"id\":\"bot\",\"on\":true},{\"id\":\"pomodoro\",\"on\":true},{\"id\":\"weather\",\"on\":true}],\"home\":\"bot\",\"poll_ms\":2000,\"bot\":{\"sleepy_after_s\":300,\"demo_hold_s\":20,\"source_label\":true,\"working_ring\":true},\"display\":{\"fast_link\":true}}"

static const char *const KNOWN[] = {"bot", "pomodoro", "weather"};

static void test_settings_defaults(void)
{
    knob_settings_t d, p;
    knob_settings_defaults(&d);
    CHECK(knob_settings_parse(SERVER_DEFAULTS, &p), "server defaults parse");
    CHECK(memcmp(&d, &p, sizeof d) == 0, "server defaults == firmware defaults");
    CHECK(!knob_settings_parse("[]", &p) && memcmp(&d, &p, sizeof d) == 0, "array -> defaults");
    CHECK(!knob_settings_parse("{", &p), "bad json");
    CHECK(!knob_settings_parse(NULL, &p), "null");
    CHECK(knob_settings_parse("{}", &p) && memcmp(&d, &p, sizeof d) == 0, "empty object -> defaults");
}

static void test_settings_values(void)
{
    knob_settings_t k;
    knob_settings_parse("{\"brightness\":{\"follow_ember\":false,\"level\":60,\"floor\":20,\"startup\":40},"
                        "\"pages\":[{\"id\":\"weather\",\"on\":true},{\"id\":\"bot\",\"on\":false},{\"id\":\"pomodoro\",\"on\":true}],"
                        "\"home\":\"pomodoro\",\"poll_ms\":5000,\"bot\":{\"sleepy_after_s\":0,\"demo_hold_s\":5}}",
                        &k);
    CHECK(!k.follow_ember && k.level == 60 && k.floor == 20 && k.startup == 40, "brightness");
    CHECK(k.n_pages == 3 && strcmp(k.pages[0].id, "weather") == 0 && !k.pages[1].on, "pages");
    CHECK(strcmp(k.home, "pomodoro") == 0 && k.poll_ms == 5000, "home, poll");
    CHECK(k.sleepy_after_s == 0 && k.demo_hold_s == 5, "bot");
    CHECK(k.source_label && k.working_ring, "bot flags absent = on");
    CHECK(k.fast_link, "display.fast_link absent = on");
    CHECK(k.swipe_pages, "swipe_pages absent = on");
    int order[8], home;
    int n = knob_settings_page_order(&k, KNOWN, 3, order, &home);
    CHECK(n == 2 && order[0] == 2 && order[1] == 1 && home == 1, "order weather, pomodoro; home pomodoro: n %d", n);

    knob_settings_parse("{\"brightness\":{\"level\":999,\"floor\":0,\"startup\":-5},\"poll_ms\":50,"
                        "\"bot\":{\"sleepy_after_s\":1e9,\"demo_hold_s\":0}}",
                        &k);
    CHECK(k.level == 255 && k.floor == 1 && k.startup == 1, "brightness clamps: %d %d %d", k.level, k.floor, k.startup);
    CHECK(k.poll_ms == 1000 && k.sleepy_after_s == 86400 && k.demo_hold_s == 1, "number clamps");
    knob_settings_parse("{\"brightness\":{\"level\":30,\"floor\":80,\"startup\":5}}", &k);
    CHECK(k.floor == 30 && k.startup == 30, "floor <= level, startup >= floor");
    knob_settings_parse("{\"brightness\":{\"level\":0}}", &k);
    CHECK(k.level == 1 && k.floor == 1, "level 0 -> 1");
    knob_settings_parse("{\"brightness\":{\"follow_ember\":1,\"level\":\"x\"},\"poll_ms\":\"fast\"}", &k);
    CHECK(k.follow_ember && k.level == 153 && k.poll_ms == 2000, "wrong types ignored");
    knob_settings_parse("{\"bot\":{\"source_label\":false,\"working_ring\":false}}", &k);
    CHECK(!k.source_label && !k.working_ring, "bot flags off");
    knob_settings_parse("{\"display\":{\"fast_link\":false}}", &k);
    CHECK(!k.fast_link, "fast link off");
    knob_settings_parse("{\"swipe_pages\":false}", &k);
    CHECK(!k.swipe_pages, "swipe off");
    knob_settings_parse("{\"swipe_pages\":\"no\"}", &k);
    CHECK(k.swipe_pages, "swipe_pages: non-boolean keeps the default");
    knob_settings_parse("{\"bot\":{\"source_label\":0,\"working_ring\":null}}", &k);
    CHECK(k.source_label && k.working_ring, "bot flags: non-booleans keep the default");
}

static void test_pages(void)
{
    knob_settings_t k;
    int order[8], home, n;
    knob_settings_parse("{\"pages\":[{\"id\":\"media\",\"on\":true},{\"id\":\"bot\",\"on\":true},{\"id\":\"bot\",\"on\":false},"
                        "{\"id\":\"Bad\",\"on\":true},{\"id\":\"weather\"}],\"home\":\"media\"}",
                        &k);
    CHECK(k.n_pages == 3 && strcmp(k.pages[0].id, "media") == 0 && k.pages[1].on && !k.pages[2].on, "kept: %d", k.n_pages);
    n = knob_settings_page_order(&k, KNOWN, 3, order, &home);
    CHECK(n == 1 && order[0] == 0 && home == 0, "only bot shown; unknown home -> first");
    knob_settings_parse("{\"pages\":[{\"id\":\"bot\",\"on\":false}],\"home\":\"weather\"}", &k);
    n = knob_settings_page_order(&k, KNOWN, 3, order, &home);
    CHECK(n == 3 && order[0] == 0 && order[2] == 2 && home == 2, "none on -> all, home weather");
    knob_settings_parse("{\"pages\":[]}", &k);
    CHECK(k.n_pages == 3, "empty pages -> defaults");
    knob_settings_parse("{\"pages\":{}}", &k);
    CHECK(k.n_pages == 3, "object pages -> defaults");
    char json[1024] = "{\"pages\":[";
    for (int i = 0; i < 12; i++) {
        char item[40];
        snprintf(item, sizeof item, "%s{\"id\":\"p%d\",\"on\":true}", i ? "," : "", i);
        strcat(json, item);
    }
    strcat(json, "]}");
    knob_settings_parse(json, &k);
    CHECK(k.n_pages == KS_MAX_PAGES, "max pages");
}

static void test_checkin_body(void)
{
    char out[384];
    dev_checkin_t c = {.fw = "0.6.0", .ip = "192.168.0.39", .rssi = -58, .heap_internal_free = 47104,
                       .heap_internal_largest = 31744, .uptime_s = 812, .config_version = 6};
    size_t n = dev_checkin_body(&c, NULL, out, sizeof out);
    const char *want = "{\"config_version\":6,\"fw\":\"0.6.0\",\"heap_internal_free\":47104,\"heap_internal_largest\":31744,"
                       "\"ip\":\"192.168.0.39\",\"rssi\":-58,\"uptime_s\":812}";
    CHECK(n == strlen(want) && strcmp(out, want) == 0, "body: %s", out);
    c.ip = "";
    dev_checkin_body(&c, NULL, out, sizeof out);
    CHECK(!strstr(out, "\"ip\""), "no ip");
    c.fw = "evil\"";
    dev_checkin_body(&c, NULL, out, sizeof out);
    CHECK(strstr(out, "\"fw\":\"\""), "unsafe fw dropped");
    CHECK(dev_checkin_body(&c, NULL, out, 20) == 0, "small buffer");
    dev_checkin_t l = {.fw = "0.9.2", .ip = "10.0.0.2", .rssi = -60, .uptime_s = 5, .config_version = 1, .link_mhz = 40,
                       .link_fallback = true};
    dev_checkin_body(&l, NULL, out, sizeof out);
    CHECK(strstr(out, "\"ip\":\"10.0.0.2\",\"link_fallback\":true,\"link_mhz\":40,\"rssi\":-60"), "link fields: %s", out);
    dev_wifi_t w = {.channel = 6, .has_bssid = true, .bssid = {0x02, 0x11, 0x22, 0xab, 0xcd, 0xef}, .disconnects = 3,
                    .last_reason = 203, .has_rssi_min = true, .rssi_min = -83};
    l.wifi = &w;
    dev_checkin_body(&l, NULL, out, sizeof out);
    CHECK(strstr(out, "\"uptime_s\":5,\"wifi\":{\"bssid\":\"02:11:22:ab:cd:ef\",\"channel\":6,\"disconnects\":3,"
                      "\"last_reason\":203,\"rssi_min\":-83}}"),
          "wifi: %s", out);
    dev_wifi_t w0 = {0};
    l.wifi = &w0;
    dev_checkin_body(&l, NULL, out, sizeof out);
    CHECK(strstr(out, "\"uptime_s\":5,\"wifi\":{\"disconnects\":0}}"), "wifi unknowns left out: %s", out);
    cJSON *j = cJSON_Parse(out);
    CHECK(j && cJSON_IsObject(cJSON_GetObjectItem(j, "wifi")), "valid JSON");
    cJSON_Delete(j);
}

static void test_checkin_answer(void)
{
    dev_checkin_result_t r;
    dev_checkin_parse("{\"config_version\":7}", &r);
    CHECK(r.ok && r.config_version == 7 && !r.config && !r.has_new_token, "current");
    dev_checkin_result_free(&r);

    dev_checkin_parse("{\"config_version\":8,\"config\":" SERVER_DEFAULTS ",\"new_token\":\"ekd_abcDEF-_123\"}", &r);
    CHECK(r.ok && r.config_version == 8 && r.config && r.has_new_token && strcmp(r.new_token, "ekd_abcDEF-_123") == 0,
          "stale + rotation");
    knob_settings_t a, b;
    knob_settings_parse(r.config, &a);
    knob_settings_parse(SERVER_DEFAULTS, &b);
    CHECK(memcmp(&a, &b, sizeof a) == 0, "config round-trips");
    dev_checkin_result_free(&r);
    CHECK(!r.config && r.new_token[0] == 0, "freed and wiped");

    dev_checkin_parse("{\"config_version\":-1,\"config\":{}}", &r);
    CHECK(!r.ok && !r.config, "bad version");
    dev_checkin_parse("{\"config_version\":1,\"new_token\":\"has space\"}", &r);
    CHECK(r.ok && !r.has_new_token, "bad token ignored");
    char longtok[160] = "{\"config_version\":1,\"new_token\":\"";
    for (int i = 0; i < 65; i++) strcat(longtok, "a");
    strcat(longtok, "\"}");
    dev_checkin_parse(longtok, &r);
    CHECK(!r.has_new_token, "token > 64 ignored");
    dev_checkin_parse("not json", &r);
    CHECK(!r.ok, "not json");
}

static void test_schedule(void)
{
    dev_sched_t s;
    dev_sched_init(&s, 1000);
    CHECK(dev_sched_due(&s, 1000), "first checkin at boot");
    dev_sched_done(&s, DEV_CHECKIN_OK, 1000);
    CHECK(!dev_sched_due(&s, 60999) && dev_sched_due(&s, 61000), "every 60 s");

    dev_sched_epoch(&s, "5", 2000);
    CHECK(!dev_sched_due(&s, 2000), "baseline epoch");
    dev_sched_epoch(&s, "5", 4000);
    CHECK(!dev_sched_due(&s, 4000), "same epoch");
    dev_sched_epoch(&s, "6", 2500);
    CHECK(!dev_sched_due(&s, 2999) && dev_sched_due(&s, 3000), "epoch change -> checkin after the min gap");
    dev_sched_done(&s, DEV_CHECKIN_OK, 3000);
    dev_sched_epoch(&s, "", 4000);
    CHECK(!dev_sched_due(&s, 4000) && strcmp(s.epoch, "6") == 0, "missing header ignored");

    int64_t t = 10000;
    int want[] = {60000, 120000, 240000, 300000, 300000};
    for (int i = 0; i < 5; i++) {
        dev_sched_done(&s, DEV_CHECKIN_FAILED, t);
        CHECK(s.next_ms - t == want[i], "backoff %d: %lld", i, (long long)(s.next_ms - t));
        t = s.next_ms;
    }
    dev_sched_done(&s, DEV_CHECKIN_OK, t);
    CHECK(s.next_ms - t == 60000 && s.backoff_ms == 0, "reset after success");
    dev_sched_done(&s, DEV_CHECKIN_UNAUTHORIZED, t);
    CHECK(s.next_ms - t == 60000, "unauthorized: keep trying every 60 s");
    dev_sched_now(&s, t + 5);
    CHECK(dev_sched_due(&s, t + 5), "now");
}

static void test_link(void)
{
    CHECK(dev_link_combine(DEV_LINK_OK, false, false, false) == DEV_LINK_OK, "legacy ok");
    CHECK(dev_link_combine(DEV_LINK_UNREACHABLE, false, false, false) == DEV_LINK_UNREACHABLE, "legacy unreachable");
    CHECK(dev_link_combine(DEV_LINK_OK, true, false, false) == DEV_LINK_CONNECTING, "no checkin yet");
    CHECK(dev_link_combine(DEV_LINK_OK, true, true, false) == DEV_LINK_OK, "checked in");
    CHECK(dev_link_combine(DEV_LINK_OK, true, false, true) == DEV_LINK_UNAUTHORIZED, "401");
    CHECK(dev_link_combine(DEV_LINK_CONNECTING, true, true, false) == DEV_LINK_CONNECTING, "Wi-Fi down");
    CHECK(dev_link_combine(DEV_LINK_UNREACHABLE, true, true, true) == DEV_LINK_UNREACHABLE, "server down wins");
    CHECK(dev_link_combine(DEV_LINK_OFF, true, true, false) == DEV_LINK_OFF, "off");
}

static void test_epoch_reset(void)
{
    dev_sched_t s;
    dev_sched_init(&s, 0);
    dev_sched_done(&s, DEV_CHECKIN_OK, 0);
    dev_sched_epoch(&s, "5", 1000);
    dev_sched_epoch_reset(&s);
    dev_sched_epoch(&s, "5/7", 3000);
    CHECK(s.next_ms == DEV_CHECKIN_PERIOD_MS, "new key shape after a reset: no checkin");
    dev_sched_epoch(&s, "6/7", 4000);
    CHECK(s.next_ms == 4000, "a real change still checks in");
}

static void test_diagnostics(void)
{
    knob_settings_t k;
    knob_settings_parse("{}", &k);
    CHECK(k.diagnostics == KS_DIAG_OFF, "missing = off");
    knob_settings_parse("{\"diagnostics\":\"basic\"}", &k);
    CHECK(k.diagnostics == KS_DIAG_BASIC, "basic");
    knob_settings_parse("{\"diagnostics\":\"full\"}", &k);
    CHECK(k.diagnostics == KS_DIAG_FULL, "full");
    knob_settings_parse("{\"diagnostics\":\"loud\"}", &k);
    CHECK(k.diagnostics == KS_DIAG_OFF, "unknown = off");

    dev_checkin_t c = {.fw = "0.6.0", .ip = "192.168.0.39", .rssi = -60, .heap_internal_free = 100,
                       .heap_internal_largest = 50, .uptime_s = 9, .config_version = 3};
    char out[768];
    dev_stats_t st = {.period_ms = 60000, .n_cpu = 2, .cpu_pct = {12.04f, 140}, .has_heap_min = true,
                      .heap_internal_min = 30000, .has_psram = true, .psram_free = 7000000, .psram_min = 6500000,
                      .psram_largest = 6000000, .has_temp = true, .temp_c = 41.54f, .reset_reason = "poweron"};
    size_t n = dev_checkin_body(&c, &st, out, sizeof out);
    const char *want = "{\"config_version\":3,\"fw\":\"0.6.0\",\"heap_internal_free\":100,\"heap_internal_largest\":50,"
                       "\"ip\":\"192.168.0.39\",\"rssi\":-60,\"stats\":{\"cpu_pct\":[12.0,100.0],\"heap_internal_min\":30000,"
                       "\"period_ms\":60000,\"psram_free\":7000000,\"psram_largest\":6000000,\"psram_min\":6500000,"
                       "\"reset_reason\":\"poweron\",\"temp_c\":41.5},\"uptime_s\":9}";
    CHECK(n == strlen(want) && strcmp(out, want) == 0, "basic stats body: %s", out);
    dev_stats_t f = {.period_ms = 0, .has_req = true, .req_ok = 28, .req_fail = 2, .req_ms_avg = 35.5f,
                     .req_ms_max = 120, .has_frames = true, .fps = 29.5f, .frame_ms_avg = 12.25f, .frame_ms_max = 40,
                     .has_temp = true, .temp_c = 200, .reset_reason = "Bad Reason"};
    dev_checkin_body(&c, &f, out, sizeof out);
    CHECK(strstr(out, "\"stats\":{\"fps\":29.5,\"frame_ms_avg\":12.25,\"frame_ms_max\":40,\"period_ms\":1,"
                      "\"req_fail\":2,\"req_ms_avg\":35.5,\"req_ms_max\":120,\"req_ok\":28}") != NULL,
          "full stats body: %s", out);
    dev_stats_t z = {.period_ms = 5000, .has_req = true};
    dev_checkin_body(&c, &z, out, sizeof out);
    CHECK(strstr(out, "\"stats\":{\"period_ms\":5000,\"req_fail\":0,\"req_ok\":0}") != NULL, "no requests: no avg/max: %s",
          out);
    CHECK(dev_checkin_body(&c, &st, out, 200) == 0, "stats that do not fit: 0");

    CHECK(dev_window_ok(60000000LL) && dev_window_ok(3600000000LL), "<= 1 h ok");
    CHECK(!dev_window_ok(3600000001LL) && !dev_window_ok(0) && !dev_window_ok(-5) && !dev_window_ok(5000000000LL),
          "over 1 h / empty / negative: not ok (64-bit, no wrap)");
    dev_stats_t g = st;
    g.no_window = true;
    g.has_req = g.has_frames = true;
    dev_checkin_body(&c, &g, out, sizeof out);
    CHECK(strstr(out, "\"stats\":{\"heap_internal_min\":30000,\"psram_free\":7000000,\"psram_largest\":6000000,"
                      "\"psram_min\":6500000,\"reset_reason\":\"poweron\",\"temp_c\":41.5}") != NULL,
          "gauges only: %s", out);

    CHECK(strcmp(dev_reset_reason_name(1), "poweron") == 0 && strcmp(dev_reset_reason_name(3), "sw") == 0 &&
              strcmp(dev_reset_reason_name(4), "panic") == 0 && strcmp(dev_reset_reason_name(6), "task_wdt") == 0 &&
              strcmp(dev_reset_reason_name(15), "cpu_lockup") == 0 && strcmp(dev_reset_reason_name(99), "unknown") == 0 &&
              strcmp(dev_reset_reason_name(-1), "unknown") == 0,
          "reset reasons");
    CHECK(dev_cpu_pct(750000, 1000000) == 25.0f && dev_cpu_pct(2000000, 1000000) == 0 && dev_cpu_pct(0, 1000000) == 100 &&
              dev_cpu_pct(5, 0) == 0,
          "cpu pct");

    CHECK(dev_live_deadline_ms(1000, 900, 50000) == 50000 + 100000, "deadline");
    CHECK(dev_live_deadline_ms(0, 900, 5) == 0 && dev_live_deadline_ms(900, 900, 5) == 0 &&
              dev_live_deadline_ms(1000, 0, 5) == 0,
          "absent / past / no server time");
    CHECK(dev_live_deadline_ms(100000, 1, 0) == DEV_LIVE_MAX_MS, "capped at 10 min");
    dev_checkin_result_t r;
    dev_checkin_parse("{\"config_version\":7,\"diag_live_until\":1782044100}", &r);
    CHECK(r.ok && r.diag_live_until == 1782044100LL, "checkin answer live_until");
    dev_checkin_result_free(&r);
    dev_checkin_parse("{\"config_version\":7}", &r);
    CHECK(r.diag_live_until == 0, "absent");
    dev_checkin_result_free(&r);

    dev_sched_t s;
    dev_sched_init(&s, 0);
    dev_sched_done(&s, DEV_CHECKIN_OK, 0);
    CHECK(s.next_ms == DEV_CHECKIN_PERIOD_MS, "normal 60 s");
    dev_sched_live(&s, 300000, 1000);
    CHECK(s.next_ms == DEV_CHECKIN_LIVE_MS, "live: next at last + 5 s");
    dev_sched_done(&s, DEV_CHECKIN_OK, 5000);
    CHECK(s.next_ms == 10000, "live cadence 5 s");
    dev_sched_live(&s, 300000, 6000);
    CHECK(s.next_ms == 10000, "renewal keeps the cadence");
    dev_sched_done(&s, DEV_CHECKIN_OK, 300000);
    CHECK(s.next_ms == 300000 + DEV_CHECKIN_PERIOD_MS, "expired: back to 60 s");
    dev_sched_live(&s, 0, 300001);
    CHECK(s.live_until_ms == 0, "off");
    dev_sched_live(&s, 400000, 330000);
    CHECK(s.next_ms == 330000, "late start: now");
    dev_sched_done(&s, DEV_CHECKIN_FAILED, 331000);
    CHECK(s.next_ms == 331000 + DEV_CHECKIN_PERIOD_MS, "failures keep the backoff");
}

static void test_intervals(void)
{
    knob_settings_t k;
    knob_settings_parse("{}", &k);
    CHECK(k.stats_interval_s == 60 && k.live_interval_s == 5, "missing = 60 s / 5 s (older server)");
    knob_settings_parse("{\"stats_interval_s\":120,\"live_interval_s\":2}", &k);
    CHECK(k.stats_interval_s == 120 && k.live_interval_s == 2, "values taken");
    knob_settings_parse("{\"stats_interval_s\":5,\"live_interval_s\":60}", &k);
    CHECK(k.stats_interval_s == 30 && k.live_interval_s == 10, "clamped");
    knob_settings_parse("{\"stats_interval_s\":\"x\",\"live_interval_s\":null}", &k);
    CHECK(k.stats_interval_s == 60 && k.live_interval_s == 5, "wrong types ignored");

    CHECK(dev_checkin_period_ms(30000, true) == 30000, "30 s stats: checkin every 30 s");
    CHECK(dev_checkin_period_ms(60000, true) == 60000, "60 s");
    CHECK(dev_checkin_period_ms(300000, true) == 60000, "300 s stats: still a 60 s checkin");
    CHECK(dev_checkin_period_ms(30000, false) == 60000, "diagnostics off: 60 s");

    CHECK(dev_stats_due(0, 300000, 60000, false, true), "first report: at once");
    CHECK(dev_stats_due(1000, 300000, 60000, true, false), "live: every checkin");
    CHECK(!dev_stats_due(240000, 300000, 60000, false, false), "300 s: not at the 4th checkin");
    CHECK(dev_stats_due(300100, 300000, 60000, false, false), "300 s: at the 5th");
    CHECK(dev_stats_due(270000, 300000, 60000, false, false), "within half a period counts");
    CHECK(!dev_stats_due(25000, 60000, 60000, false, false), "60 s: an early epoch checkin carries none");
    CHECK(dev_stats_due(30000, 60000, 60000, false, false), "60 s: half a period early is fine");
    CHECK(dev_stats_due(30200, 30000, 30000, false, false), "30 s: every checkin");

    dev_sched_t s;
    dev_sched_init(&s, 0);
    CHECK(s.period_ms == DEV_CHECKIN_PERIOD_MS && s.live_ms == DEV_CHECKIN_LIVE_MS, "init: 60 s / 5 s");
    dev_sched_done(&s, DEV_CHECKIN_OK, 0);
    dev_sched_intervals(&s, 30000, 2000, 1000);
    CHECK(s.next_ms == 30000, "shorter period: next at last + 30 s");
    dev_sched_done(&s, DEV_CHECKIN_OK, 30000);
    CHECK(s.next_ms == 60000, "30 s cadence");
    dev_sched_intervals(&s, 60000, 2000, 31000);
    CHECK(s.next_ms == 60000, "longer period: from the next checkin");
    dev_sched_done(&s, DEV_CHECKIN_OK, 60000);
    CHECK(s.next_ms == 120000, "60 s cadence");
    dev_sched_live(&s, 400000, 61000);
    CHECK(s.next_ms == 62000, "live at 2 s: next at last + 2 s");
    dev_sched_done(&s, DEV_CHECKIN_OK, 62000);
    CHECK(s.next_ms == 64000, "2 s live cadence");
    dev_sched_intervals(&s, 60000, 10000, 63000);
    CHECK(s.next_ms == 64000, "longer live period: from the next checkin");
    dev_sched_done(&s, DEV_CHECKIN_OK, 64000);
    CHECK(s.next_ms == 74000, "10 s live cadence");
    dev_sched_intervals(&s, 60000, 5000, 65000);
    CHECK(s.next_ms == 69000, "shorter live period applies now");
    dev_sched_done(&s, DEV_CHECKIN_FAILED, 70000);
    int64_t backoff = s.next_ms;
    dev_sched_intervals(&s, 30000, 2000, 71000);
    CHECK(s.next_ms == backoff, "no change during a backoff");
    dev_sched_t f;
    dev_sched_init(&f, 0);
    dev_sched_intervals(&f, 30000, 2000, 0);
    CHECK(f.next_ms == 0 && f.period_ms == 30000, "before the first checkin: stays due now");
    dev_sched_done(&f, DEV_CHECKIN_UNAUTHORIZED, 1000);
    CHECK(f.next_ms == 61000, "401 at a 30 s interval: still every 60 s");
    dev_sched_live(&f, 400000, 2000);
    dev_sched_done(&f, DEV_CHECKIN_UNAUTHORIZED, 3000);
    CHECK(f.next_ms == 63000, "401 in live mode: every 60 s");
}

static void test_checkin_diag(void)
{
    char out[1024];
    dev_diag_t d = {.reset_reason = "task_wdt", .boots = 42, .heap_internal_min = 71234, .heap_largest_min = 30720,
                    .n_tasks = 3, .has_crash = true, .crash_reason = "panic", .crash_pc = 0x4201a2b3};
    strcpy(d.tasks[0].name, "ember");
    d.tasks[0].stack_free = 1880;
    strcpy(d.tasks[1].name, "bad\"name");
    d.tasks[1].stack_free = 1;
    strcpy(d.tasks[2].name, "lvgl");
    d.tasks[2].stack_free = 2304;
    strcpy(d.crash_task, "ember");
    dev_checkin_t c = {.fw = "0.9.13", .rssi = -60, .heap_internal_free = 100, .heap_internal_largest = 50,
                       .uptime_s = 9, .config_version = 3, .diag = &d};
    size_t n = dev_checkin_body(&c, NULL, out, sizeof out);
    const char *want = "{\"config_version\":3,\"diag\":{\"boots\":42,\"crash\":{\"pc\":\"0x4201a2b3\",\"reason\":\"panic\","
                       "\"task\":\"ember\"},\"heap_internal_min\":71234,\"heap_largest_min\":30720,\"reset_reason\":\"task_wdt\","
                       "\"stack_free\":{\"ember\":1880,\"lvgl\":2304}},\"fw\":\"0.9.13\",\"heap_internal_free\":100,"
                       "\"heap_internal_largest\":50,\"rssi\":-60,\"uptime_s\":9}";
    CHECK(n == strlen(want) && strcmp(out, want) == 0, "diag body: %s", out);
    cJSON *j = cJSON_Parse(out);
    CHECK(j && cJSON_IsObject(cJSON_GetObjectItem(cJSON_GetObjectItem(j, "diag"), "crash")), "diag valid JSON");
    cJSON_Delete(j);

    dev_diag_t m = {.boots = 1, .n_tasks = 0, .has_crash = true, .crash_reason = "Bad Reason", .reset_reason = NULL};
    strcpy(m.crash_task, "\x01");
    c.diag = &m;
    dev_checkin_body(&c, NULL, out, sizeof out);
    CHECK(strstr(out, "\"diag\":{\"boots\":1,\"crash\":{\"pc\":\"0x00000000\",\"reason\":\"unknown\"},"
                      "\"heap_internal_min\":0,\"heap_largest_min\":0},\"fw\"") != NULL,
          "diag minimal, bad reason and task dropped: %s", out);
    CHECK(dev_checkin_body(&c, NULL, out, 120) == 0, "diag that does not fit: 0");

    CHECK(strcmp(dev_crash_reason_name(4), "panic") == 0 && strcmp(dev_crash_reason_name(5), "int_wdt") == 0 &&
              strcmp(dev_crash_reason_name(6), "task_wdt") == 0 && strcmp(dev_crash_reason_name(7), "wdt") == 0 &&
              strcmp(dev_crash_reason_name(1), "unknown") == 0 && strcmp(dev_crash_reason_name(3), "unknown") == 0 &&
              strcmp(dev_crash_reason_name(9), "unknown") == 0,
          "crash reasons");

    dev_twdt_capture_t cap = {0};
    const char *msgs[] = {"Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:",
                          "\n - ", "ember", " (CPU 0/1)", "\n - ", "IDLE1", " (CPU 1)"};
    for (size_t i = 0; i < sizeof msgs / sizeof msgs[0]; i++) dev_twdt_capture_msg(&cap, msgs[i]);
    CHECK(strcmp(cap.name, "ember") == 0, "twdt culprit: first name: %s", cap.name);
    dev_twdt_capture_t lng = {0};
    dev_twdt_capture_msg(&lng, "\n - ");
    dev_twdt_capture_msg(&lng, "a_name_longer_than_sixteen");
    CHECK(strlen(lng.name) == DEV_TASK_NAME_MAX, "twdt culprit cut at 16");
    dev_twdt_capture_t none = {0};
    dev_twdt_capture_msg(&none, "caption only");
    dev_twdt_capture_msg(&none, NULL);
    CHECK(none.name[0] == 0 && none.state == 0, "no culprit");
    CHECK(strcmp(dev_crash_task("task_wdt", "ember", "IDLE1"), "ember") == 0 &&
              strcmp(dev_crash_task("task_wdt", "", "IDLE1"), "twdt") == 0 &&
              strcmp(dev_crash_task("task_wdt", NULL, "IDLE1"), "twdt") == 0 &&
              strcmp(dev_crash_task("panic", "ember", "lvgl"), "lvgl") == 0 &&
              strcmp(dev_crash_task("unknown", NULL, "lvgl"), "lvgl") == 0 &&
              strcmp(dev_crash_task("lvgl_stall", NULL, "stats"), "lvgl") == 0,
          "crash task");
    CHECK(strcmp(dev_crash_reason_name(DEV_RR_LVGL_STALL), "lvgl_stall") == 0 &&
              strcmp(dev_boot_reason_name(DEV_RR_PANIC, true), "lvgl_stall") == 0 &&
              strcmp(dev_boot_reason_name(DEV_RR_PANIC, false), "panic") == 0,
          "lvgl_stall names");
}

static int stall_cycle(dev_stall_note_t *n)
{
    CHECK(dev_stall_check(n, true, 90000) == DEV_STALL_ABORT, "stall aborts below the cap");
    return dev_stall_boot(n, DEV_RR_PANIC);
}

static void test_stall_guard(void)
{
    dev_stall_note_t n;
    memset(&n, 0xA5, sizeof n);
    CHECK(!dev_stall_boot(&n, DEV_RR_PANIC) && n.resets == 0 && n.magic == DEV_STALL_MAGIC, "garbage note: cleared");
    CHECK(dev_stall_check(&n, false, 60000) == DEV_STALL_NONE, "no stall: nothing");
    for (int i = 1; i <= DEV_STALL_MAX_RESETS; i++) CHECK(stall_cycle(&n) && n.resets == (uint32_t)i, "stall reset %d counted", i);
    CHECK(dev_stall_check(&n, true, 90000) == DEV_STALL_LOG && !n.marked, "after %d resets: log only", DEV_STALL_MAX_RESETS);
    CHECK(dev_stall_check(&n, false, DEV_STALL_CLEAR_MS) == DEV_STALL_NONE && n.resets == DEV_STALL_MAX_RESETS,
          "a boot that stalled does not clear");
    CHECK(!dev_stall_boot(&n, DEV_RR_SW) && n.resets == DEV_STALL_MAX_RESETS, "SW reset keeps the count");
    CHECK(dev_stall_check(&n, false, DEV_STALL_CLEAR_MS - 1) == DEV_STALL_NONE && n.resets == DEV_STALL_MAX_RESETS,
          "not yet 10 min");
    dev_stall_check(&n, false, DEV_STALL_CLEAR_MS);
    CHECK(n.resets == 0, "10 min without a stall clears");
    CHECK(dev_stall_check(&n, true, DEV_STALL_CLEAR_MS + 30000) == DEV_STALL_ABORT, "armed again in the same boot");

    dev_stall_boot(&n, DEV_RR_PANIC);
    CHECK(n.resets == 1, "stall counted");
    CHECK(!dev_stall_boot(&n, DEV_RR_PANIC) && n.resets == 0, "panic without the mark clears");
    stall_cycle(&n);
    CHECK(!dev_stall_boot(&n, 1) && n.resets == 0, "power-on clears");
    stall_cycle(&n);
    CHECK(!dev_stall_boot(&n, 6) && n.resets == 0, "task_wdt clears");
    n.marked = 1;
    CHECK(!dev_stall_boot(&n, DEV_RR_SW) && !n.marked, "the mark is one boot only");
}

static void test_json_nesting(void)
{
    char doc[2 * (CJSON_NESTING_LIMIT + 1) + 1];
    for (int depth = CJSON_NESTING_LIMIT; depth <= CJSON_NESTING_LIMIT + 1; depth++) {
        memset(doc, '[', (size_t)depth);
        memset(doc + depth, ']', (size_t)depth);
        doc[2 * depth] = 0;
        cJSON *j = cJSON_Parse(doc);
        CHECK(depth == CJSON_NESTING_LIMIT ? j != NULL : j == NULL, "cJSON nesting limit");
        cJSON_Delete(j);
    }
    CHECK(CJSON_NESTING_LIMIT == 32, "cJSON nesting limit matches the firmware build");
}

int main(void)
{
    test_json_nesting();
    test_intervals();
    test_stall_guard();
    test_epoch_reset();
    test_diagnostics();
    test_link();
    test_settings_defaults();
    test_settings_values();
    test_pages();
    test_checkin_body();
    test_checkin_diag();
    test_checkin_answer();
    test_schedule();
    if (failures) {
        printf("device: %d failure(s)\n", failures);
        return 1;
    }
    printf("device: all tests passed\n");
    return 0;
}
