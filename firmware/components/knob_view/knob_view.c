#include "knob_view.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "wx_air.h"
#include "wx_wind.h"

static const cJSON *item(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k); }

static int num_int(const cJSON *o, const char *k)
{
    const cJSON *v = item(o, k);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : 0;
}

static bool num_ll(const cJSON *o, const char *k, long long *out)
{
    const cJSON *v = item(o, k);
    if (!cJSON_IsNumber(v)) return false;
    *out = (long long)v->valuedouble;
    return true;
}

static void str_copy(char *dst, size_t cap, const cJSON *o, const char *k)
{
    const cJSON *v = item(o, k);
    snprintf(dst, cap, "%s", cJSON_IsString(v) && v->valuestring ? v->valuestring : "");
}

bot_mood_t knob_view_mood_from(int waiting, int errors, int running, int done)
{
    return waiting ? BOT_WAITING : errors ? BOT_ERROR : running ? BOT_WORKING : done ? BOT_DONE : BOT_IDLE;
}

bot_mood_t knob_view_mood(const knob_view_t *v)
{
    return knob_view_mood_from(v->waiting, v->errors, v->running, v->done);
}

static void parse_pomo(const cJSON *p, knob_view_t *out)
{
    if (!cJSON_IsObject(p)) return;
    const cJSON *phase = item(p, "phase");
    out->has_pomo = true;
    out->pomo = (pomo_state_t){
        .phase = pomo_phase_from_wire(cJSON_IsString(phase) ? phase->valuestring : NULL),
        .running = cJSON_IsTrue(item(p, "running")),
        .paused = cJSON_IsTrue(item(p, "paused")),
        .remaining_sec = num_int(p, "remaining_sec"),
        .planned_sec = num_int(p, "planned_sec"),
        .round = num_int(p, "round"),
    };
    out->pomo_counting = num_ll(p, "ends_at", &out->ends_at);
    if (out->pomo_counting) out->pomo.remaining_sec = 0;
}

static bool num_in(const cJSON *o, const char *k, int lo, int hi, int *out)
{
    const cJSON *v = cJSON_IsObject(o) ? item(o, k) : NULL;
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= lo && v->valuedouble <= hi)) return false;
    *out = (int)(v->valuedouble < 0 ? v->valuedouble - 0.5 : v->valuedouble + 0.5);
    return true;
}

static bool int_in(const cJSON *o, const char *k, int lo, int hi, int *out)
{
    const cJSON *v = cJSON_IsObject(o) ? item(o, k) : NULL;
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= lo && v->valuedouble <= hi) || v->valuedouble != (int)v->valuedouble)
        return false;
    *out = (int)v->valuedouble;
    return true;
}

static bool unix_in(const cJSON *v, long long *out)
{
    if (!cJSON_IsNumber(v) || !(v->valuedouble > 0 && v->valuedouble < 1e13) ||
        v->valuedouble != (double)(long long)v->valuedouble)
        return false;
    *out = (long long)v->valuedouble;
    return true;
}

static bool spell_in(const cJSON *s, wx_spell_t *out)
{
    if (!cJSON_IsArray(s) || cJSON_GetArraySize(s) < 4) return false;
    wx_spell_t p;
    const cJSON *in = cJSON_GetArrayItem(s, 2), *kind = cJSON_GetArrayItem(s, 3);
    if (!unix_in(cJSON_GetArrayItem(s, 0), &p.start) || !unix_in(cJSON_GetArrayItem(s, 1), &p.end) || p.end <= p.start ||
        !cJSON_IsNumber(in) || !cJSON_IsNumber(kind))
        return false;
    double i = in->valuedouble, k = kind->valuedouble;
    if (!(i == 1 || i == 2 || i == 3) || !(k == 0 || k == 1)) return false;
    p.intensity = (int)i;
    p.kind = (int)k;
    *out = p;
    return true;
}

static void parse_rain(const cJSON *r, wx_obs_t *o)
{
    o->has_rain = cJSON_IsObject(r);
    if (!o->has_rain) return;
    long long u;
    if (unix_in(item(r, "until"), &u)) o->rain_until = u;
    const cJSON *sp = item(r, "spells");
    for (const cJSON *s = cJSON_IsArray(sp) ? sp->child : NULL; s && o->rain_n < WX_SPELLS; s = s->next)
        if (spell_in(s, &o->rain_spell[o->rain_n])) o->rain_n++;
}

static long long day_end(const cJSON *e, long long t)
{
    long long end, next;
    if (unix_in(item(e, "end"), &end) && end >= t + WX_DAY_END_MIN_S && end <= t + WX_DAY_END_MAX_S) return end;
    return e->next && unix_in(item(e->next, "t"), &next) && next > t && next <= t + WX_DAY_MAX_S ? next : 0;
}

static void parse_days(const cJSON *d, wx_obs_t *o)
{
    for (const cJSON *e = cJSON_IsArray(d) ? d->child : NULL; e && o->day_n < WX_DAYS; e = e->next) {
        wx_day_t day;
        int lo, hi, pop;
        if (!unix_in(item(e, "t"), &day.t) || !num_in(e, "min_c", -99, 99, &lo) ||
            !num_in(e, "max_c", -99, 99, &hi) || lo > hi || (o->day_n && day.t <= o->day[o->day_n - 1].t))
            continue;
        char cond[12], code[40];
        str_copy(cond, sizeof cond, e, "cond");
        str_copy(code, sizeof code, e, "code");
        day.face = (signed char)wx_look_from(o->provider, cond, code, 0).face;
        day.pop = (signed char)(int_in(e, "pop", 0, 100, &pop) ? pop : -1);
        day.min_c = (short)lo;
        day.max_c = (short)hi;
        day.end = day_end(e, day.t);
        o->day[o->day_n++] = day;
    }
}

static void parse_twilight(const cJSON *t, wx_obs_t *o)
{
    static const char *const KEY[WX_TWILIGHT] = {"dawn", "blue_end", "golden_end", "golden_start", "blue_start", "dusk"};
    for (int i = 0; i < WX_TWILIGHT; i++) unix_in(item(t, KEY[i]), &o->twilight[i]);
}

static void parse_detail(const cJSON *w, wx_obs_t *o)
{
    const cJSON *now = item(w, "now"), *today = item(w, "today"), *moon = item(w, "moon");
    o->has_cloud = num_in(now, "cloud", 0, 100, &o->cloud);
    o->has_uv = num_in(now, "uv", 0, 40, &o->uv);
    o->has_today = num_in(today, "min_c", -99, 99, &o->min_c) && num_in(today, "max_c", -99, 99, &o->max_c) &&
                   o->min_c <= o->max_c;
    o->has_moon = num_in(moon, "phase", 0, 99, &o->moon_phase) && num_in(moon, "illum", 0, 100, &o->moon_illum);
    const cJSON *up = cJSON_IsObject(moon) ? item(moon, "up") : NULL;
    o->moon_up = cJSON_IsBool(up) ? cJSON_IsTrue(up) : -1;
    o->has_now_rain = int_in(now, "rain", 0, 3, &o->now_rain);
    o->has_wind = num_in(now, "wind_kmh", 0, WX_WIND_MAX_KMH, &o->wind_kmh);
    o->has_wind_dir = num_in(now, "wind_dir", 0, 360, &o->wind_dir);
    if (o->has_wind_dir) o->wind_dir %= 360;
    o->has_pop = int_in(today, "pop", 0, 100, &o->pop);
    o->has_utc_offset = int_in(w, "utc_offset", -50400, 50400, &o->utc_offset);
    parse_rain(item(w, "rain"), o);
    parse_days(item(w, "days"), o);
    parse_twilight(item(w, "twilight"), o);
    o->has_aqi = num_in(w, "aqi", 0, WX_AIR_MAX, &o->aqi);
}

static void parse_weather(const cJSON *w, knob_view_t *out)
{
    if (!cJSON_IsObject(w)) return;
    wx_obs_t *o = &out->weather;
    out->has_weather = true;
    o->valid = true;
    o->enabled = true;
    o->stale = cJSON_IsTrue(item(w, "stale"));
    o->severe = cJSON_IsTrue(item(w, "severe"));
    str_copy(o->provider, sizeof o->provider, w, "provider");
    str_copy(o->condition, sizeof o->condition, w, "cond");
    str_copy(o->code, sizeof o->code, w, "code");
    const cJSON *t = item(w, "temp_c");
    if (cJSON_IsNumber(t)) {
        o->has_temp = true;
        o->temp_c = (float)t->valuedouble;
    }
    o->now_min = o->rise_min = o->set_min = -1;
    long long rise, set;
    if (num_ll(w, "sunrise", &rise) && num_ll(w, "sunset", &set)) {
        o->has_night = true;
        o->night = cJSON_IsTrue(item(w, "night"));
    }
    if (!unix_in(item(w, "sunrise"), &o->sunrise) || !unix_in(item(w, "sunset"), &o->sunset)) o->sunrise = o->sunset = 0;
    const cJSON *night = item(w, "night");
    o->has_sun_down = cJSON_IsBool(night);
    o->sun_down = cJSON_IsTrue(night);
    const cJSON *at = item(w, "observed_at");
    if (cJSON_IsNumber(at) && at->valuedouble >= 1577836800.0 && at->valuedouble < 1e13 &&
        at->valuedouble == (double)(long long)at->valuedouble)
        o->observed_at = (long long)at->valuedouble;
    parse_detail(w, o);
}

static knob_view_res_t view_major(const cJSON *root, int *major)
{
    const cJSON *v = cJSON_IsObject(root) ? item(root, "v") : NULL;
    if (!cJSON_IsNumber(v)) return KNOB_VIEW_BAD;
    double d = v->valuedouble;
    int m = d < -1e9 ? -1000000000 : d > 1e9 ? 1000000000 : (int)d;
    if (d >= -1e9 && d <= 1e9 && d != (double)m) return KNOB_VIEW_BAD;
    if (major) *major = m;
    if (m < KNOB_VIEW_V_MIN) return KNOB_VIEW_TOO_OLD;
    if (m > KNOB_VIEW_V_MAX) return KNOB_VIEW_TOO_NEW;
    return KNOB_VIEW_OK;
}

static void parse_mood(const cJSON *mood, knob_view_t *out)
{
    if (!cJSON_IsObject(mood)) return;
    out->has_mood = true;
    out->waiting = num_int(mood, "waiting");
    out->errors = num_int(mood, "errors");
    out->running = num_int(mood, "running");
    out->done = num_int(mood, "done");
    str_copy(out->source, sizeof out->source, mood, "source");
    str_copy(out->lead, sizeof out->lead, mood, "lead");
    out->hosts = num_int(mood, "hosts");
    str_copy(out->lead_color, sizeof out->lead_color, mood, "lead_color");
    str_copy(out->tool, sizeof out->tool, mood, "tool");
}

static void read_body(const cJSON *root, knob_view_t *out)
{
    const cJSON *e = item(root, "epoch"), *cv = item(root, "config_version");
    out->epoch = cJSON_IsNumber(e) && e->valuedouble > 0 ? (unsigned long long)e->valuedouble : 0;
    out->config_version = cJSON_IsNumber(cv) && cv->valuedouble > 0 ? (unsigned long)cv->valuedouble : 0;
    parse_mood(item(root, "mood"), out);
    parse_pomo(item(root, "pomo"), out);
    parse_weather(item(root, "weather"), out);
    const cJSON *b = item(root, "brightness");
    const cJSON *lv = cJSON_IsObject(b) ? item(b, "level") : NULL;
    if (cJSON_IsNumber(lv) && lv->valuedouble >= 0 && lv->valuedouble <= 255) {
        out->has_brightness = true;
        out->level = (int)lv->valuedouble;
        out->bright_night = cJSON_IsTrue(item(b, "night"));
    }
    out->quiet = cJSON_IsTrue(item(root, "quiet"));
    out->has_np = np_parse(item(root, "nowplaying"), &out->np);
    long long lu;
    if (num_ll(root, "diag_live_until", &lu) && lu > 0) out->diag_live_until = lu;
}

knob_view_res_t knob_view_read_full(const char *json, knob_view_t *out, int *major, knob_view_t *demo_view)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    knob_view_res_t res = view_major(root, major);
    if (res == KNOB_VIEW_TOO_OLD || res == KNOB_VIEW_TOO_NEW) {
        cJSON_Delete(root);
        return res;
    }
    memset(out, 0, sizeof *out);
    out->demo.face = -1;
    if (demo_view) memset(demo_view, 0, sizeof *demo_view);
    if (res == KNOB_VIEW_OK) {
        out->v = num_int(root, "v");
        read_body(root, out);
        const cJSON *d = item(root, "demo");
        if (demo_block_parse(d, &out->demo) && out->demo.has_view && demo_view) {
            demo_view->v = out->v;
            demo_view->demo.face = -1;
            read_body(item(d, "view"), demo_view);
        }
    }
    cJSON_Delete(root);
    return res;
}

knob_view_res_t knob_view_read(const char *json, knob_view_t *out, int *major)
{
    return knob_view_read_full(json, out, major, NULL);
}

bool knob_view_parse(const char *json, knob_view_t *out) { return knob_view_read(json, out, NULL) == KNOB_VIEW_OK; }

void knob_view_epoch_key(const knob_view_t *v, char *out, size_t cap)
{
    snprintf(out, cap, "%llu/%lu", v->epoch, v->config_version);
}

bool knob_quiet_next(bool cur, knob_quiet_src_t src, bool view_quiet) { return src == KNOB_QUIET_VIEW ? view_quiet : cur; }
