#include "weather_face.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const FACE_NAMES[WX_FACE_COUNT] = {
    "clear-day", "clear-night", "partly-cloudy", "overcast", "fog", "rain", "snow", "storm",
};

const char *wx_face_name(wx_face_t f)
{
    return (unsigned)f < WX_FACE_COUNT ? FACE_NAMES[f] : "?";
}

static int two_digits(const char *s)
{
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1])) return -1;
    return (s[0] - '0') * 10 + (s[1] - '0');
}

int wx_minute_of_day(const char *iso)
{
    if (!iso || strlen(iso) < 16 || iso[10] != 'T' || iso[13] != ':') return -1;
    int h = two_digits(iso + 11), m = two_digits(iso + 14);
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

int wx_is_night(int now_min, int rise_min, int set_min)
{
    if (now_min < 0 || rise_min < 0 || set_min < 0) return -1;
    if (rise_min < set_min) return (now_min < rise_min || now_min >= set_min) ? 1 : 0;
    /* Sunset before sunrise on the clock (far east/west offsets): day wraps midnight. */
    return (now_min >= set_min && now_min < rise_min) ? 1 : 0;
}

bool wx_look_equal(const wx_look_t *a, const wx_look_t *b)
{
    return a->face == b->face && a->intensity == b->intensity && a->night == b->night && a->rime == b->rime &&
           a->severe == b->severe && a->still == b->still;
}

static wx_look_t look(wx_face_t f, wx_intensity_t i)
{
    wx_look_t l = {.face = f, .intensity = i};
    return l;
}

static wx_look_t from_bucket(const char *c)
{
    if (!strcmp(c, "clear")) return look(WX_CLEAR_DAY, WX_INT_NONE);
    if (!strcmp(c, "fog")) return look(WX_FOG, WX_INT_NONE);
    if (!strcmp(c, "rain")) return look(WX_RAIN, WX_INT_MODERATE);
    if (!strcmp(c, "snow")) return look(WX_SNOW, WX_INT_MODERATE);
    if (!strcmp(c, "storm")) return look(WX_STORM, WX_INT_MODERATE);
    return look(WX_OVERCAST, WX_INT_NONE);
}

static bool from_wmo(int c, wx_look_t *out)
{
    switch (c) {
    case 0: *out = look(WX_CLEAR_DAY, WX_INT_NONE); return true;
    case 1: case 2: *out = look(WX_PARTLY_CLOUDY, WX_INT_NONE); return true;
    case 3: *out = look(WX_OVERCAST, WX_INT_NONE); return true;
    case 45: *out = look(WX_FOG, WX_INT_NONE); return true;
    case 48: *out = look(WX_FOG, WX_INT_NONE); out->rime = true; return true;
    case 51: case 53: case 55: case 56: case 57:
        *out = look(WX_RAIN, WX_INT_LIGHT); return true;
    case 61: case 63: case 66: case 80: case 81:
        *out = look(WX_RAIN, WX_INT_MODERATE); return true;
    case 65: case 67: case 82:
        *out = look(WX_RAIN, WX_INT_HEAVY); return true;
    case 71: case 77: case 85: *out = look(WX_SNOW, WX_INT_LIGHT); return true;
    case 73: *out = look(WX_SNOW, WX_INT_MODERATE); return true;
    case 75: case 86: *out = look(WX_SNOW, WX_INT_HEAVY); return true;
    case 95: *out = look(WX_STORM, WX_INT_MODERATE); return true;
    case 96: case 99: *out = look(WX_STORM, WX_INT_HEAVY); return true;
    default: return false;
    }
}

static bool from_met(const char *sym, wx_look_t *out, int *night)
{
    char s[40];
    size_t n = 0;
    for (; sym[n] && n < sizeof s - 1; n++) s[n] = (char)tolower((unsigned char)sym[n]);
    s[n] = 0;
    if (strstr(s, "_night") || strstr(s, "_polartwilight")) *night = 1;
    else if (strstr(s, "_day")) *night = 0;
    wx_intensity_t in = strstr(s, "heavy") ? WX_INT_HEAVY : strstr(s, "light") ? WX_INT_LIGHT : WX_INT_MODERATE;
    if (strstr(s, "thunder")) *out = look(WX_STORM, in);
    else if (strstr(s, "sleet")) *out = look(WX_RAIN, in);
    else if (strstr(s, "snow")) *out = look(WX_SNOW, in);
    else if (strstr(s, "rain") || strstr(s, "showers")) *out = look(WX_RAIN, in);
    else if (strstr(s, "fog")) *out = look(WX_FOG, WX_INT_NONE);
    else if (strstr(s, "partlycloudy")) *out = look(WX_PARTLY_CLOUDY, WX_INT_NONE);
    else if (strstr(s, "cloudy")) *out = look(WX_OVERCAST, WX_INT_NONE);
    else if (strstr(s, "clearsky") || strstr(s, "fair")) *out = look(WX_CLEAR_DAY, WX_INT_NONE);
    else return false;
    return true;
}

wx_look_t wx_look_from(const char *provider, const char *condition, const char *code, int night)
{
    provider = provider ? provider : "";
    condition = condition ? condition : "";
    code = code ? code : "";
    wx_look_t l;
    bool ok = false;
    int sfx_night = -1;
    if (*code) {
        char *end;
        long v = strtol(code, &end, 10);
        if (end != code && *end == 0) ok = from_wmo((int)v, &l);
        else if (strcmp(provider, "open-meteo") != 0) ok = from_met(code, &l, &sfx_night);
    }
    if (!ok) l = from_bucket(condition);
    if (night < 0) night = sfx_night;
    l.night = night == 1;
    if (l.face == WX_CLEAR_DAY && l.night) l.face = WX_CLEAR_NIGHT;
    return l;
}

wx_look_t wx_look_from_obs(const wx_obs_t *o)
{
    if (!o || !o->valid) {
        wx_look_t l = look(WX_OVERCAST, WX_INT_NONE);
        l.still = true;
        return l;
    }
    int now = o->now_min;
    if (now >= 0) now = (now + (int)(o->age_s / 60.0)) % 1440;
    int night = o->has_night ? (o->night ? 1 : 0) : wx_is_night(now, o->rise_min, o->set_min);
    wx_look_t l = wx_look_from(o->provider, o->condition, o->code, night);
    l.severe = o->severe;
    l.still = !o->enabled || o->stale || o->age_s > WX_MAX_AGE_S;
    return l;
}

int wx_age_text(double age_s, char *buf, size_t n)
{
    if (!(age_s >= 60)) return snprintf(buf, n, "just now");
    if (age_s < 3600) return snprintf(buf, n, "%d min ago", (int)(age_s / 60));
    if (age_s < 86400) return snprintf(buf, n, "%d h ago", (int)(age_s / 3600));
    double d = age_s / 86400;
    return snprintf(buf, n, "%d d ago", d < 999 ? (int)d : 999);
}

int wx_age_label(const wx_obs_t *o, char *buf, size_t n)
{
    if (o->stale) return snprintf(buf, n, "stale");
    return wx_age_text(o->age_s, buf, n);
}
