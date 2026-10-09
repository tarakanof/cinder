#include "knob_view.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

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

knob_view_res_t knob_view_read(const char *json, knob_view_t *out, int *major)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    knob_view_res_t res = view_major(root, major);
    if (res == KNOB_VIEW_TOO_OLD || res == KNOB_VIEW_TOO_NEW) {
        cJSON_Delete(root);
        return res;
    }
    memset(out, 0, sizeof *out);
    if (res == KNOB_VIEW_OK) {
        out->v = num_int(root, "v");
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
    cJSON_Delete(root);
    return res;
}

bool knob_view_parse(const char *json, knob_view_t *out) { return knob_view_read(json, out, NULL) == KNOB_VIEW_OK; }

void knob_view_epoch_key(const knob_view_t *v, char *out, size_t cap)
{
    snprintf(out, cap, "%llu/%lu", v->epoch, v->config_version);
}

bool knob_quiet_next(bool cur, knob_quiet_src_t src, bool view_quiet) { return src == KNOB_QUIET_VIEW ? view_quiet : cur; }
