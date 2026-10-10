#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "cfg.h"
#include "coredump_up.h"
#include "device_api.h"
#include "knob_caps.h"
#include "knob_settings.h"
#include "knob_view.h"
#include "ota_policy.h"
#include "pages.h"
#include "pomo.h"
#include "pomo_legacy.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *dir;

static const char *const COVERED[] = {
    "view_full.json",          "view_minimal.json",           "view_nowplaying_none.json",
    "view_single_host_paused.json", "checkin_req_full.json", "checkin_req_minimal.json",
    "checkin_reply_current.json", "checkin_reply_config.json", "checkin_reply_coredump.json",
    "checkin_reply_ota.json",  "checkin_reply_rotation.json", "config_default.json",
    "config_custom.json",      "pomodoro_action.json",        "view_quiet.json",
    "checkin_req_caps.json",   "checkin_reply_caps.json",     "view_caps_limited.json",
};
#define N_COVERED (sizeof COVERED / sizeof COVERED[0])

static char *load(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        failures++;
        printf("FAIL: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    size_t got = buf ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!buf || got != (size_t)n) {
        failures++;
        printf("FAIL: cannot read %s\n", path);
        free(buf);
        return NULL;
    }
    buf[n] = 0;
    return buf;
}

static void test_all_covered(void)
{
    DIR *d = opendir(dir);
    CHECK(d != NULL, "fixture dir %s", dir);
    if (!d) return;
    int seen = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 5, ".json") != 0) continue;
        bool known = false;
        for (size_t i = 0; i < N_COVERED; i++) known |= strcmp(e->d_name, COVERED[i]) == 0;
        CHECK(known, "fixture %s has no host test", e->d_name);
        seen++;
    }
    closedir(d);
    CHECK(seen == (int)N_COVERED, "%d fixtures, %d covered", seen, (int)N_COVERED);
}

static bool view(const char *name, knob_view_t *v)
{
    char *j = load(name);
    bool ok = j && knob_view_parse(j, v);
    free(j);
    CHECK(ok, "%s parses", name);
    return ok;
}

static void test_view_full(void)
{
    knob_view_t v;
    if (!view("view_full.json", &v)) return;
    char key[32];
    knob_view_epoch_key(&v, key, sizeof key);
    CHECK(v.v == 1 && strcmp(key, "2/2") == 0, "header v %d key %s", v.v, key);
    CHECK(v.waiting == 3 && v.errors == 0 && v.running == 1 && v.done == 0, "counts");
    CHECK(knob_view_mood(&v) == BOT_WAITING, "waiting wins");
    CHECK(v.source[0] == 0 && strcmp(v.lead, "STUDIO") == 0 && v.hosts == 2, "lead %s over source %s, hosts %d", v.lead,
          v.source, v.hosts);
    CHECK(strcmp(v.lead_color, "#00C8C8") == 0 && strcmp(v.tool, "claude") == 0, "colour %s tool %s", v.lead_color, v.tool);
    CHECK(v.has_pomo && v.pomo_counting && v.ends_at == 1782044700LL && v.pomo.remaining_sec == 0, "counting pomo");
    CHECK(v.pomo.phase == POMO_PHASE_FOCUS && v.pomo.running && !v.pomo.paused && v.pomo.planned_sec == 1500 &&
              v.pomo.round == 0,
          "pomo state");
    CHECK(pomo_mode(&v.pomo) == POMO_MODE_RUNNING, "pomo running");
    CHECK(v.has_weather && v.weather.valid && !v.weather.stale && !v.weather.severe, "weather present");
    CHECK(strcmp(v.weather.provider, "open-meteo") == 0 && strcmp(v.weather.condition, "rain") == 0 &&
              strcmp(v.weather.code, "61") == 0,
          "weather strings");
    CHECK(v.weather.has_temp && fabsf(v.weather.temp_c - 12.5f) < 1e-4f, "temp %.2f", (double)v.weather.temp_c);
    CHECK(v.weather.has_night && !v.weather.night, "sun times: Ember's day/night is used");
    CHECK(v.has_brightness && v.level == 255 && !v.bright_night, "brightness");
    CHECK(v.has_np && v.np.state == NP_PLAYING, "playing");
    CHECK(strcmp(v.np.source, "plex") == 0 && strcmp(v.np.title, "Example Song") == 0 &&
              strcmp(v.np.artist, "Example Artist") == 0 && strcmp(v.np.album, "Example Album") == 0 &&
              strcmp(v.np.track_id, "4242") == 0,
          "np strings");
    CHECK(v.np.duration_ms == 330000 && v.np.position_ms == 61000 && v.np.position_at == 1782043198000LL, "np times");
    CHECK(strcmp(v.np.art_version, "1351688abc58") == 0 && v.np.album_art && v.np.artist_art && v.np.volume == 40,
          "np art %s volume %d", v.np.art_version, v.np.volume);
    CHECK(v.diag_live_until == 1782043500LL, "diag_live_until");
}

static void test_view_minimal(void)
{
    knob_view_t v;
    if (!view("view_minimal.json", &v)) return;
    char key[32];
    knob_view_epoch_key(&v, key, sizeof key);
    CHECK(strcmp(key, "1/1") == 0, "key %s", key);
    CHECK(knob_view_mood(&v) == BOT_IDLE && v.source[0] == 0 && v.lead[0] == 0 && v.hosts == 0, "idle, no source");
    CHECK(!v.has_pomo && !v.has_weather, "pomo and weather null");
    CHECK(!v.has_np && v.diag_live_until == 0, "nowplaying and live mode absent");
    CHECK(v.has_brightness && v.level == 255, "brightness");
}

static void test_view_nowplaying_none(void)
{
    knob_view_t v;
    if (!view("view_nowplaying_none.json", &v)) return;
    CHECK(v.has_np && v.np.state == NP_NONE && v.np.title[0] == 0 && v.np.volume == -1, "nothing playing");
    CHECK(v.has_pomo && !v.pomo_counting && v.pomo.phase == POMO_PHASE_IDLE && v.pomo.remaining_sec == 0, "idle pomo");
    CHECK(pomo_mode(&v.pomo) == POMO_MODE_IDLE, "idle mode");
    CHECK(pomo_action_for(&v.pomo, POMO_INPUT_PUSH) == POMO_ACT_START, "push starts");
    CHECK(!v.has_weather, "weather null");
}

static void test_view_single_host_paused(void)
{
    knob_view_t v;
    if (!view("view_single_host_paused.json", &v)) return;
    CHECK(knob_view_mood(&v) == BOT_WORKING, "working");
    CHECK(strcmp(v.source, "studio") == 0 && v.lead[0] == 0 && v.hosts == 0 && v.lead_color[0] == 0, "source %s, no lead",
          v.source);
    CHECK(strcmp(v.tool, "claude") == 0, "tool");
    CHECK(v.has_pomo && !v.pomo_counting && v.ends_at == 0, "paused: no ends_at");
    CHECK(v.pomo.phase == POMO_PHASE_FOCUS && v.pomo.paused && v.pomo.remaining_sec == 1400 && v.pomo.planned_sec == 1500,
          "paused remaining %d", v.pomo.remaining_sec);
    CHECK(pomo_mode(&v.pomo) == POMO_MODE_PAUSED && pomo_action_for(&v.pomo, POMO_INPUT_PUSH) == POMO_ACT_RESUME,
          "push resumes");
    CHECK(v.has_weather && v.weather.stale && strcmp(v.weather.condition, "clear") == 0, "stale weather");
    CHECK(v.weather.has_temp && fabsf(v.weather.temp_c + 3.0f) < 1e-4f, "temp %.2f", (double)v.weather.temp_c);
    CHECK(!v.weather.has_night && v.weather.rise_min == -1 && v.weather.set_min == -1, "null sun times: night unknown");
}

static void test_view_quiet(void)
{
    knob_view_t v;
    if (!view("view_quiet.json", &v)) return;
    CHECK(v.quiet, "quiet on");
    CHECK(knob_view_mood(&v) == BOT_IDLE && !v.has_pomo && !v.has_weather && !v.has_np, "idle, pomo and weather off");
    CHECK(v.has_brightness && v.level == 255 && !v.bright_night, "brightness");
    const char *others[] = {"view_full.json", "view_minimal.json", "view_nowplaying_none.json", "view_single_host_paused.json"};
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++)
        if (view(others[i], &v)) CHECK(!v.quiet, "%s: quiet absent = off", others[i]);
}

static void test_view_caps_limited(void)
{
    char *j = load("view_caps_limited.json");
    cJSON *f = j ? cJSON_Parse(j) : NULL;
    CHECK(f && !cJSON_GetObjectItemCaseSensitive(f, "pomo") && !cJSON_GetObjectItemCaseSensitive(f, "nowplaying"), "fixture leaves pomo and nowplaying out");
    cJSON_Delete(f);
    free(j);
    knob_view_t v;
    if (!view("view_caps_limited.json", &v)) return;
    CHECK(v.v == 1 && v.has_mood && knob_view_mood(&v) == BOT_WAITING && strcmp(v.lead, "STUDIO") == 0, "mood");
    CHECK(!v.has_pomo && !v.pomo_counting, "absent pomo = null");
    CHECK(v.has_weather && strcmp(v.weather.condition, "rain") == 0, "weather");
    CHECK(!v.has_np && v.has_brightness && v.diag_live_until == 1782043500LL, "no nowplaying, brightness, live");
}

static char *edited_view(const char *name, const char *drop, int v)
{
    char *j = load(name);
    cJSON *f = j ? cJSON_Parse(j) : NULL;
    free(j);
    if (!f) return NULL;
    if (drop) cJSON_DeleteItemFromObjectCaseSensitive(f, drop);
    if (v) cJSON_ReplaceItemInObjectCaseSensitive(f, "v", cJSON_CreateNumber(v));
    char *out = cJSON_PrintUnformatted(f);
    cJSON_Delete(f);
    return out;
}

static void test_view_without_blocks(void)
{
    const char *drop[] = {"mood", "pomo", "weather"};
    for (size_t i = 0; i < sizeof drop / sizeof drop[0]; i++) {
        char *j = edited_view("view_full.json", drop[i], 0);
        knob_view_t v;
        CHECK(j && knob_view_parse(j, &v), "view_full without %s parses", drop[i]);
        CHECK(v.has_mood == (i != 0) && v.has_pomo == (i != 1) && v.has_weather == (i != 2) && v.has_np,
              "view_full without %s: only that block off", drop[i]);
        if (i == 0) CHECK(knob_view_mood(&v) == BOT_IDLE && v.lead[0] == 0, "no mood: idle, no host");
        free(j);
    }
}

static void test_view_major_99(void)
{
    knob_view_t v;
    if (!view("view_full.json", &v)) return;
    knob_view_t keep = v;
    char *j = edited_view("view_minimal.json", NULL, 99);
    int major = 0;
    CHECK(j && knob_view_read(j, &v, &major) == KNOB_VIEW_TOO_NEW && major == 99, "v 99: too new");
    CHECK(memcmp(&v, &keep, sizeof v) == 0, "v 99: view_full kept");
    free(j);
    j = edited_view("view_minimal.json", NULL, 1);
    CHECK(j && knob_view_read(j, &v, NULL) == KNOB_VIEW_OK && knob_view_mood(&v) == BOT_IDLE, "v 1 again: applied");
    free(j);
}

static bool reply(const char *name, dev_checkin_result_t *r)
{
    char *j = load(name);
    dev_checkin_parse(j, r);
    free(j);
    CHECK(r->ok, "%s parses", name);
    return r->ok;
}

static void test_checkin_replies(void)
{
    dev_checkin_result_t r;
    if (reply("checkin_reply_current.json", &r)) {
        CHECK(r.config_version == 1 && !r.config && !r.has_new_token && !r.has_ota && r.diag_live_until == 0 &&
                  !r.has_coredump_wanted && !r.has_coredump_ack,
              "current: config_version only");
    }
    dev_checkin_result_free(&r);

    if (reply("checkin_reply_config.json", &r)) {
        CHECK(r.config_version == 2 && r.config && !r.config_too_deep, "stale: config included");
        knob_settings_t ks;
        CHECK(knob_settings_parse(r.config, &ks), "config parses");
        CHECK(ks.diagnostics == KS_DIAG_FULL && ks.n_pages == 4 && !ks.pages[3].on, "diagnostics full, nowplaying off");
        CHECK(ks.quiet_calm && ks.quiet_dim == 20, "quiet defaults");
        CHECK(strlen(r.config) <= CFG_SETTINGS_MAX, "config fits the store");
    }
    dev_checkin_result_free(&r);

    if (reply("checkin_reply_coredump.json", &r)) {
        CHECK(r.diag_live_until == 1782043500LL, "live until");
        CHECK(r.has_coredump_wanted && r.coredump_wanted == 0x37315f06u && !r.has_coredump_ack, "coredump_wanted");
    }
    dev_checkin_result_free(&r);

    if (reply("checkin_reply_ota.json", &r)) {
        CHECK(r.has_coredump_ack && r.coredump_ack == 0x37315f06u && !r.has_coredump_wanted, "coredump_ack");
        CHECK(r.has_ota, "ota offer accepted");
        CHECK(r.ota.attempt == 3 && !r.ota.is_auto && !r.ota.retry && r.ota.size == 71680, "offer numbers");
        CHECK(strcmp(r.ota.version, "0.9.14") == 0 && strcmp(r.ota.build, "42f7f65e") == 0 &&
                  strcmp(r.ota.sha256, "de7e409603f8e06e88ed25982476a271aeb0cc76b5f25a6d98f931e503a4d95c") == 0,
              "offer strings %s %s", r.ota.version, r.ota.build);
    }
    dev_checkin_result_free(&r);

    dev_checkin_result_t old;
    if (reply("checkin_reply_caps.json", &r) && reply("checkin_reply_current.json", &old)) {
        CHECK(r.config_version == 1 && !r.config && !r.has_new_token && !r.has_ota && r.diag_live_until == 0 &&
                  !r.has_coredump_wanted && !r.has_coredump_ack,
              "caps_ack: config_version only");
        CHECK(memcmp(&r, &old, sizeof r) == 0, "caps_ack ignored: same result as an old server's reply");
    }
    dev_checkin_result_free(&r);
    dev_checkin_result_free(&old);

    if (reply("checkin_reply_rotation.json", &r)) {
        CHECK(r.config_version == 1 && r.has_new_token, "rotation: new_token");
        CHECK(strncmp(r.new_token, "ekd_", 4) == 0 && strlen(r.new_token) == 47, "device token shape, %zu chars",
              strlen(r.new_token));
    }
    dev_checkin_result_free(&r);
}

static bool config(const char *name, uint32_t *ver, knob_settings_t *ks)
{
    dev_checkin_result_t r;
    bool ok = reply(name, &r) && r.config && knob_settings_parse(r.config, ks);
    CHECK(!r.config || strlen(r.config) <= CFG_SETTINGS_MAX, "%s: %zu B config over the %d B store", name,
          r.config ? strlen(r.config) : 0, CFG_SETTINGS_MAX);
    *ver = r.config_version;
    dev_checkin_result_free(&r);
    CHECK(ok, "%s: settings parse", name);
    return ok;
}

static void test_config_default(void)
{
    uint32_t ver;
    knob_settings_t ks, d;
    if (!config("config_default.json", &ver, &ks)) return;
    CHECK(ver == 1, "version");
    CHECK(ks.follow_ember && ks.level == 153 && ks.floor == 10 && ks.startup == 153, "brightness");
    CHECK(ks.n_pages == 4 && strcmp(ks.pages[3].id, "nowplaying") == 0 && !ks.pages[3].on, "nowplaying off");
    CHECK(strcmp(ks.home, "bot") == 0 && ks.poll_ms == 2000, "home, poll");
    CHECK(ks.sleepy_after_s == 300 && ks.demo_hold_s == 20 && ks.source_label && ks.working_ring, "bot");
    CHECK(ks.diagnostics == KS_DIAG_OFF && ks.stats_interval_s == 60 && ks.live_interval_s == 5, "diagnostics");
    CHECK(ks.fast_link, "fast link");
    CHECK(ks.quiet_calm && ks.quiet_dim == 20, "quiet defaults");

    knob_settings_defaults(&d);
    const char *ids[PAGES_N];
    int nk = pages_ids(ids, PAGES_N), a[PAGES_N], b[PAGES_N], ha, hb;
    int na = knob_settings_page_order(&ks, ids, nk, a, &ha);
    int nb = knob_settings_page_order(&d, ids, nk, b, &hb);
    CHECK(na == nb && ha == hb && memcmp(a, b, sizeof(int) * (size_t)na) == 0, "server default pages == firmware default");
    d.n_pages = ks.n_pages;
    memcpy(d.pages, ks.pages, sizeof d.pages);
    CHECK(memcmp(&d, &ks, sizeof d) == 0, "server defaults == firmware defaults apart from the page list");
}

static void test_config_custom(void)
{
    uint32_t ver;
    knob_settings_t ks;
    if (!config("config_custom.json", &ver, &ks)) return;
    CHECK(ver == 2, "version");
    CHECK(!ks.follow_ember && ks.level == 200 && ks.floor == 20 && ks.startup == 120, "brightness %d/%d/%d", ks.level,
          ks.floor, ks.startup);
    CHECK(ks.n_pages == 5, "pages %d", ks.n_pages);
    CHECK(strcmp(ks.pages[4].id, "future-page") == 0 && !ks.pages[4].on, "unknown page id kept");
    CHECK(strcmp(ks.pages[0].id, "pomodoro") == 0 && strcmp(ks.pages[3].id, "weather") == 0 && !ks.pages[3].on,
          "page order and weather off");
    CHECK(strcmp(ks.home, "pomodoro") == 0 && ks.poll_ms == 3000, "home %s poll %d", ks.home, ks.poll_ms);
    CHECK(ks.sleepy_after_s == 600 && ks.demo_hold_s == 30 && !ks.source_label && !ks.working_ring, "bot");
    CHECK(ks.diagnostics == KS_DIAG_BASIC && ks.stats_interval_s == 120 && ks.live_interval_s == 2, "diagnostics");
    CHECK(!ks.fast_link, "fast link off");
    CHECK(!ks.quiet_calm && ks.quiet_dim == 5, "quiet calm off, dim 5");
    const char *ids[PAGES_N];
    int order[PAGES_N], home;
    int n = knob_settings_page_order(&ks, ids, pages_ids(ids, PAGES_N), order, &home);
    CHECK(n == 3 && order[0] == 1 && order[1] == 0 && order[2] == 3 && home == 0, "shown: pomodoro, bot, nowplaying");
}

static void test_pomodoro_action(void)
{
    char *j = load("pomodoro_action.json");
    pomo_state_t s;
    bool ok = j && pomo_legacy_parse(j, &s);
    free(j);
    CHECK(ok, "action reply parses");
    if (!ok) return;
    CHECK(s.phase == POMO_PHASE_SHORT_BREAK && s.running && s.paused && s.remaining_sec == 300 && s.planned_sec == 300 &&
              s.round == 0,
          "paused short break");
    CHECK(pomo_mode(&s) == POMO_MODE_PAUSED && pomo_action_for(&s, POMO_INPUT_PUSH) == POMO_ACT_RESUME, "push resumes");
}

static const cJSON *at(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k); }
static double num(const cJSON *o, const char *k) { return cJSON_IsNumber(at(o, k)) ? at(o, k)->valuedouble : 0; }
static const char *str(const cJSON *o, const char *k) { return cJSON_GetStringValue(at(o, k)); }

static const char *const NOT_YET_SENT[] = {NULL};
#define N_NOT_YET_SENT ((int)(sizeof NOT_YET_SENT / sizeof NOT_YET_SENT[0]))
static int not_yet_sent_used[N_NOT_YET_SENT];

static int not_yet_sent(const char *path)
{
    for (int i = 0; i < N_NOT_YET_SENT; i++)
        if (NOT_YET_SENT[i] && strcmp(NOT_YET_SENT[i], path) == 0) return i;
    return -1;
}

static void body_diff(const char *name, const cJSON *want, const cJSON *got, const char *path)
{
    const cJSON *e;
    char p[256];
    cJSON_ArrayForEach(e, want)
    {
        snprintf(p, sizeof p, "%s.%s", path, e->string);
        const cJSON *g = at(got, e->string);
        int n = not_yet_sent(p);
        if (n >= 0) {
            CHECK(!g, "%s: firmware sends %s, drop it from NOT_YET_SENT", name, p);
            not_yet_sent_used[n]++;
        } else if (!g) {
            CHECK(false, "%s: firmware does not send %s (list it in NOT_YET_SENT until it does)", name, p);
        } else if (cJSON_IsObject(e) && cJSON_IsObject(g)) {
            body_diff(name, e, g, p);
        } else {
            CHECK(cJSON_Compare(e, g, true), "%s: %s differs", name, p);
        }
    }
    cJSON_ArrayForEach(e, got)
    {
        snprintf(p, sizeof p, "%s.%s", path, e->string);
        CHECK(at(want, e->string) != NULL, "%s: firmware sends %s, not in the fixture", name, p);
    }
}

static void same_body(const char *name, const char *body)
{
    char *j = load(name);
    cJSON *want = j ? cJSON_Parse(j) : NULL, *got = cJSON_Parse(body);
    free(j);
    CHECK(cJSON_IsObject(want) && cJSON_IsObject(got), "%s: fixture and firmware body parse: %s", name, body);
    if (cJSON_IsObject(want) && cJSON_IsObject(got)) body_diff(name, want, got, "");
    cJSON_Delete(want);
    cJSON_Delete(got);
}

static void test_not_yet_sent_used(void)
{
    for (int i = 0; i < N_NOT_YET_SENT; i++)
        if (NOT_YET_SENT[i]) CHECK(not_yet_sent_used[i] > 0, "NOT_YET_SENT %s is in no checkin_req fixture, drop it", NOT_YET_SENT[i]);
}

static void test_checkin_req_minimal(void)
{
    char *j = load("checkin_req_minimal.json");
    cJSON *f = cJSON_Parse(j);
    free(j);
    CHECK(f != NULL, "checkin_req_minimal parses");
    if (!f) return;
    dev_checkin_t c = {.fw = str(f, "fw"), .ip = str(f, "ip"), .rssi = (int)num(f, "rssi"),
                       .heap_internal_free = (uint32_t)num(f, "heap_internal_free"),
                       .heap_internal_largest = (uint32_t)num(f, "heap_internal_largest"),
                       .uptime_s = (int64_t)num(f, "uptime_s"), .config_version = (uint32_t)num(f, "config_version")};
    char out[2048];
    CHECK(dev_checkin_body(&c, NULL, out, sizeof out) > 0, "minimal body fits");
    same_body("checkin_req_minimal.json", out);
    cJSON_Delete(f);
}

static void test_checkin_req_caps(void)
{
    char *j = load("checkin_req_caps.json");
    cJSON *f = cJSON_Parse(j);
    free(j);
    CHECK(f != NULL, "checkin_req_caps parses");
    if (!f) return;
    dev_caps_t caps;
    const char *ids[PAGES_N];
    knob_caps(&caps, ids);
    dev_checkin_t c = {.caps = &caps, .fw = str(f, "fw"), .ip = str(f, "ip"), .rssi = (int)num(f, "rssi"),
                       .heap_internal_free = (uint32_t)num(f, "heap_internal_free"),
                       .heap_internal_largest = (uint32_t)num(f, "heap_internal_largest"),
                       .uptime_s = (int64_t)num(f, "uptime_s"), .config_version = (uint32_t)num(f, "config_version")};
    char out[DEV_CHECKIN_BODY_MAX];
    CHECK(dev_checkin_body(&c, NULL, out, sizeof out) > 0, "caps body fits");
    same_body("checkin_req_caps.json", out);
    cJSON_Delete(f);
}

static void test_checkin_req_full(void)
{
    char *j = load("checkin_req_full.json");
    cJSON *f = cJSON_Parse(j);
    free(j);
    CHECK(f != NULL, "checkin_req_full parses");
    if (!f) return;

    const cJSON *dj = at(f, "diag"), *cj = at(dj, "crash");
    dev_diag_t d = {.reset_reason = str(dj, "reset_reason"), .boots = (uint32_t)num(dj, "boots"),
                    .heap_internal_min = (uint32_t)num(dj, "heap_internal_min"),
                    .heap_largest_min = (uint32_t)num(dj, "heap_largest_min"), .has_crash = cj != NULL,
                    .crash_reason = str(cj, "reason"), .crash_size = (uint32_t)num(cj, "size")};
    const char *pc = str(cj, "pc");
    d.crash_pc = pc ? (uint32_t)strtoul(pc, NULL, 16) : 0;
    d.has_crash_id = cd_id_parse(str(cj, "id"), &d.crash_id);
    snprintf(d.crash_elf, sizeof d.crash_elf, "%s", str(cj, "elf") ? str(cj, "elf") : "");
    snprintf(d.crash_task, sizeof d.crash_task, "%s", str(cj, "task") ? str(cj, "task") : "");
    const cJSON *t;
    cJSON_ArrayForEach(t, at(dj, "stack_free"))
    {
        if (d.n_tasks == DEV_DIAG_MAX_TASKS) break;
        snprintf(d.tasks[d.n_tasks].name, sizeof d.tasks[0].name, "%s", t->string);
        d.tasks[d.n_tasks++].stack_free = (uint32_t)t->valuedouble;
    }

    const cJSON *oj = at(f, "ota"), *lj = at(oj, "last");
    const char *res = str(lj, "result");
    ota_last_t last = {.attempt = (uint32_t)num(lj, "attempt"),
                       .result = !res ? OTA_RES_NONE
                                 : strcmp(res, "ok") == 0 ? OTA_RES_OK
                                 : strcmp(res, "failed") == 0 ? OTA_RES_FAILED
                                 : strcmp(res, "rolled_back") == 0 ? OTA_RES_ROLLED_BACK
                                                                   : OTA_RES_NONE};
    snprintf(last.error, sizeof last.error, "%s", str(lj, "error") ? str(lj, "error") : "");
    snprintf(last.version, sizeof last.version, "%s", str(lj, "version") ? str(lj, "version") : "");
    ota_report_t ota = {.rollback = cJSON_IsTrue(at(oj, "rollback")), .slot = (int)num(oj, "slot"),
                        .image = str(oj, "image"), .phase = str(oj, "phase"), .last = &last};

    const cJSON *wj = at(f, "wifi");
    dev_wifi_t w = {.channel = (int)num(wj, "channel"), .disconnects = (uint32_t)num(wj, "disconnects"),
                    .last_reason = (int)num(wj, "last_reason"), .has_rssi_min = at(wj, "rssi_min") != NULL,
                    .rssi_min = (int)num(wj, "rssi_min")};
    unsigned m[6];
    const char *bssid = str(wj, "bssid");
    w.has_bssid = bssid && sscanf(bssid, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6;
    for (int i = 0; w.has_bssid && i < 6; i++) w.bssid[i] = (uint8_t)m[i];

    const cJSON *sj = at(f, "stats");
    dev_stats_t st = {.period_ms = (uint32_t)num(sj, "period_ms"), .has_heap_min = at(sj, "heap_internal_min") != NULL,
                      .heap_internal_min = (uint32_t)num(sj, "heap_internal_min"),
                      .has_psram = at(sj, "psram_free") != NULL, .psram_free = (uint32_t)num(sj, "psram_free"),
                      .psram_min = (uint32_t)num(sj, "psram_min"), .psram_largest = (uint32_t)num(sj, "psram_largest"),
                      .has_temp = at(sj, "temp_c") != NULL, .temp_c = (float)num(sj, "temp_c"),
                      .reset_reason = str(sj, "reset_reason"), .has_req = at(sj, "req_ok") != NULL,
                      .req_ok = (uint32_t)num(sj, "req_ok"), .req_fail = (uint32_t)num(sj, "req_fail"),
                      .req_ms_avg = (float)num(sj, "req_ms_avg"), .req_ms_max = (uint32_t)num(sj, "req_ms_max"),
                      .has_frames = at(sj, "fps") != NULL, .fps = (float)num(sj, "fps"),
                      .frame_ms_avg = (float)num(sj, "frame_ms_avg"), .frame_ms_max = (uint32_t)num(sj, "frame_ms_max")};
    cJSON_ArrayForEach(t, at(sj, "cpu_pct"))
    {
        if (st.n_cpu < DEV_STATS_MAX_CPU) st.cpu_pct[st.n_cpu++] = (float)t->valuedouble;
    }

    dev_checkin_t c = {.fw = str(f, "fw"), .fw_build = str(f, "fw_build"), .ota = &ota, .ip = str(f, "ip"),
                       .rssi = (int)num(f, "rssi"), .heap_internal_free = (uint32_t)num(f, "heap_internal_free"),
                       .heap_internal_largest = (uint32_t)num(f, "heap_internal_largest"),
                       .uptime_s = (int64_t)num(f, "uptime_s"), .config_version = (uint32_t)num(f, "config_version"),
                       .link_mhz = (int)num(f, "link_mhz"), .link_fallback = cJSON_IsTrue(at(f, "link_fallback")),
                       .wifi = &w, .diag = &d};
    char out[2048];
    CHECK(dev_checkin_body(&c, &st, out, sizeof out) > 0, "full body fits");
    same_body("checkin_req_full.json", out);
    cJSON_Delete(f);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <fixture dir>\n", argv[0]);
        return 2;
    }
    dir = argv[1];
    test_all_covered();
    test_view_full();
    test_view_minimal();
    test_view_nowplaying_none();
    test_view_single_host_paused();
    test_view_quiet();
    test_view_caps_limited();
    test_view_without_blocks();
    test_view_major_99();
    test_checkin_replies();
    test_config_default();
    test_config_custom();
    test_pomodoro_action();
    test_checkin_req_minimal();
    test_checkin_req_full();
    test_checkin_req_caps();
    test_not_yet_sent_used();
    if (failures) {
        printf("fixtures: %d failure(s)\n", failures);
        return 1;
    }
    printf("fixtures: all tests passed\n");
    return 0;
}
