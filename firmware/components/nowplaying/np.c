#include "np.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const cJSON *item(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k); }

static void utf8_copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void str_field(char *dst, size_t cap, const cJSON *o, const char *k)
{
    const cJSON *v = item(o, k);
    utf8_copy(dst, cap, cJSON_IsString(v) && v->valuestring ? v->valuestring : "");
}

static long long ll_field(const cJSON *o, const char *k)
{
    const cJSON *v = item(o, k);
    if (!cJSON_IsNumber(v) || !(v->valuedouble > 0)) return 0;
    return v->valuedouble < 1e15 ? (long long)v->valuedouble : 1000000000000000LL;
}

bool np_parse(const cJSON *o, np_info_t *out)
{
    memset(out, 0, sizeof *out);
    out->volume = -1;
    if (!cJSON_IsObject(o)) return false;
    const cJSON *st = item(o, "state");
    const char *s = cJSON_IsString(st) ? st->valuestring : "";
    out->state = strcmp(s, "playing") == 0 ? NP_PLAYING : strcmp(s, "paused") == 0 ? NP_PAUSED : NP_NONE;
    if (out->state == NP_NONE) return true;
    str_field(out->source, sizeof out->source, o, "source");
    str_field(out->title, sizeof out->title, o, "title");
    str_field(out->artist, sizeof out->artist, o, "artist");
    str_field(out->album, sizeof out->album, o, "album");
    str_field(out->track_id, sizeof out->track_id, o, "track_id");
    out->duration_ms = ll_field(o, "duration_ms");
    out->position_ms = ll_field(o, "position_ms");
    out->position_at = ll_field(o, "position_at");
    char v[NP_ARTV_MAX + 2];
    str_field(v, sizeof v, o, "art_version");
    if (np_art_version_ok(v)) strcpy(out->art_version, v);
    out->album_art = cJSON_IsTrue(item(o, "album_art"));
    out->artist_art = cJSON_IsTrue(item(o, "artist_art"));
    const cJSON *vol = item(o, "volume");
    if (cJSON_IsNumber(vol) && vol->valuedouble >= 0 && vol->valuedouble <= 100) out->volume = (int)vol->valuedouble;
    return true;
}

bool np_same_track(const np_info_t *a, const np_info_t *b)
{
    return a->state == b->state && strcmp(a->title, b->title) == 0 && strcmp(a->artist, b->artist) == 0 &&
           strcmp(a->album, b->album) == 0 && strcmp(a->source, b->source) == 0;
}

static long long clamp_pos(long long p, long long dur)
{
    if (p < 0) return 0;
    return dur > 0 && p > dur ? dur : p;
}

long long np_position_ms(const np_info_t *np, double offset_s, double now_s)
{
    long long p = np->position_ms;
    if (np->state == NP_PLAYING && np->position_at > 0)
        p += (long long)llround((now_s + offset_s) * 1000.0) - np->position_at;
    return clamp_pos(p, np->duration_ms);
}

long long np_anchor_at(const np_anchor_t *a, double now)
{
    if (!a->valid) return 0;
    long long p = a->pos_ms;
    if (a->playing) p += (long long)llround((now - a->t) * 1000.0);
    return clamp_pos(p, a->dur_ms);
}

void np_anchor_sync(np_anchor_t *a, bool same_track, bool playing, long long pos_ms, long long dur_ms, double now)
{
    if (a->valid && same_track && a->playing == playing && a->dur_ms == dur_ms) {
        long long cur = np_anchor_at(a, now);
        long long d = pos_ms - cur;
        if (d > -1500 && d < 1500) return;
    }
    *a = (np_anchor_t){.valid = true, .playing = playing, .t = now, .pos_ms = pos_ms, .dur_ms = dur_ms};
}

int np_format_time(long long ms, char *buf, size_t cap)
{
    long long s = ms > 0 ? ms / 1000 : 0;
    if (s >= 3600) return snprintf(buf, cap, "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
    return snprintf(buf, cap, "%lld:%02lld", s / 60, s % 60);
}

static const char *const LATIN[192] = {
    "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
    "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y",
    "A", "a", "A", "a", "A", "a", "C", "c", "C", "c", "C", "c", "C", "c", "D", "d",
    "D", "d", "E", "e", "E", "e", "E", "e", "E", "e", "E", "e", "G", "g", "G", "g",
    "G", "g", "G", "g", "H", "h", "H", "h", "I", "i", "I", "i", "I", "i", "I", "i",
    "I", "i", "IJ", "ij", "J", "j", "K", "k", "k", "L", "l", "L", "l", "L", "l", "L",
    "l", "L", "l", "N", "n", "N", "n", "N", "n", "n", "N", "n", "O", "o", "O", "o",
    "O", "o", "OE", "oe", "R", "r", "R", "r", "R", "r", "S", "s", "S", "s", "S", "s",
    "S", "s", "T", "t", "T", "t", "T", "t", "U", "u", "U", "u", "U", "u", "U", "u",
    "U", "u", "U", "u", "W", "w", "Y", "y", "Y", "Z", "z", "Z", "z", "Z", "z", "s",
};

static const char *const CYR_UPPER[32] = {
    "A", "B", "V", "G", "D", "E", "Zh", "Z", "I", "Y", "K", "L", "M", "N", "O", "P",
    "R", "S", "T", "U", "F", "Kh", "Ts", "Ch", "Sh", "Shch", "", "Y", "", "E", "Yu", "Ya",
};
static const char *const CYR_LOWER[32] = {
    "a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p",
    "r", "s", "t", "u", "f", "kh", "ts", "ch", "sh", "shch", "", "y", "", "e", "yu", "ya",
};

static const char *fold_cp(unsigned cp)
{
    if (cp >= 0xC0 && cp <= 0x17F) return LATIN[cp - 0xC0];
    if (cp >= 0x410 && cp <= 0x42F) return CYR_UPPER[cp - 0x410];
    if (cp >= 0x430 && cp <= 0x44F) return CYR_LOWER[cp - 0x430];
    switch (cp) {
    case 0x401: return "Yo";
    case 0x451: return "yo";
    case 0x404: return "Ye";
    case 0x454: return "ye";
    case 0x406: return "I";
    case 0x456: return "i";
    case 0x407: return "Yi";
    case 0x457: return "yi";
    case 0x490: return "G";
    case 0x491: return "g";
    case 0x40E: return "U";
    case 0x45E: return "u";
    case 0xA0: return " ";
    case 0xA1: return "!";
    case 0xBF: return "?";
    case 0xAB: case 0xBB: return "\"";
    case 0xB7: case 0x2022: case 0x2027: return ".";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return "-";
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033: return "\"";
    case 0x2026: return "...";
    case 0x200B: case 0x200C: case 0x200D: case 0xFEFF: return "";
    default: break;
    }
    if (cp >= 0x2000 && cp <= 0x200A) return " ";
    if (cp >= 0x300 && cp <= 0x36F) return "";
    return "?";
}

void np_text_fold(const char *in, char *out, size_t cap)
{
    if (!cap) return;
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p) {
        unsigned cp;
        int len;
        if (p[0] < 0x80) {
            cp = p[0];
            len = 1;
        } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
            len = 2;
        } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
            len = 3;
        } else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            cp = 0x10000;
            len = 4;
        } else {
            cp = 0xFFFD;
            len = 1;
        }
        p += len;
        char one[2] = {(char)cp, 0};
        const char *s = cp < 0x80 ? (cp < 0x20 || cp == 0x7F ? " " : one) : fold_cp(cp);
        size_t n = strlen(s);
        if (o + n > cap - 1) break;
        memcpy(out + o, s, n);
        o += n;
    }
    out[o] = 0;
}

static const int ART_PX[NP_ART_KINDS] = {NP_BACKDROP_PX, NP_ALBUM_PX, NP_ARTIST_PX};
static const char *const ART_NAME[NP_ART_KINDS] = {"backdrop", "album", "artist"};

int np_art_px(np_art_kind_t k) { return (unsigned)k < NP_ART_KINDS ? ART_PX[k] : 0; }
const char *np_art_kind_name(np_art_kind_t k) { return (unsigned)k < NP_ART_KINDS ? ART_NAME[k] : ""; }

void np_art_plan(const np_info_t *np, np_art_plan_t *out)
{
    memset(out, 0, sizeof *out);
    if (np->state == NP_NONE || !np->art_version[0] || !(np->album_art || np->artist_art)) return;
    strcpy(out->version, np->art_version);
    out->want[NP_ART_BACKDROP] = true;
    out->want[NP_ART_ALBUM] = np->album_art;
    out->want[NP_ART_ARTIST] = np->artist_art;
}

bool np_art_version_ok(const char *v)
{
    size_t n = strlen(v);
    if (n == 0 || n > NP_ARTV_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = v[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

bool np_art_url(const char *base, np_art_kind_t k, const char *version, char *out, size_t cap)
{
    if ((unsigned)k >= NP_ART_KINDS || !np_art_version_ok(version)) return false;
    int n = snprintf(out, cap, "%s/v1/nowplaying/art?kind=%s&size=%d&v=%s", base, ART_NAME[k], ART_PX[k], version);
    return n > 0 && (size_t)n < cap;
}

int np_art_retry_ms(int failures)
{
    if (failures <= 0) return 0;
    int ms = 5000;
    for (int i = 1; i < failures && ms < 60000; i++) ms *= 2;
    return ms > 60000 ? 60000 : ms;
}
