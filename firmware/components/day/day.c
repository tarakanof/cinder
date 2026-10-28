#include "day.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const cJSON *item(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k); }

static int count(const cJSON *o, const char *k, int none)
{
    const cJSON *v = item(o, k);
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= 0)) return none;
    return v->valuedouble > DAY_COUNT_MAX ? DAY_COUNT_MAX : (int)v->valuedouble;
}

static int hhmm(const cJSON *o, const char *k)
{
    const cJSON *v = item(o, k);
    return cJSON_IsString(v) ? day_hhmm_parse(v->valuestring) : DAY_NONE;
}

int day_hhmm_parse(const char *s)
{
    if (!s || strlen(s) != 5 || s[2] != ':') return DAY_NONE;
    for (int i = 0; i < 5; i++)
        if (i != 2 && (s[i] < '0' || s[i] > '9')) return DAY_NONE;
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
    return h < 24 && m < 60 ? h * 60 + m : DAY_NONE;
}

void day_hhmm(int min, char *buf, size_t n)
{
    if (min < 0 || min >= DAY_MINUTES) snprintf(buf, n, "--:--");
    else snprintf(buf, n, "%02d:%02d", min / 60, min % 60);
}

static int wrap(int m) { return ((m % DAY_MINUTES) + DAY_MINUTES) % DAY_MINUTES; }

int day_fwd(int a, int b) { return wrap(b - a); }

int day_step(int m, int steps, int step)
{
    if (!steps) return m;
    int off = m % step;
    if (off && steps > 0) {
        m += step - off;
        steps--;
    } else if (off) {
        m -= off;
        steps++;
    }
    return wrap(m + steps * step);
}

uint32_t day_scale(uint32_t rgb, float k)
{
    uint32_t out = 0;
    for (int s = 16; s >= 0; s -= 8) out |= (uint32_t)((float)((rgb >> s) & 0xFF) * k + 0.5f) << s;
    return out;
}

bool day_parse(const cJSON *o, day_block_t *out)
{
    memset(out, 0, sizeof *out);
    out->started = out->stop_at = out->offer_start = out->agents = DAY_NONE;
    if (!cJSON_IsObject(o)) return false;
    const cJSON *st = item(o, "state");
    const char *s = cJSON_IsString(st) ? st->valuestring : "";
    if (strcmp(s, "off") == 0) out->state = DAY_OFF;
    else if (strcmp(s, "on") == 0) out->state = DAY_ON;
    else if (strcmp(s, "break") == 0) out->state = DAY_BREAK;
    else return false;
    out->started = hhmm(o, "started");
    out->stop_at = hhmm(o, "stop_at");
    out->offer_start = hhmm(o, "offer_start");
    out->on_min = count(o, "on_min", 0);
    out->break_min = count(o, "break_min", 0);
    out->over_min = count(o, "over_min", 0);
    out->replies_off_last_night = count(o, "replies_off_last_night", 0);
    out->agents = count(o, "agents", DAY_NONE);
    out->break_hint = cJSON_IsTrue(item(o, "break_hint"));
    const cJSON *br = item(o, "breaks"), *e;
    if (cJSON_IsArray(br)) {
        cJSON_ArrayForEach(e, br)
        {
            if (out->n_breaks == DAY_BREAKS) break;
            const cJSON *s = cJSON_GetArrayItem(e, 0), *f = cJSON_GetArrayItem(e, 1);
            int a = cJSON_IsArray(e) && cJSON_GetArraySize(e) == 2 && cJSON_IsString(s) ? day_hhmm_parse(s->valuestring) : DAY_NONE;
            int z = a != DAY_NONE && cJSON_IsString(f) ? day_hhmm_parse(f->valuestring) : DAY_NONE;
            if (z == DAY_NONE) continue;
            out->breaks[out->n_breaks][0] = a;
            out->breaks[out->n_breaks][1] = z;
            out->n_breaks++;
        }
    }
    const cJSON *sm = item(o, "summary");
    if (cJSON_IsObject(sm)) {
        out->summary.has = true;
        out->summary.on_min = count(sm, "on_min", 0);
        out->summary.in_range = cJSON_IsTrue(item(sm, "in_range"));
        out->summary.week_in_range = count(sm, "week_in_range", 0);
        out->summary.week_days = count(sm, "week_days", 0);
    }
    return true;
}

const char *day_act_path(day_act_kind_t k)
{
    switch (k) {
    case DAY_ACT_START: return "start";
    case DAY_ACT_BREAK: return "break";
    case DAY_ACT_RESUME: return "resume";
    case DAY_ACT_STOP: return "stop";
    case DAY_ACT_STOP_AT: return "stop_at";
    case DAY_ACT_DISMISS: return "offer/dismiss";
    default: return NULL;
    }
}

bool day_act_put(day_act_kind_t k) { return k == DAY_ACT_STOP_AT; }

int day_act_body(const day_act_t *a, char *buf, size_t n)
{
    char stop[8], at[8];
    int len;
    if (a->kind == DAY_ACT_START || a->kind == DAY_ACT_STOP_AT) {
        if (a->stop_at < 0 || a->stop_at >= DAY_MINUTES) return -1;
        day_hhmm(a->stop_at, stop, sizeof stop);
    }
    if (a->kind == DAY_ACT_START && a->at != DAY_NONE) {
        if (a->at < 0 || a->at >= DAY_MINUTES) return -1;
        day_hhmm(a->at, at, sizeof at);
        len = snprintf(buf, n, "{\"at\":\"%s\",\"stop_at\":\"%s\"}", at, stop);
    } else if (a->kind == DAY_ACT_START || a->kind == DAY_ACT_STOP_AT) {
        len = snprintf(buf, n, "{\"stop_at\":\"%s\"}", stop);
    } else if (day_act_path(a->kind)) {
        len = snprintf(buf, n, "{}");
    } else {
        return -1;
    }
    return len >= 0 && (size_t)len < n ? len : -1;
}

const char *day_face_name(day_face_t f)
{
    static const char *const NAMES[] = {"wait", "off", "set", "on", "break", "past", "offer", "summary"};
    return (unsigned)f < sizeof NAMES / sizeof NAMES[0] ? NAMES[f] : "?";
}
