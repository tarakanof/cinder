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
#include "wx_temp.h"

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
    "view_faces.json",         "view_faces_met.json",         "view_faces_polar_dry.json",
    "view_np_cover.json",
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
    CHECK(v.weather.observed_at == 1782041400LL, "observed_at %lld", v.weather.observed_at);
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
    CHECK(v.weather.observed_at == 0, "no observed_at: unknown");
    char lb[16];
    wx_age_label(&v.weather, lb, sizeof lb);
    CHECK(strcmp(lb, "stale") == 0, "no observed_at: the label works as before, got '%s'", lb);
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
    CHECK(v.weather.observed_at == 1782041400LL, "observed_at %lld", v.weather.observed_at);
    CHECK(!v.has_np && v.has_brightness && v.diag_live_until == 1782043500LL, "no nowplaying, brightness, live");
}

static void test_view_faces(void)
{
    knob_view_t v;
    if (!view("view_faces.json", &v)) return;
    const wx_obs_t *o = &v.weather;
    CHECK(v.has_weather && o->has_temp && fabsf(o->temp_c - 15.4f) < 1e-4f && o->observed_at == 1782043200LL, "legacy keys");
    CHECK(o->has_cloud && o->cloud == 94 && o->has_uv && o->uv == 3, "now: cloud %d uv %d", o->cloud, o->uv);
    CHECK(o->has_today && o->min_c == 9 && o->max_c == 15, "today %d..%d", o->min_c, o->max_c);
    CHECK(o->has_moon && o->moon_phase == 24 && o->moon_illum == 46 && o->moon_up == 1, "moon");
    wx_temp_t t;
    wx_temp_from(o, &t);
    CHECK(t.have && t.value == 15 && t.has_range && t.lo == 9 && t.hi == 15 && t.frac == 1, "gauge %d..%d", t.lo, t.hi);
    CHECK(!t.night && t.cloud_band == 4 && t.hero == WX_HERO_NONE && !t.stars, "overcast day");
    CHECK(strcmp(t.sub, "Sunglasses on") == 0 && t.sub_rgb == 0xF0E641, "UV 3 by day: '%s'", t.sub);
    CHECK(o->has_now_rain && o->now_rain == 1 && o->has_pop && o->pop == 83 && o->has_utc_offset && o->utc_offset == 3600,
          "now.rain, today.pop, utc_offset");
    CHECK(o->has_rain && o->rain_until == 1782049500LL && o->rain_n == 2 && o->rain_spell[0].start == 1782042300LL &&
              o->rain_spell[0].end == 1782049500LL && o->rain_spell[0].intensity == 2 && o->rain_spell[0].kind == 0 &&
              o->rain_spell[1].intensity == 1 && o->rain_spell[1].kind == 1,
          "rain block");
    CHECK(t.rain.on && t.rain.kind == WX_PRECIP_RAIN && t.rain.intensity == 2 && t.rain.has_pop && t.rain.pop == 83 &&
              !strcmp(t.rain.sub, "until 14:45"),
          "raining now: the Rain now face, the moderate spell's peak, 83 %%, '%s'", t.rain.sub);
    wx_obs_t at = *o;
    at.has_obs_age = true;
    wx_cast_t c;
    wx_cast_from(&at, NULL, &c);
    CHECK(c.have && c.wet && c.has_count && c.hours && c.count == 4 && c.variant == 2 && !strcmp(c.sub, "#5C9CE0 rain# until 14:45"),
          "forecast while wet: 2 h to the stop, '%s'", c.sub);
    CHECK(c.narc == 2 && c.arc[0].a == 60 && c.arc[0].n == 105 && c.arc[1].a == 345 && c.arc[1].n == 60 && c.arc[1].intensity == 1,
          "forecast arcs from now, then the light snow spell");
    CHECK(o->has_wind && o->wind_kmh == 21 && o->has_wind_dir && o->wind_dir == 225, "now: wind %d from %d", o->wind_kmh, o->wind_dir);
    wx_wind_t wd;
    wx_wind_from(o, NULL, &wd);
    CHECK(wd.have && wd.kmh == 21 && wd.level == 1 && !strcmp(wd.sub, "#C9D25A Moderate breeze# \xE2\x80\xA2 SW"), "wind: '%s'", wd.sub);
    CHECK(t.face == WXF_RAIN, "21 km/h while wet: Rain now");

    if (!view("view_faces_met.json", &v)) return;
    CHECK(o->has_cloud && o->cloud == 70 && o->has_uv && o->uv == 3, "MET now");
    CHECK(o->has_today && o->min_c == 14 && o->max_c == 17, "MET today");
    wx_temp_from(o, &t);
    CHECK(t.value == 14 && t.lo == 14 && t.hi == 17 && t.frac == 0 && t.cloud_band == 3 && t.hero == WX_HERO_NONE,
          "MET gauge and clouds");
    CHECK(o->has_now_rain && o->now_rain == 0 && !o->has_pop && o->has_rain && o->rain_until == 0 && o->rain_n == 2 && !t.rain.on,
          "MET dry now, rain later: Temperature face");
    CHECK(t.face == WXF_TEMP, "rain in 2 h is not the auto forecast");
    CHECK(o->has_wind && o->wind_kmh == 15 && o->has_wind_dir && o->wind_dir == 200, "MET wind without gusts");
    wx_wind_from(o, NULL, &wd);
    CHECK(wd.have && wd.level == 1 && !strcmp(wd.sub, "#8ACF7A Gentle breeze# \xE2\x80\xA2 S"), "MET wind: '%s'", wd.sub);
    at = *o;
    at.has_obs_age = true;
    wx_cast_from(&at, NULL, &c);
    CHECK(!c.wet && c.has_count && c.hours && c.count == 4 && c.variant == 2 && !strcmp(c.label, "14") &&
              !strcmp(c.sub, "#5C9CE0 rain# at 14:00"),
          "MET forecast: 2 h to the moderate spell, '%s'", c.sub);

    if (!view("view_faces_polar_dry.json", &v)) return;
    CHECK(o->has_moon && o->moon_phase == 65 && o->moon_illum == 80 && o->moon_up == 1, "polar moon without set");
    wx_temp_from(o, &t);
    CHECK(o->has_rain && o->rain_n == 0 && o->has_now_rain && o->now_rain == 0 && !t.rain.on,
          "condition rain but dry steps and no spells: Temperature face");
    at = *o;
    at.has_obs_age = true;
    wx_cast_from(&at, NULL, &c);
    char val[12];
    uint32_t rgb = 0;
    const char *unit = wx_cast_value(&c, wx_cast_count_n(&c), val, sizeof val, &rgb);
    CHECK(c.have && c.dry && !c.has_count && c.variant == 1 && !strcmp(c.sub, "No rain for 12 h") && unit &&
              !strcmp(unit, "h") && !strcmp(val, "12+") && rgb == WX_CAST_DRY_RGB,
          "polar forecast: '%s %s', '%s'", val, unit ? unit : "-", c.sub);
    char *j = load("view_faces.json");
    cJSON *f = j ? cJSON_Parse(j) : NULL;
    free(j);
    cJSON *w = cJSON_GetObjectItemCaseSensitive(f, "weather");
    cJSON_DeleteItemFromObjectCaseSensitive(w, "today");
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "now"), "cloud");
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "moon"), "illum", cJSON_CreateString("46"));
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "moon"), "up");
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "now"), "uv", cJSON_CreateNumber(-1));
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "now"), "rain", cJSON_CreateNumber(4));
    cJSON_ReplaceItemInObjectCaseSensitive(w, "utc_offset", cJSON_CreateString("3600"));
    cJSON *rain = cJSON_GetObjectItemCaseSensitive(w, "rain");
    cJSON_ReplaceItemInObjectCaseSensitive(rain, "until", cJSON_CreateNumber(-5));
    cJSON *spells = cJSON_GetObjectItemCaseSensitive(rain, "spells");
    cJSON_ReplaceItemInArray(cJSON_GetArrayItem(spells, 0), 2, cJSON_CreateNumber(7));
    cJSON_ReplaceItemInArray(cJSON_GetArrayItem(spells, 1), 0, cJSON_CreateNumber(1782063900));
    j = cJSON_PrintUnformatted(f);
    cJSON_Delete(f);
    bool ok = j && knob_view_parse(j, &v);
    free(j);
    CHECK(ok && !o->has_today && !o->has_cloud && !o->has_uv && !o->has_moon && o->moon_up == -1, "missing or bad keys degrade");
    CHECK(!o->has_now_rain && !o->has_pop && !o->has_utc_offset && o->has_rain && o->rain_until == 0 && o->rain_n == 0,
          "bad rain keys are unknown; a bad spell is skipped");
    wx_temp_from(o, &t);
    CHECK(!t.rain.on, "a rain block without a valid step or spell: dry, whatever the condition");
    CHECK(t.have && !t.has_range && t.lo == WX_SCALE_LO && t.cloud_band == 4 && strcmp(t.sub, "Rain") == 0,
          "without detail: fixed scale, cloud from the condition, sky word");
}

static cJSON *faces_weather(cJSON **root)
{
    char *j = load("view_faces.json");
    *root = j ? cJSON_Parse(j) : NULL;
    free(j);
    return cJSON_GetObjectItemCaseSensitive(*root, "weather");
}

static bool faces_parse(cJSON *root, knob_view_t *v)
{
    char *j = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    bool ok = j && knob_view_parse(j, v);
    free(j);
    return ok;
}

static cJSON *spell(double start, double end, double in, double kind)
{
    double a[4] = {start, end, in, kind};
    return cJSON_CreateDoubleArray(a, 4);
}

static void test_view_rain_keys(void)
{
    static const struct {
        const char *what;
        double now_rain, pop, until;
        bool pop_str;
    } BAD[] = {{"now.rain 0.49, pop 101, until x.5", 0.49, 101, 1782049500.5, false},
               {"now.rain 0.5, pop \"83\"", 0.5, 83, 1782049500.5, true},
               {"now.rain 1.5, pop 83.5", 1.5, 83.5, 1782049500.5, false}};
    knob_view_t v;
    const wx_obs_t *o = &v.weather;
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        cJSON *root, *w = faces_weather(&root);
        cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "now"), "rain", cJSON_CreateNumber(BAD[i].now_rain));
        cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "today"), "pop",
                                               BAD[i].pop_str ? cJSON_CreateString("83") : cJSON_CreateNumber(BAD[i].pop));
        cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "rain"), "until", cJSON_CreateNumber(BAD[i].until));
        bool ok = faces_parse(root, &v);
        CHECK(ok && !o->has_now_rain && !o->has_pop && o->rain_until == 0 && o->has_today, "%s: unknown", BAD[i].what);
    }
    static const double OFFSET[] = {3600.5, -1800.5};
    for (size_t i = 0; i < sizeof OFFSET / sizeof OFFSET[0]; i++) {
        cJSON *root, *w = faces_weather(&root);
        cJSON_ReplaceItemInObjectCaseSensitive(w, "utc_offset", cJSON_CreateNumber(OFFSET[i]));
        bool ok = faces_parse(root, &v);
        CHECK(ok && !o->has_utc_offset && o->has_now_rain, "utc_offset %.1f: not an integer, unknown", OFFSET[i]);
    }
    static const struct {
        double kmh, dir;
        bool str, has_kmh, has_dir;
        int want_kmh, want_dir;
    } WIND[] = {{20.6, 359.4, false, true, true, 21, 359}, {0, 360, false, true, true, 0, 0}, {-1, -1, false, false, false, 0, 0},
                {1000, 360.6, false, false, false, 0, 0}, {999, 0, false, true, true, 999, 0}, {21, 225, true, false, false, 0, 0}};
    for (size_t i = 0; i < sizeof WIND / sizeof WIND[0]; i++) {
        cJSON *root, *w = faces_weather(&root), *now = cJSON_GetObjectItemCaseSensitive(w, "now");
        cJSON_ReplaceItemInObjectCaseSensitive(now, "wind_kmh", WIND[i].str ? cJSON_CreateString("21") : cJSON_CreateNumber(WIND[i].kmh));
        cJSON_ReplaceItemInObjectCaseSensitive(now, "wind_dir", WIND[i].str ? cJSON_CreateString("225") : cJSON_CreateNumber(WIND[i].dir));
        bool ok = faces_parse(root, &v);
        CHECK(ok && o->has_wind == WIND[i].has_kmh && o->has_wind_dir == WIND[i].has_dir &&
                  (!o->has_wind || o->wind_kmh == WIND[i].want_kmh) && (!o->has_wind_dir || o->wind_dir == WIND[i].want_dir),
              "wind %.1f from %.1f: %d %d, %d %d", WIND[i].kmh, WIND[i].dir, o->has_wind, o->wind_kmh, o->has_wind_dir, o->wind_dir);
    }
    {
        cJSON *root, *w = faces_weather(&root);
        cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "now"), "wind_kmh");
        cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "now"), "wind_dir");
        bool ok = faces_parse(root, &v);
        CHECK(ok && !o->has_wind && !o->has_wind_dir, "no wind keys: unknown");
    }

    cJSON *root, *w = faces_weather(&root);
    cJSON *sp = cJSON_CreateArray();
    cJSON_AddItemToArray(sp, spell(1782049500, 1782049500, 2, 0));
    cJSON_AddItemToArray(sp, spell(1782042300, 1782049500, 2, 0));
    cJSON_AddItemToArray(sp, spell(1782050000.5, 1782051000, 1, 0));
    cJSON_AddItemToArray(sp, spell(1782052000, 1782053000, 3, 1));
    cJSON_AddItemToArray(sp, spell(1782054000, 1782055000, 2.5, 0));
    cJSON_AddItemToArray(sp, spell(1782056000, 1782057000, 1, 0));
    cJSON_AddItemToArray(sp, spell(1782058000, 1782059000, 2, 1));
    cJSON_AddItemToArray(sp, spell(1782060000, 1782061000, 3, 0));
    cJSON_AddItemToArray(sp, spell(1782062000, 1782063000, 1, 1));
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(w, "rain"), "spells", sp);
    bool ok = faces_parse(root, &v);
    CHECK(ok && o->rain_n == WX_SPELLS, "9 spells, 3 bad: the first 4 valid kept, %d", o->rain_n);
    static const long long START[WX_SPELLS] = {1782042300, 1782052000, 1782056000, 1782058000};
    for (int i = 0; ok && i < WX_SPELLS; i++)
        CHECK(o->rain_spell[i].start == START[i], "spell %d starts %lld, want %lld", i, o->rain_spell[i].start, START[i]);
}

static cJSON *day_item(double t, double lo, double hi, const char *cond, const char *code, double pop)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "t", t);
    if (cond) cJSON_AddStringToObject(d, "cond", cond);
    if (code) cJSON_AddStringToObject(d, "code", code);
    if (!isnan(lo)) cJSON_AddNumberToObject(d, "min_c", lo);
    if (!isnan(hi)) cJSON_AddNumberToObject(d, "max_c", hi);
    if (!isnan(pop)) cJSON_AddNumberToObject(d, "pop", pop);
    return d;
}

static void test_view_days(void)
{
    knob_view_t v;
    const wx_obs_t *o = &v.weather;
    wx_days_t d;
    if (!view("view_faces.json", &v)) return;
    CHECK(o->day_n == 3 && o->day[0].t == 1782082800 && o->day[1].t == 1782169200 && o->day[2].t == 1782255600, "days: three, in order");
    CHECK(o->day[0].end == 1782169200 && o->day[1].end == 1782255600 && o->day[2].end == 1782342000,
          "days: each ends at Ember's end, the last one included");
    CHECK(o->day[0].face == WX_OVERCAST && o->day[1].face == WX_RAIN && o->day[2].face == WX_SNOW, "days: clouds, rain, snow");
    CHECK(o->day[0].min_c == 7 && o->day[0].max_c == 16 && o->day[2].min_c == -4 && o->day[2].max_c == 3 && o->day[0].pop == 32 &&
              o->day[1].pop == 19 && o->day[2].pop == 40,
          "days: ranges and chances");
    wx_days_from(o, &d);
    CHECK(d.have && d.n == 3 && d.row[0].wday == 1 && d.row[1].wday == 2 && d.row[2].wday == 3 && d.row[0].icon == WX_ICON_CLOUD &&
              d.row[1].icon == WX_ICON_RAIN && d.row[2].icon == WX_ICON_SNOW && d.row[0].pop == -1 && d.row[1].pop == 19 &&
              d.row[2].pop == 40 && d.amin == -4 && d.amax == 17,
          "Next days: Mon, Tue, Wed on -4..17");
    wx_temp_t t;
    wx_temp_from(o, &t);
    CHECK(t.face != WXF_DAYS && !t.days.on, "never the auto face");

    if (!view("view_faces_met.json", &v)) return;
    CHECK(o->day_n == 3 && o->day[0].face == WX_STORM && o->day[1].face == WX_SNOW && o->day[2].face == WX_STORM &&
              o->day[0].pop == -1 && o->day[1].pop == -1,
          "MET days: worst symbols, no chance");
    wx_days_from(o, &d);
    CHECK(d.n == 3 && d.row[0].wday == 1 && d.row[0].icon == WX_ICON_STORM && d.row[0].pop == -1 && d.amin == 8 && d.amax == 19,
          "MET Next days: Mon storm, no chance");

    if (!view("view_faces_polar_dry.json", &v)) return;
    wx_days_from(o, &d);
    CHECK(d.n == 3 && d.row[0].wday == 3, "polar fixture: from Wednesday (offset +2 h)");

    {
        cJSON *root, *w = faces_weather(&root), *a = cJSON_CreateArray();
        cJSON_AddItemToArray(a, cJSON_CreateNumber(5));
        cJSON_AddItemToArray(a, day_item(1782082800.5, 5, 9, "clear", "0", NAN));
        cJSON_AddItemToArray(a, day_item(1782082800, 9, 5, "clear", "0", NAN));
        cJSON_AddItemToArray(a, day_item(1782082800, NAN, 5, "clear", "0", NAN));
        cJSON_AddItemToArray(a, day_item(1782082800, -100, 5, "clear", "0", NAN));
        cJSON_AddItemToArray(a, day_item(1782082800, 7.6, 16.4, "clear", "0", 50.5));
        cJSON_AddItemToArray(a, day_item(1782082800, 1, 2, "rain", "61", 30));
        cJSON_AddItemToArray(a, day_item(1782169200, -0.4, 98.6, "rain", NULL, 100));
        cJSON *bad_t = day_item(0, 1, 2, "clear", "0", NAN);
        cJSON_ReplaceItemInObjectCaseSensitive(bad_t, "t", cJSON_CreateString("1782255600"));
        cJSON_AddItemToArray(a, bad_t);
        cJSON *str_pop = day_item(1782255600, 1, 2, NULL, NULL, NAN);
        cJSON_AddStringToObject(str_pop, "pop", "50");
        cJSON_AddItemToArray(a, str_pop);
        cJSON_AddItemToArray(a, day_item(1782342000, 1, 2, "snow", "71", 10));
        cJSON_ReplaceItemInObjectCaseSensitive(w, "days", a);
        bool ok = faces_parse(root, &v);
        CHECK(ok && o->day_n == 3, "11 entries, 7 bad, a fourth good one past the three: %d days", o->day_n);
        CHECK(o->day[0].t == 1782082800 && o->day[0].min_c == 8 && o->day[0].max_c == 16 && o->day[0].pop == -1 &&
                  o->day[0].face == WX_CLEAR_DAY,
              "the first good day: rounded, a fractional chance unknown");
        CHECK(o->day[1].t == 1782169200 && o->day[1].min_c == 0 && o->day[1].max_c == 99 && o->day[1].pop == 100 &&
                  o->day[1].face == WX_RAIN,
              "a repeated t skipped; rain by its bucket without a code: %lld %d..%d", o->day[1].t, o->day[1].min_c, o->day[1].max_c);
        CHECK(o->day[2].t == 1782255600 && o->day[2].pop == -1 && o->day[2].face == WX_OVERCAST,
              "a string t skipped; a string chance unknown; no condition: overcast");
    }
    {
        cJSON *root, *w = faces_weather(&root), *a = cJSON_CreateArray();
        cJSON_AddItemToArray(a, day_item(1782082800, 7, 16, "clouds", "3", NAN));
        cJSON_AddItemToArray(a, day_item(1782169200, NAN, 17, "rain", "61", NAN));
        cJSON_AddItemToArray(a, day_item(1782255600, -4, 3, "snow", "71", NAN));
        cJSON_AddItemToArray(a, day_item(1782342000, 1, 2, "snow", "71", NAN));
        cJSON_ReplaceItemInObjectCaseSensitive(w, "days", a);
        bool ok = faces_parse(root, &v);
        CHECK(ok && o->day_n == 3 && o->day[0].end == 1782169200 && o->day[1].t == 1782255600 && o->day[1].end == 1782342000 &&
                  o->day[2].end == 0,
              "a bad middle day keeps its boundary: ends %lld %lld %lld", o->day[0].end, o->day[1].end, o->day[2].end);
    }
    {
        cJSON *root, *w = faces_weather(&root), *a = cJSON_CreateArray();
        cJSON_AddItemToArray(a, day_item(1782082800, 7, 16, "clouds", "3", NAN));
        cJSON_AddItemToArray(a, day_item(1782082800 + 90001, 1, 2, "rain", "61", NAN));
        cJSON *bad = day_item(0, 1, 2, "rain", "61", NAN);
        cJSON_ReplaceItemInObjectCaseSensitive(bad, "t", cJSON_CreateString("x"));
        cJSON_AddItemToArray(a, bad);
        cJSON_AddItemToArray(a, day_item(1782082800 + 90001 + 86400, 1, 2, "rain", "61", NAN));
        cJSON_ReplaceItemInObjectCaseSensitive(w, "days", a);
        bool ok = faces_parse(root, &v);
        CHECK(ok && o->day_n == 3 && o->day[0].end == 0 && o->day[1].end == 0 && o->day[2].end == 0,
              "a next start more than 25 h on, or none readable: end unknown (%lld %lld)", o->day[0].end, o->day[1].end);
        CHECK(o->day[0].t == 1782082800 && o->day[1].t == 1782082800 + 90001, "both kept");
    }
    {
        cJSON *root, *w = faces_weather(&root);
        cJSON_ReplaceItemInObjectCaseSensitive(w, "days", cJSON_CreateObject());
        bool ok = faces_parse(root, &v);
        CHECK(ok && o->day_n == 0, "days not an array: none");
    }
    {
        cJSON *root, *w = faces_weather(&root);
        cJSON_DeleteItemFromObjectCaseSensitive(w, "days");
        bool ok = faces_parse(root, &v);
        CHECK(ok && o->day_n == 0 && o->has_today, "no days: none, the rest parsed");
    }
    {
        cJSON *root, *w = faces_weather(&root);
        cJSON_ReplaceItemInObjectCaseSensitive(w, "provider", cJSON_CreateString("met-no"));
        cJSON *a = cJSON_CreateArray();
        cJSON_AddItemToArray(a, day_item(1782082800, 1, 2, "rain", "partlycloudy_night", NAN));
        cJSON_AddItemToArray(a, day_item(1782169200, 1, 2, "clear", "clearsky_night", NAN));
        cJSON_ReplaceItemInObjectCaseSensitive(w, "days", a);
        bool ok = faces_parse(root, &v);
        CHECK(ok && o->day_n == 2 && o->day[0].face == WX_PARTLY_CLOUDY && o->day[1].face == WX_CLEAR_DAY,
              "a night symbol on a day is the day face: %d %d", o->day[0].face, o->day[1].face);
    }
    const char *names[] = {"view_full.json", "view_caps_limited.json"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (view(names[i], &v)) CHECK(o->day_n == 0, "%s: no days without wx_faces", names[i]);
}

static wx_sun_t sun_now(const wx_obs_t *o)
{
    wx_obs_t c = *o;
    c.has_obs_age = true;
    c.obs_age_s = 0;
    wx_sun_t s;
    wx_sun_from(&c, &s);
    return s;
}

static int days_at(knob_view_t *v, long long now)
{
    wx_days_t d;
    v->weather.observed_at = now;
    v->weather.has_obs_age = true;
    v->weather.obs_age_s = 0;
    v->weather.age_s = 0;
    wx_days_from(&v->weather, &d);
    return d.n;
}

static bool dst_days(knob_view_t *v, const long long t[3], const double end[3], int off)
{
    cJSON *root, *w = faces_weather(&root), *a = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) {
        cJSON *d = day_item((double)t[i], 1, 2, "clear", "0", NAN);
        if (!isnan(end[i])) cJSON_AddNumberToObject(d, "end", end[i]);
        cJSON_AddItemToArray(a, d);
    }
    cJSON_ReplaceItemInObjectCaseSensitive(w, "utc_offset", cJSON_CreateNumber(off));
    cJSON_ReplaceItemInObjectCaseSensitive(w, "days", a);
    return faces_parse(root, v);
}

static void test_view_day_ends(void)
{
    knob_view_t v;
    const wx_obs_t *o = &v.weather;
    static const struct {
        long long t[3], end;
        int off;
        const char *what;
    } DST[] = {
        {{1774566000LL, 1774652400LL, 1774738800LL}, 1774821600LL, 3600, "Belgrade 2026-03-29, 23 h"},
        {{1792706400LL, 1792792800LL, 1792879200LL}, 1792969200LL, 7200, "Belgrade 2026-10-25, 25 h"},
    };
    for (size_t k = 0; k < sizeof DST / sizeof DST[0]; k++) {
        const long long *t = DST[k].t, end = DST[k].end;
        const char *what = DST[k].what;
        const double ENDS[3] = {(double)t[1], (double)t[2], (double)end};
        bool ok = dst_days(&v, t, ENDS, DST[k].off);
        CHECK(ok && o->day_n == 3 && o->day[0].end == t[1] && o->day[1].end == t[2] && o->day[2].end == end,
              "%s: the last day ends at Ember's end: %lld", what, o->day[2].end);
        CHECK(days_at(&v, end - 1) == 1, "%s: the last row shown to its last second", what);
        CHECK(days_at(&v, end) == 0, "%s: the last row gone at local midnight", what);

        const double NONE[3] = {NAN, NAN, NAN};
        ok = dst_days(&v, t, NONE, DST[k].off);
        CHECK(ok && o->day_n == 3 && o->day[0].end == t[1] && o->day[1].end == t[2] && o->day[2].end == 0,
              "%s without end (an older Ember): the next start, the last unknown", what);
        CHECK(days_at(&v, t[2] + 86400 - 1) == 1 && days_at(&v, t[2] + 86400) == 0, "%s without end: the last day lasts 24 h", what);

        static const double BAD[] = {0, -1, 79199, 93601, 86400.5, -86400};
        for (size_t b = 0; b < sizeof BAD / sizeof BAD[0]; b++) {
            const double E[3] = {t[0] + BAD[b], t[1] + BAD[b], t[2] + BAD[b]};
            ok = dst_days(&v, t, E, DST[k].off);
            CHECK(ok && o->day_n == 3 && o->day[0].end == t[1] && o->day[1].end == t[2] && o->day[2].end == 0,
                  "%s: an end at t%+.1f s ignored: %lld %lld %lld", what, BAD[b], o->day[0].end, o->day[1].end, o->day[2].end);
        }
        {
            cJSON *root, *w = faces_weather(&root), *a = cJSON_CreateArray();
            cJSON *d = day_item((double)t[2], 1, 2, "clear", "0", NAN);
            cJSON_AddStringToObject(d, "end", "1774821600");
            cJSON_AddItemToArray(a, d);
            cJSON_ReplaceItemInObjectCaseSensitive(w, "days", a);
            ok = faces_parse(root, &v);
            CHECK(ok && o->day_n == 1 && o->day[0].end == 0, "%s: a string end ignored", what);
        }
        static const long long EDGE[] = {79200, 93600};
        for (size_t b = 0; b < sizeof EDGE / sizeof EDGE[0]; b++) {
            const double E[3] = {(double)(t[0] + EDGE[b]), (double)(t[1] + EDGE[b]), (double)(t[2] + EDGE[b])};
            ok = dst_days(&v, t, E, DST[k].off);
            CHECK(ok && o->day[0].end == t[0] + EDGE[b] && o->day[2].end == t[2] + EDGE[b], "%s: an end %lld h on kept, not the next start",
                  what, EDGE[b] / 3600);
        }
    }
}

static void test_view_sun(void)
{
    knob_view_t v;
    const wx_obs_t *o = &v.weather;
    if (!view("view_faces.json", &v)) return;
    static const long long TW[WX_TWILIGHT] = {1782010500, 1782011700, 1782016500, 1782069900, 1782075000, 1782076200};
    CHECK(o->sunrise == 1782013500 && o->sunset == 1782073200 && !memcmp(o->twilight, TW, sizeof TW), "sun: times and all six crossings");
    CHECK(o->has_sun_down && !o->sun_down, "sun: the night flag, day");
    wx_sun_t s = sun_now(o);
    CHECK(s.have && s.has_now && s.phase == WX_SUN_DAY && s.hours && s.count == 16 && !strcmp(s.unit, "h left") &&
              !strcmp(s.sub, "golden hour 20:25") && !strcmp(s.rise, "04:45") && !strcmp(s.set, "21:20"),
          "Sun at 13:00 local: 8 h left, '%s', %s..%s", s.sub, s.rise, s.set);
    CHECK(wx_sun_start(&s) == TW[0] && wx_sun_end(&s) == TW[5], "Sun: dawn to dusk");
    wx_sun_from(o, &s);
    CHECK(s.have && !s.has_now, "Sun without the server time: no clock");
    wx_temp_t t;
    wx_temp_from(o, &t);
    CHECK(t.face != WXF_SUN && !t.sun.on, "Sun: never the auto face");

    if (!view("view_faces_met.json", &v)) return;
    s = sun_now(o);
    CHECK(s.have && !strcmp(s.rise, "03:45") && !strcmp(s.set, "20:20") && s.t[WX_SUN_DAWN] == TW[0], "MET: the same sun at offset 0");

    if (!view("view_faces_polar_dry.json", &v)) return;
    CHECK(o->sunrise == 1788226500 && o->sunset == 1788293400 && !o->twilight[0] && !o->twilight[1] && o->twilight[2] == 1788236100 &&
              o->twilight[3] == 1788284100 && !o->twilight[4] && !o->twilight[5],
          "polar: only the golden crossings");
    s = sun_now(o);
    CHECK(s.have && wx_sun_start(&s) == 1788226500 && wx_sun_end(&s) == 1788293400 && !strcmp(s.sub, "golden hour 19:35") &&
              !strcmp(s.rise, "03:35") && !strcmp(s.set, "22:10") && s.count == 16,
          "polar Sun: sunrise to sunset, '%s'", s.sub);

    if (!view("view_single_host_paused.json", &v)) return;
    CHECK(!o->sunrise && !o->sunset && o->has_sun_down && !o->sun_down && o->moon_up == -1, "no location: null sun times, no moon");
    s = sun_now(o);
    CHECK(!s.have && s.polar == WX_SUN_NOT_POLAR && !s.sub[0], "no location: never 'Midnight sun'");

    if (!view("view_full.json", &v)) return;
    CHECK(o->sunrise == 1782013500 && o->sunset == 1782073200 && !o->twilight[0] && !o->twilight[5], "a legacy block: sun times, no twilight");
    s = sun_now(o);
    CHECK(!s.have, "without wx_faces there is no offset: no gauge");

    {
        cJSON *root, *w = faces_weather(&root);
        cJSON_ReplaceItemInObjectCaseSensitive(w, "sunrise", cJSON_CreateNull());
        cJSON_ReplaceItemInObjectCaseSensitive(w, "sunset", cJSON_CreateNull());
        cJSON_DeleteItemFromObjectCaseSensitive(w, "twilight");
        bool ok = faces_parse(root, &v);
        CHECK(ok && !o->sunrise && !o->sunset && !o->twilight[2] && o->moon_up == 1, "polar day: null sun times");
        s = sun_now(o);
        CHECK(s.polar == WX_SUN_POLAR_DAY && !strcmp(s.sub, "Midnight sun"), "polar day: '%s'", s.sub);
    }
    {
        cJSON *root, *w = faces_weather(&root);
        cJSON_ReplaceItemInObjectCaseSensitive(w, "sunrise", cJSON_CreateNull());
        cJSON_ReplaceItemInObjectCaseSensitive(w, "night", cJSON_CreateTrue());
        bool ok = faces_parse(root, &v);
        CHECK(ok && !o->sunrise && !o->sunset && o->sun_down, "one null: both unknown");
        s = sun_now(o);
        CHECK(s.polar == WX_SUN_POLAR_NIGHT && !strcmp(s.sub, "Polar night"), "a boundary day after its sunset: '%s'", s.sub);
    }
    {
        cJSON *root, *w = faces_weather(&root), *tw = cJSON_GetObjectItemCaseSensitive(w, "twilight");
        cJSON_ReplaceItemInObjectCaseSensitive(tw, "dawn", cJSON_CreateString("1782010500"));
        cJSON_ReplaceItemInObjectCaseSensitive(tw, "blue_end", cJSON_CreateNumber(1782011700.5));
        cJSON_ReplaceItemInObjectCaseSensitive(tw, "golden_end", cJSON_CreateNumber(-1));
        cJSON_ReplaceItemInObjectCaseSensitive(tw, "golden_start", cJSON_CreateNumber(0));
        cJSON_ReplaceItemInObjectCaseSensitive(tw, "blue_start", cJSON_CreateNumber(1e13));
        cJSON_ReplaceItemInObjectCaseSensitive(w, "night", cJSON_CreateString("true"));
        cJSON_ReplaceItemInObjectCaseSensitive(w, "sunset", cJSON_CreateNumber(1782073200.25));
        bool ok = faces_parse(root, &v);
        CHECK(ok && !o->twilight[0] && !o->twilight[1] && !o->twilight[2] && !o->twilight[3] && !o->twilight[4] &&
                  o->twilight[5] == 1782076200,
              "a string, a fraction, a negative, a zero and a huge crossing are unknown; dusk kept");
        CHECK(!o->has_sun_down && !o->sun_down, "a night flag that is not a boolean: unknown");
        CHECK(!o->sunrise && !o->sunset, "a fractional sunset: both unknown");
    }
    {
        cJSON *root, *w = faces_weather(&root);
        cJSON_ReplaceItemInObjectCaseSensitive(w, "twilight", cJSON_CreateArray());
        cJSON_DeleteItemFromObjectCaseSensitive(w, "night");
        bool ok = faces_parse(root, &v);
        CHECK(ok && !o->twilight[0] && !o->twilight[5] && !o->has_sun_down && o->sunrise == 1782013500, "twilight not an object, no night");
    }
}

static void test_view_air(void)
{
    knob_view_t v;
    const wx_obs_t *o = &v.weather;
    if (!view("view_faces.json", &v)) return;
    CHECK(o->has_aqi && o->aqi == 42, "aqi 42");
    wx_air_t a;
    wx_air_from(o, NULL, &a);
    CHECK(a.have && a.aqi == 42 && a.band == 2 && !a.wilted && !strcmp(wx_air_word(a.band), "Moderate"), "Air: 42 Moderate, healthy");
    wx_temp_t t;
    wx_temp_from(o, &t);
    CHECK(t.face == WXF_RAIN && !t.air.on, "raining: Rain now, not Air");
    if (!view("view_faces_met.json", &v)) return;
    CHECK(!o->has_aqi, "MET with the air tile off: no aqi");
    wx_air_from(o, NULL, &a);
    CHECK(a.on && !a.have, "Air without aqi: grey");
    if (!view("view_faces_polar_dry.json", &v)) return;
    CHECK(!o->has_aqi, "polar: no aqi");
    if (!view("view_full.json", &v)) return;
    CHECK(!o->has_aqi, "a legacy block: no aqi");
    static const struct {
        double v;
        bool str, has;
        int want;
    } C[] = {{68, false, true, 68}, {42.6, false, true, 43}, {0, false, true, 0}, {999, false, true, 999},
             {999.6, false, false, 0}, {-1, false, false, 0}, {1000, false, false, 0}, {42, true, false, 0}};
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        cJSON *root, *w = faces_weather(&root);
        cJSON_ReplaceItemInObjectCaseSensitive(w, "aqi", C[i].str ? cJSON_CreateString("42") : cJSON_CreateNumber(C[i].v));
        bool ok = faces_parse(root, &v);
        CHECK(ok && o->has_aqi == C[i].has && (!C[i].has || o->aqi == C[i].want), "aqi %s%g: %s %d", C[i].str ? "string " : "", C[i].v,
              o->has_aqi ? "kept" : "unknown", o->aqi);
    }
    {
        cJSON *root, *w = faces_weather(&root);
        cJSON_ReplaceItemInObjectCaseSensitive(w, "aqi", cJSON_CreateNumber(75));
        cJSON *now = cJSON_GetObjectItemCaseSensitive(w, "now");
        cJSON_ReplaceItemInObjectCaseSensitive(now, "rain", cJSON_CreateNumber(0));
        cJSON_DeleteItemFromObjectCaseSensitive(w, "rain");
        cJSON_ReplaceItemInObjectCaseSensitive(w, "cond", cJSON_CreateString("clouds"));
        cJSON_ReplaceItemInObjectCaseSensitive(w, "code", cJSON_CreateString("3"));
        bool ok = faces_parse(root, &v);
        wx_temp_from(o, &t);
        CHECK(ok && t.face == WXF_AIR && t.air.wilted && t.air.band == 3, "dry, calm and 75: the Air face, wilted");
    }
}

static void test_view_legacy_weather(void)
{
    const char *names[] = {"view_full.json", "view_caps_limited.json"};
    knob_view_t v;
    wx_temp_t t;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (!view(names[i], &v)) continue;
        const wx_obs_t *o = &v.weather;
        CHECK(!o->has_cloud && !o->has_uv && !o->has_today && !o->has_moon && !o->has_wind && !o->has_wind_dir,
              "%s: no detail without wx_faces", names[i]);
        wx_temp_from(o, &t);
        CHECK(t.have && !t.has_range && t.lo == WX_SCALE_LO && t.hi == WX_SCALE_HI && t.cloud_band == 4 &&
                  t.hero == WX_HERO_NONE && strcmp(t.sub, "Rain") == 0,
              "%s: rain without detail: fixed scale, overcast, '%s'", names[i], t.sub);
    }
    if (!view("view_single_host_paused.json", &v)) return;
    wx_temp_from(&v.weather, &t);
    CHECK(t.have && t.value == -3 && t.still && !t.night && t.hero == WX_HERO_SUN && strcmp(t.sub, "Sunny") == 0,
          "stale clear, night unknown: day sun, grey value");
}

static void test_view_np_cover(void)
{
    knob_view_t v;
    if (!view("view_np_cover.json", &v)) return;
    CHECK(v.has_np && v.np.state == NP_PLAYING && strcmp(v.np.title, "Example Song") == 0, "cover view: now playing");
    CHECK(!v.has_weather, "no weather block");
}

static int hook_allocs, hook_frees;

static void *count_malloc(size_t n)
{
    hook_allocs++;
    return malloc(n);
}

static void count_free(void *p)
{
    if (p) hook_frees++;
    free(p);
}

static void test_json_hooks(void)
{
    char *j = load("view_faces.json");
    if (!j) return;
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = count_malloc, .free_fn = count_free});
    knob_view_t v;
    bool ok = knob_view_parse(j, &v);
    int faces = hook_allocs;
    hook_allocs = hook_frees = 0;
    char *legacy = load("view_caps_limited.json");
    ok &= legacy && knob_view_parse(legacy, &v);
    cJSON_InitHooks(NULL);
    printf("fixtures: cJSON allocations: wx_faces view %d, legacy view %d\n", faces, hook_allocs);
    CHECK(ok && faces >= 150 && hook_allocs < faces / 2 && hook_frees == hook_allocs,
          "every node of the wx_faces view goes through the hooks and is freed: %d vs %d legacy, %d freed", faces, hook_allocs,
          hook_frees);
    free(legacy);
    free(j);
}

static void apply(pomo_srv_clock_t *clk, wx_store_t *st, const knob_view_t *v, long long srv_now, double sent,
                  double received)
{
    wx_store_put(st, v->has_weather ? &v->weather : NULL, pomo_srv_clock_feed(clk, srv_now, sent, received), received);
}

static void label_at(const wx_store_t *st, double now, char *lb, size_t n)
{
    wx_obs_t o;
    if (wx_store_get(st, now, &o)) wx_age_label(&o, lb, n);
    else snprintf(lb, n, "-");
}

static void test_obs_age_wiring(void)
{
    knob_view_t v;
    if (!view("view_caps_limited.json", &v)) return;
    char lb[16];
    pomo_srv_clock_t clk;
    wx_store_t st = {0};

    pomo_srv_clock_init(&clk);
    apply(&clk, &st, &v, 0, 100, 100.2);
    label_at(&st, 100.2 + 90, lb, sizeof lb);
    CHECK(strcmp(lb, "1 min ago") == 0, "first view, no server clock yet: the confirm age, got '%s'", lb);
    apply(&clk, &st, &v, 1782043200LL, 200, 200);
    label_at(&st, 200 + 60, lb, sizeof lb);
    CHECK(strcmp(lb, "31 min ago") == 0, "a later answer with X-Ember-Now: the observation age, got '%s'", lb);

    pomo_srv_clock_init(&clk);
    st = (wx_store_t){0};
    apply(&clk, &st, &v, 1782043200LL, 100, 100);
    wx_obs_t o;
    CHECK(wx_store_get(&st, 100, &o) && o.has_obs_age && fabs(o.obs_age_s - 1800.5) < 0.01,
          "first view: this answer's own X-Ember-Now sets the age, %.2f", o.obs_age_s);
    label_at(&st, 100 + 60, lb, sizeof lb);
    CHECK(strcmp(lb, "31 min ago") == 0, "first view: label '%s'", lb);

    apply(&clk, &st, &v, 1782043800LL, 700, 700);
    CHECK(wx_store_get(&st, 700, &o) && o.age_s == 0 && fabs(o.obs_age_s - 2400.5) < 0.01,
          "304 re-apply: confirm age restarts, observation age %.2f", o.obs_age_s);
    label_at(&st, 700 + 5, lb, sizeof lb);
    CHECK(strcmp(lb, "40 min ago") == 0, "304 re-apply: label '%s'", lb);
    apply(&clk, &st, &v, 0, 1300, 1300);
    CHECK(wx_store_get(&st, 1300, &o) && o.has_obs_age && fabs(o.obs_age_s - 3000.5) < 0.01,
          "304 without a clock sample: the kept offset, %.2f", o.obs_age_s);

    label_at(&st, 1300 + 600, lb, sizeof lb);
    CHECK(strcmp(lb, "1 h ago") == 0, "offline 10 min: '%s'", lb);
    label_at(&st, 1300 + 3 * 3600, lb, sizeof lb);
    CHECK(strcmp(lb, "3 h ago") == 0, "offline 3 h: '%s'", lb);
    label_at(&st, 1300 + 2 * 86400, lb, sizeof lb);
    CHECK(strcmp(lb, "2 d ago") == 0, "offline 2 d: '%s'", lb);

    v.weather.stale = true;
    v.weather.observed_at = 1782045000LL;
    apply(&clk, &st, &v, 0, 1400, 1400);
    label_at(&st, 1400 + 60, lb, sizeof lb);
    CHECK(strcmp(lb, "stale") == 0, "stale with a provider clock ahead: '%s'", lb);

    knob_view_t none = {0};
    apply(&clk, &st, &none, 0, 1500, 1500);
    CHECK(!wx_store_get(&st, 1500, &o), "a view without weather clears it");
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
        CHECK(ks.rotation == 0, "rotation 0");
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
    CHECK(ks.rotation == 0, "rotation 0");

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
    CHECK(ks.rotation == 180, "rotation 180");
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

static const char *const FEATURES_AHEAD[] = {"wx_faces", "wx_moments", "demo"};
#define N_FEATURES_AHEAD ((int)(sizeof FEATURES_AHEAD / sizeof FEATURES_AHEAD[0]))
static int features_ahead_used[N_FEATURES_AHEAD];

static void features_diff(const char *name, const cJSON *want, const cJSON *got)
{
    cJSON *g = cJSON_Duplicate(got, true);
    for (int i = 0; i < N_FEATURES_AHEAD; i++) {
        int sent = 0;
        const cJSON *e;
        cJSON_ArrayForEach(e, want)
        {
            CHECK(!cJSON_IsString(e) || strcmp(e->valuestring, FEATURES_AHEAD[i]) != 0,
                  "%s: the fixture has feature %s now, drop it from FEATURES_AHEAD", name, FEATURES_AHEAD[i]);
        }
        for (int k = cJSON_GetArraySize(g) - 1; k >= 0; k--) {
            const char *s = cJSON_GetStringValue(cJSON_GetArrayItem(g, k));
            if (s && strcmp(s, FEATURES_AHEAD[i]) == 0) {
                cJSON_DeleteItemFromArray(g, k);
                sent++;
            }
        }
        CHECK(sent == 1, "%s: the firmware sends %s %d times", name, FEATURES_AHEAD[i], sent);
        features_ahead_used[i] += sent;
    }
    CHECK(cJSON_Compare(want, g, true), "%s: .caps.features differs", name);
    cJSON_Delete(g);
}

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
        } else if (strcmp(p, ".caps.features") == 0 && cJSON_IsArray(e) && cJSON_IsArray(g)) {
            features_diff(name, e, g);
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
    for (int i = 0; i < N_FEATURES_AHEAD; i++)
        CHECK(features_ahead_used[i] > 0, "FEATURES_AHEAD %s is in no checkin_req fixture with caps (%d)", FEATURES_AHEAD[i],
              features_ahead_used[i]);
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
                      .frame_ms_avg = (float)num(sj, "frame_ms_avg"), .frame_ms_max = (uint32_t)num(sj, "frame_ms_max"),
                      .has_inv = at(sj, "inv_areas") != NULL, .inv_areas = (int64_t)num(sj, "inv_areas"),
                      .inv_px = (int64_t)num(sj, "inv_px")};
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
    test_view_faces();
    test_view_rain_keys();
    test_view_days();
    test_view_day_ends();
    test_view_sun();
    test_view_air();
    test_view_legacy_weather();
    test_view_np_cover();
    test_json_hooks();
    test_obs_age_wiring();
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
