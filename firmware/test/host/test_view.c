#include <math.h>
#include <stdio.h>
#include <string.h>

#include "knob_view.h"
#include "pomo.h"
#include "view_policy.h"
#include "weather_face.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *FULL =
    "{\"v\":1,\"epoch\":1,\"config_version\":1,\"mood\":{\"waiting\":1,\"errors\":0,\"running\":1,\"done\":0,"
    "\"source\":\"M4\"},\"pomo\":{\"phase\":\"focus\",\"running\":true,\"paused\":false,\"ends_at\":1782044100,"
    "\"planned_sec\":1500,\"round\":0},\"weather\":{\"provider\":\"open-meteo\",\"cond\":\"rain\",\"code\":\"61\","
    "\"temp_c\":12.5,\"stale\":false,\"severe\":false,\"night\":false,\"sunrise\":1782013200,\"sunset\":1782072900},"
    "\"brightness\":{\"level\":255,\"night\":false}}";

static void test_parse(void)
{
    knob_view_t v;
    CHECK(knob_view_parse(FULL, &v), "full view parses");
    CHECK(v.v == 1 && v.epoch == 1 && v.config_version == 1, "header fields");
    CHECK(v.waiting == 1 && v.errors == 0 && v.running == 1 && v.done == 0, "counts");
    CHECK(strcmp(v.source, "M4") == 0, "source %s", v.source);
    CHECK(knob_view_mood(&v) == BOT_WAITING, "waiting wins");
    CHECK(v.has_pomo && v.pomo_counting && v.ends_at == 1782044100LL, "ends_at");
    CHECK(v.pomo.phase == POMO_PHASE_FOCUS && v.pomo.running && !v.pomo.paused && v.pomo.planned_sec == 1500 &&
              v.pomo.remaining_sec == 0,
          "pomo state");
    CHECK(v.has_weather && v.weather.valid && v.weather.enabled && !v.weather.stale, "weather present");
    CHECK(strcmp(v.weather.provider, "open-meteo") == 0 && strcmp(v.weather.condition, "rain") == 0 &&
              strcmp(v.weather.code, "61") == 0,
          "weather strings");
    CHECK(v.weather.has_temp && fabsf(v.weather.temp_c - 12.5f) < 1e-4, "temp");
    CHECK(v.weather.has_night && !v.weather.night, "Ember's day");
    CHECK(v.has_brightness && v.level == 255 && !v.bright_night, "brightness");
    char key[32];
    knob_view_epoch_key(&v, key, sizeof key);
    CHECK(strcmp(key, "1/1") == 0, "epoch key %s", key);

    CHECK(knob_view_parse("{\"v\":1,\"epoch\":7,\"config_version\":3,\"mood\":{\"waiting\":0,\"errors\":0,"
                          "\"running\":0,\"done\":2,\"source\":\"\"},\"pomo\":null,\"weather\":null,"
                          "\"brightness\":{\"level\":40,\"night\":true}}",
                          &v),
          "nulls parse");
    CHECK(!v.has_pomo && !v.has_weather && v.level == 40 && v.bright_night, "nulls");
    CHECK(knob_view_mood(&v) == BOT_DONE && v.source[0] == 0, "done, no source");
    CHECK(v.lead[0] == 0 && v.hosts == 0 && v.lead_color[0] == 0 && v.tool[0] == 0, "no lead fields");

    knob_view_epoch_key(&v, key, sizeof key);
    CHECK(strcmp(key, "7/3") == 0, "epoch key moves with either");
    CHECK(knob_view_parse("{\"v\":1,\"mood\":{\"waiting\":0,\"errors\":0,\"running\":3,\"done\":0,\"source\":\"\","
                          "\"lead\":\"M4\",\"hosts\":2,\"lead_color\":\"#B48CFF\",\"tool\":\"claude\"}}",
                          &v),
          "lead parses");
    CHECK(strcmp(v.lead, "M4") == 0 && v.hosts == 2 && strcmp(v.lead_color, "#B48CFF") == 0 &&
              strcmp(v.tool, "claude") == 0,
          "lead %s hosts %d colour %s tool %s", v.lead, v.hosts, v.lead_color, v.tool);

    CHECK(knob_view_parse("{\"v\":1,\"mood\":{},\"pomo\":{\"phase\":\"short_break\",\"running\":true,\"paused\":true,"
                          "\"remaining_sec\":120,\"planned_sec\":300,\"round\":2}}",
                          &v),
          "paused parses");
    CHECK(v.has_pomo && !v.pomo_counting && v.pomo.remaining_sec == 120 && v.pomo.paused && v.pomo.round == 2,
          "remaining_sec when not counting");
    CHECK(knob_view_mood(&v) == BOT_IDLE && !v.has_brightness, "idle; no brightness");

    CHECK(knob_view_parse("{\"v\":1,\"mood\":{},\"weather\":{\"provider\":\"met-no\",\"cond\":\"clear\","
                          "\"code\":\"clearsky_night\",\"temp_c\":3,\"night\":false,\"sunrise\":null,\"sunset\":null}}",
                          &v),
          "no-location weather parses");
    CHECK(v.has_weather && !v.weather.has_night, "night unknown without sun times");
    wx_look_t l = wx_look_from_obs(&v.weather);
    CHECK(l.face == WX_CLEAR_NIGHT, "met-no suffix decides without sun times");
    CHECK(knob_view_parse(FULL, &v), "reparse");
    v.weather.night = true;
    l = wx_look_from_obs(&v.weather);
    CHECK(l.night, "Ember's night wins");
    v.weather.night = false;
    v.weather.code[0] = 0;
    strcpy(v.weather.condition, "clear");
    l = wx_look_from_obs(&v.weather);
    CHECK(l.face == WX_CLEAR_DAY && !l.still, "Ember's day, clear");

    CHECK(v.diag_live_until == 0, "no live mode");
    CHECK(knob_view_parse("{\"v\":1,\"mood\":{},\"brightness\":{\"level\":5,\"night\":false},\"diag_live_until\":1782044400}", &v) &&
              v.diag_live_until == 1782044400LL,
          "diag_live_until");
    CHECK(!knob_view_parse("", &v), "empty body (a 304) does not parse");
    CHECK(!knob_view_parse(NULL, &v), "NULL");
    CHECK(!knob_view_parse("[]", &v), "array");
    CHECK(knob_view_parse("{\"v\":1}", &v) && !v.has_mood && knob_view_mood(&v) == BOT_IDLE && v.lead[0] == 0 &&
              !v.has_pomo && !v.has_weather && !v.has_np && !v.has_brightness,
          "no mood, pomo or weather key: a view, every block off");
    CHECK(knob_view_parse(FULL, &v) && v.has_mood, "mood present");
    CHECK(!knob_view_parse("{\"mood\":{}}", &v), "no v");
    CHECK(!knob_view_parse("{\"v\":\"1\",\"mood\":{}}", &v), "v not a number");
    CHECK(knob_view_read("{\"v\":1.5,\"mood\":{}}", &v, NULL) == KNOB_VIEW_BAD, "v 1.5: bad, not too new");
    CHECK(knob_view_read("{\"v\":0.5,\"mood\":{}}", &v, NULL) == KNOB_VIEW_BAD, "v 0.5: bad, not too old");
    CHECK(knob_view_read("{\"v\":99.5}", &v, NULL) == KNOB_VIEW_BAD, "v 99.5: bad");
    CHECK(knob_view_read("{\"mood\":{}}", &v, NULL) == KNOB_VIEW_BAD, "no v: bad");
    CHECK(knob_view_read("{\"v\":\"1\"}", &v, NULL) == KNOB_VIEW_BAD, "string v: bad");
    CHECK(!knob_view_parse("{\"v\":1,\"mood\":{\"waiting\":1", &v) && v.waiting == 0, "cut short: zeroed");
    CHECK(knob_view_parse("{\"v\":1,\"mood\":{\"waiting\":0,\"errors\":0,\"running\":0,\"done\":0},\"brightness\":{\"level\":300}}",
                          &v) &&
              !v.has_brightness,
          "level out of range ignored");
    CHECK(knob_view_mood_from(0, 3, 1, 1) == BOT_ERROR && knob_view_mood_from(0, 0, 1, 1) == BOT_WORKING,
          "priority");
}

static void test_major(void)
{
    knob_view_t v;
    CHECK(knob_view_parse(FULL, &v) && v.waiting == 1, "last good view");
    knob_view_t keep = v;
    int major = 0;
    CHECK(knob_view_read("{\"v\":99,\"mood\":{\"waiting\":0,\"running\":4},\"pomo\":null}", &v, &major) ==
              KNOB_VIEW_TOO_NEW && major == 99,
          "v 99: too new");
    CHECK(memcmp(&v, &keep, sizeof v) == 0, "too new: last view kept");
    CHECK(knob_view_read("{\"v\":2}", &v, NULL) == KNOB_VIEW_TOO_NEW, "v 2: too new");
    CHECK(knob_view_read("{\"v\":0,\"mood\":{}}", &v, &major) == KNOB_VIEW_TOO_OLD && major == 0, "v 0: too old");
    CHECK(knob_view_read("{\"v\":-3}", &v, &major) == KNOB_VIEW_TOO_OLD && major == -3, "v -3: too old");
    CHECK(memcmp(&v, &keep, sizeof v) == 0, "too old: last view kept");
    CHECK(knob_view_read("{\"v\":1e300}", &v, &major) == KNOB_VIEW_TOO_NEW && major == 1000000000, "huge v clamped");
    CHECK(!knob_view_parse("{\"v\":99,\"mood\":{}}", &v), "parse: false for an unsupported major");
    CHECK(memcmp(&v, &keep, sizeof v) == 0, "parse: last view kept");
    CHECK(knob_view_read("{\"v\":1,\"mood\":{\"running\":2}}", &v, &major) == KNOB_VIEW_OK && major == 1 &&
              knob_view_mood(&v) == BOT_WORKING,
          "supported again");
    CHECK(knob_view_read("[1]", &v, NULL) == KNOB_VIEW_BAD && v.v == 0, "bad: zeroed");
    CHECK(KNOB_VIEW_V_MIN == 1 && KNOB_VIEW_V_MAX == 1, "parses view major 1 only");
}

#define V1_WAITING "{\"v\":1,\"mood\":{\"waiting\":1,\"lead\":\"M4\"},\"pomo\":null}"
#define V1_WORKING "{\"v\":1,\"mood\":{\"running\":1}}"
#define V2 "{\"v\":2,\"mood\":{\"errors\":5}}"

static knob_step_t step(knob_view_state_t *s, int status, const char *body) { return knob_view_step(s, status, body); }

static void test_step(void)
{
    knob_view_state_t s = {0};
    knob_view_state_reset(&s);
    CHECK(!knob_view_conditional(&s), "fresh: no If-None-Match");

    knob_step_t r = step(&s, 200, V1_WAITING);
    CHECK(r.act == KNOB_STEP_APPLY && r.mood == BOT_WAITING && r.etag_set && !r.etag_clear, "v1: apply");
    CHECK(s.have_view && !s.unsupported && s.compat == KNOB_COMPAT_OK && knob_view_conditional(&s), "v1: state");
    r = step(&s, 304, "");
    CHECK(r.act == KNOB_STEP_APPLY && r.mood == BOT_WAITING && !r.etag_set && !r.etag_clear, "304: apply the cached view");

    r = step(&s, 200, V2);
    CHECK(r.act == KNOB_STEP_RESTORE && r.mood == BOT_WAITING && r.etag_set && !r.etag_clear,
          "v2: last view's mood, ETag kept");
    CHECK(s.unsupported && s.have_view && s.compat == KNOB_COMPAT_UPDATE_KNOB && s.major == 2, "v2: update the knob");
    CHECK(strcmp(s.view.lead, "M4") == 0 && s.view.waiting == 1 && s.view.errors == 0, "v2: last view kept");

    r = step(&s, -1, NULL);
    CHECK(r.act == KNOB_STEP_NONE && r.mood == -1 && !r.etag_set && !r.etag_clear, "disconnect: nothing");
    CHECK(s.unsupported && s.compat == KNOB_COMPAT_UPDATE_KNOB && knob_view_conditional(&s), "disconnect: state kept");
    r = step(&s, 304, "");
    CHECK(r.act == KNOB_STEP_RESTORE && r.mood == BOT_WAITING && !r.etag_clear,
          "v1, v2, disconnect, 304: the kept view's mood comes back");
    r = step(&s, 503, "");
    CHECK(r.act == KNOB_STEP_NONE && s.unsupported, "5xx: nothing");

    r = step(&s, 200, V1_WORKING);
    CHECK(r.act == KNOB_STEP_APPLY && r.mood == BOT_WORKING && r.etag_set, "v1 again: apply");
    CHECK(!s.unsupported && s.compat == KNOB_COMPAT_OK, "v1 again: supported");

    knob_view_state_reset(&s);
    r = step(&s, 200, "{\"v\":0}");
    CHECK(r.act == KNOB_STEP_NONE && r.mood == -1 && r.etag_set, "first view too old: nothing to show, ETag kept");
    CHECK(s.unsupported && !s.have_view && s.compat == KNOB_COMPAT_UPDATE_EMBER, "first view too old: update Ember");
    CHECK(knob_view_conditional(&s), "unsupported without a view still sends If-None-Match");
    r = step(&s, 304, "");
    CHECK(r.act == KNOB_STEP_NONE && !r.etag_clear, "304 after an unsupported first view: no refetch");

    r = step(&s, 200, "{\"v\":1.5}");
    CHECK(r.act == KNOB_STEP_UNPARSED && r.etag_clear && !s.unsupported && !s.have_view, "bad after unsupported");
    CHECK(s.compat == KNOB_COMPAT_UPDATE_EMBER, "bad keeps the label until a good view");
    r = step(&s, 304, "");
    CHECK(r.act == KNOB_STEP_REFETCH && r.etag_clear, "304 without a view: refetch");

    r = step(&s, 200, "{\"v\":99}");
    CHECK(s.compat == KNOB_COMPAT_UPDATE_KNOB, "too new");
    knob_view_state_reset(&s);
    CHECK(!s.unsupported && !s.have_view && s.compat == KNOB_COMPAT_OK && !knob_view_conditional(&s),
          "reset (token change, legacy) clears it");

    r = step(&s, 200, "{\"v\":1}");
    CHECK(r.act == KNOB_STEP_APPLY && r.mood == -1, "no mood block: mood unknown, not idle");
    r = step(&s, 200, V2);
    CHECK(r.act == KNOB_STEP_RESTORE && r.mood == -1, "restore without a mood block: unknown");
}

static void test_quiet(void)
{
    knob_view_t v;
    CHECK(knob_view_parse(FULL, &v) && !v.quiet, "absent quiet: not quiet");
    CHECK(knob_view_parse("{\"v\":1,\"mood\":{},\"quiet\":true}", &v) && v.quiet, "quiet true");
    CHECK(knob_view_parse("{\"v\":1,\"mood\":{},\"quiet\":false}", &v) && !v.quiet, "quiet false");
    CHECK(knob_view_parse("{\"v\":1,\"mood\":{},\"quiet\":1}", &v) && !v.quiet, "quiet not a bool: not quiet");
    CHECK(knob_view_parse("{\"v\":1,\"mood\":{},\"quiet\":true}", &v) && knob_view_parse("{\"v\":1,\"mood\":{}}", &v) && !v.quiet,
          "next view without quiet: off again");
    CHECK(knob_quiet_next(true, KNOB_QUIET_LEGACY, false), "legacy fallback keeps quiet");
    CHECK(!knob_quiet_next(false, KNOB_QUIET_LEGACY, true), "legacy fallback: no new quiet");
    CHECK(!knob_quiet_next(true, KNOB_QUIET_VIEW, false), "a view without quiet clears it");
    CHECK(knob_quiet_next(false, KNOB_QUIET_VIEW, true), "a view with quiet sets it");
}

static void test_srv_clock(void)
{
    pomo_srv_clock_t c;
    pomo_srv_clock_init(&c);
    CHECK(pomo_srv_clock_offset(&c) == 0, "no answer: 0");
    double off = 1000000000.3;
    for (int i = 0; i < 40; i++) {
        double t = 10 + i * 1.37;
        long long now_hdr = (long long)floor(t + 0.02 + off);
        pomo_srv_clock_note(&c, now_hdr, t, t + 0.05);
    }
    CHECK(fabs(pomo_srv_clock_offset(&c) - off) < 0.1, "converges: %.3f", pomo_srv_clock_offset(&c) - off);
    pomo_srv_clock_note(&c, (long long)floor(100 + off + 30), 100, 100.05);
    CHECK(fabs(pomo_srv_clock_offset(&c) - (off + 30)) < 1.0, "step: restarts");
    pomo_srv_clock_init(&c);
    pomo_srv_clock_note(&c, 1782044000, 5.0, 5.1);
    CHECK(fabs(pomo_srv_clock_offset(&c) - (1782044000 + 0.5 - 5.05)) < 0.6, "single answer");
}

static pomo_state_t focus_run(void)
{
    return (pomo_state_t){.phase = POMO_PHASE_FOCUS, .running = true, .planned_sec = 1500};
}

static void test_clock_end(void)
{
    pomo_clock_t c;
    pomo_clock_init(&c);
    pomo_state_t s = focus_run();
    double off = 1782040000.0 - 100.0;
    pomo_clock_sync_end(&c, &s, 1782040600, off, 100.0);
    CHECK(c.valid && c.state.remaining_sec == 601, "ceil of 600.5: %d", c.state.remaining_sec);
    CHECK(pomo_clock_at(&c, 100.0).remaining_sec == 601, "now");
    CHECK(pomo_clock_at(&c, 100.49).remaining_sec == 601, "before the flip");
    CHECK(pomo_clock_at(&c, 100.51).remaining_sec == 600, "flips at the half second: %d",
          pomo_clock_at(&c, 100.51).remaining_sec);
    CHECK(pomo_clock_at(&c, 400.6).remaining_sec == 300, "five minutes later");
    double tb = c.t_base;
    pomo_clock_sync_end(&c, &s, 1782040600, off + 0.3, 101.0);
    CHECK(c.t_base < tb && tb - c.t_base <= 0.0501, "earlier end slews: %.3f", tb - c.t_base);
    tb = c.t_base;
    pomo_clock_sync_end(&c, &s, 1782040600, off - 0.6, 101.5);
    CHECK(c.t_base == tb, "later end within 1 s keeps the base");
    pomo_clock_sync_end(&c, &s, 1782040601, off - 0.5, 101.5);
    CHECK(c.t_base != tb && pomo_clock_at(&c, 101.5).remaining_sec == 601, "end 1.55 s later rebases: %d",
          pomo_clock_at(&c, 101.5).remaining_sec);
    tb = c.t_base;
    pomo_clock_sync_end(&c, &s, 1782040660, off, 101.0);
    CHECK(c.t_base != tb && pomo_clock_at(&c, 101.0).remaining_sec == 660, "new end rebases: %d",
          pomo_clock_at(&c, 101.0).remaining_sec);
    pomo_clock_sync_end(&c, &s, 1782040000, off, 200.0);
    CHECK(pomo_clock_at(&c, 200.0).remaining_sec == 0, "past end");
    pomo_state_t p = s;
    p.paused = true;
    p.remaining_sec = 450;
    pomo_clock_sync(&c, &p, 300.0);
    CHECK(pomo_clock_at(&c, 330.0).remaining_sec == 450, "paused holds");
    pomo_clock_sync_end(&c, &s, 1782040000 + 200 + 450, off, 300.0);
    CHECK(pomo_clock_at(&c, 300.0).remaining_sec == 451 && pomo_clock_at(&c, 310.6).remaining_sec == 440,
          "resumed: %d", pomo_clock_at(&c, 310.6).remaining_sec);
}

static void drift_run(double ppm)
{
    pomo_srv_clock_t sc;
    pomo_srv_clock_init(&sc);
    pomo_clock_t c;
    pomo_clock_init(&c);
    pomo_state_t s = {.phase = POMO_PHASE_FOCUS, .running = true, .planned_sec = 7 * 3600};
    const double srv0 = 1782040000.25;
    const long long ends_at = 1782040000LL + 7 * 3600;
    double max_err = 0;
    int last_shown = -1, bad_steps = 0;
    unsigned seed = 7;
    double t = 10.0;
    for (int i = 0; t < 10.0 + 6 * 3600; i++) {
        seed = seed * 1103515245u + 12345u;
        double rtt = 0.010 + (seed >> 16) % 40 / 1000.0;
        if (i) t += 2.0 + rtt;
        double srv_at = srv0 + (t + rtt / 2 - 10.0) * (1.0 - ppm * 1e-6);
        pomo_srv_clock_note(&sc, (long long)floor(srv_at), t, t + rtt);
        double true_off = srv_at - (t + rtt / 2);
        double err = fabs(pomo_srv_clock_offset(&sc) - true_off);
        if (i > 100 && err > max_err) max_err = err;
        pomo_clock_sync_end(&c, &s, ends_at, pomo_srv_clock_offset(&sc), t + rtt);
        for (int k = 0; k < 20; k++) {
            int shown = pomo_clock_at(&c, t + rtt + k * 0.1).remaining_sec;
            if (last_shown >= 0 && (shown > last_shown || last_shown - shown > 1)) bad_steps++;
            last_shown = shown;
        }
    }
    CHECK(sc.resets == 0, "%.0f ppm: no resets (%d)", ppm, sc.resets);
    CHECK(max_err < 0.1, "%.0f ppm: offset error %.3f s", ppm, max_err);
    CHECK(bad_steps == 0, "%.0f ppm: shown second stepped %d times", ppm, bad_steps);
}

static void test_drift(void)
{
    drift_run(20);
    drift_run(-20);
    drift_run(0);
}

static void test_policy(void)
{
    view_policy_t p;
    view_policy_init(&p, false);
    CHECK(p.legacy && !view_policy_try_view(&p, 0) && p.why == VIEW_WHY_NO_DEVICE, "no device: legacy, no probe");
    CHECK(!view_policy_result(&p, 200, 0) && p.legacy, "no device: a stray 200 changes nothing");

    view_policy_init(&p, true);
    CHECK(!p.legacy && view_policy_try_view(&p, 0), "device: view first");
    CHECK(!view_policy_result(&p, 200, 0) && !p.legacy, "200 stays");
    CHECK(!view_policy_result(&p, 304, 0) && !p.legacy, "304 stays");
    CHECK(!view_policy_result(&p, -1, 0) && !p.legacy, "network error stays");
    CHECK(!view_policy_result(&p, 429, 0) && !p.legacy, "429 stays");

    CHECK(view_policy_result(&p, 404, 1000) && p.legacy && p.why == VIEW_WHY_OLD_SERVER, "404: legacy");
    CHECK(!view_policy_try_view(&p, 1000 + VIEW_REPROBE_OLD_MS - 1), "no probe before 10 min");
    CHECK(view_policy_try_view(&p, 1000 + VIEW_REPROBE_OLD_MS), "probe after 10 min");
    CHECK(!view_policy_result(&p, 404, 700000) && p.legacy && p.probe_ms == 700000 + VIEW_REPROBE_OLD_MS,
          "still 404: wait again");
    CHECK(view_policy_result(&p, 200, 1400000) && !p.legacy && p.why == VIEW_WHY_OK, "upgraded server: view");

    CHECK(view_policy_result(&p, 401, 0) && p.legacy && p.why == VIEW_WHY_REJECTED, "401: legacy reads");
    CHECK(!view_policy_try_view(&p, VIEW_REPROBE_MS - 1) && view_policy_try_view(&p, VIEW_REPROBE_MS), "401: 60 s");
    view_policy_device_changed(&p, true, 5);
    CHECK(view_policy_try_view(&p, 5), "new token: probe now");
    CHECK(!view_policy_result(&p, -1, 10) && p.legacy && !view_policy_try_view(&p, 11) &&
              view_policy_try_view(&p, 10 + VIEW_REPROBE_MS),
          "failed probe waits 60 s");

    view_policy_init(&p, true);
    CHECK(!view_policy_result(&p, 500, 0) && !view_policy_result(&p, 503, 0) && !p.legacy, "two 5xx: still view");
    CHECK(!view_policy_result(&p, 200, 0) && !view_policy_result(&p, 500, 0) && !view_policy_result(&p, 500, 0) &&
              !p.legacy,
          "a 200 resets the 5xx count");
    CHECK(view_policy_result(&p, 500, 0) && p.legacy && p.why == VIEW_WHY_SERVER_ERROR, "third 5xx: legacy");
    view_policy_device_changed(&p, false, 0);
    CHECK(p.legacy && !view_policy_try_view(&p, 1LL << 40), "device removed: legacy for good");

    CHECK(view_answer(200, false) == VIEW_ANS_NEW && view_answer(200, true) == VIEW_ANS_NEW, "200 new");
    CHECK(view_answer(304, true) == VIEW_ANS_SAME, "304 same");
    CHECK(view_answer(304, false) == VIEW_ANS_REFETCH, "304 without a view: refetch");
    CHECK(view_answer(-1, true) == VIEW_ANS_FAILED && view_answer(404, true) == VIEW_ANS_FAILED, "failed");
}

static void test_etag(void)
{
    view_etag_t e;
    view_etag_clear(&e);
    CHECK(view_etag_get(&e) == NULL, "empty: no If-None-Match");
    CHECK(view_etag_set(&e, "\"0123456789abcdef\"") && strcmp(view_etag_get(&e), "\"0123456789abcdef\"") == 0,
          "Ember's strong tag");
    CHECK(view_etag_set(&e, "W/\"x1\"") && strcmp(view_etag_get(&e), "W/\"x1\"") == 0, "weak tag");
    CHECK(!view_etag_set(&e, "abc") && view_etag_get(&e) == NULL, "unquoted: cleared");
    CHECK(!view_etag_set(&e, "\"a b\"") && !view_etag_set(&e, "\"a\r\n\"") && !view_etag_set(&e, "\"a\"b\""),
          "space / CRLF / quote inside");
    CHECK(!view_etag_set(&e, "\"") && !view_etag_set(&e, "") && !view_etag_set(&e, NULL), "too short");
    char big[VIEW_ETAG_MAX + 4];
    memset(big, 'a', sizeof big);
    big[0] = '"';
    big[VIEW_ETAG_MAX] = '"';
    big[VIEW_ETAG_MAX + 1] = 0;
    CHECK(!view_etag_set(&e, big), "too long");
    CHECK(view_etag_set(&e, "\"\""), "empty tag is a valid tag");

    long long n;
    CHECK(view_parse_now("1782044100", &n) && n == 1782044100LL, "X-Ember-Now");
    CHECK(!view_parse_now("", &n) && !view_parse_now(NULL, &n) && !view_parse_now("-5", &n) &&
              !view_parse_now("17820441OO", &n) && !view_parse_now(" 1782044100", &n),
          "malformed");
    CHECK(!view_parse_now("12", &n), "implausible (1970)");
    CHECK(!view_parse_now("99999999999999999999999", &n), "overflow");
}

int main(void)
{
    test_parse();
    test_major();
    test_step();
    test_quiet();
    test_srv_clock();
    test_clock_end();
    test_drift();
    test_policy();
    test_etag();
    if (failures) {
        printf("view: %d failure(s)\n", failures);
        return 1;
    }
    printf("view: all tests passed\n");
    return 0;
}
