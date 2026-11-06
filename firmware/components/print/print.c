#include "print.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const cJSON *item(const cJSON *o, const char *k) { return cJSON_GetObjectItemCaseSensitive(o, k); }

static const char *const PHASES[] = {
    [PRINT_OFFLINE] = "offline",     [PRINT_IDLE] = "idle",         [PRINT_HEATING] = "heating",
    [PRINT_PREPARING] = "preparing", [PRINT_PRINTING] = "printing", [PRINT_FINISHING] = "finishing",
    [PRINT_PAUSED] = "paused",       [PRINT_ATTENTION] = "attention", [PRINT_FINISHED] = "finished",
    [PRINT_STOPPED] = "stopped",     [PRINT_ERROR] = "error",
};
#define N_PHASES ((int)(sizeof PHASES / sizeof PHASES[0]))

print_phase_t print_phase_from_wire(const char *s)
{
    for (int i = 1; s && i < N_PHASES; i++)
        if (strcmp(s, PHASES[i]) == 0) return (print_phase_t)i;
    return PRINT_UNKNOWN;
}

const char *print_phase_name(print_phase_t p) { return p > PRINT_UNKNOWN && (int)p < N_PHASES ? PHASES[p] : "unknown"; }

print_ev_kind_t print_ev_kind_from_wire(const char *s)
{
    static const char *const K[] = {[PRINT_EV_STARTED] = "started", [PRINT_EV_ATTENTION] = "attention",
                                    [PRINT_EV_FINISHED] = "finished", [PRINT_EV_STOPPED] = "stopped",
                                    [PRINT_EV_ERROR] = "error"};
    if (!s) return PRINT_EV_NONE;
    for (int i = PRINT_EV_STARTED; i <= PRINT_EV_ERROR; i++)
        if (strcmp(s, K[i]) == 0) return (print_ev_kind_t)i;
    return PRINT_EV_UNKNOWN;
}

static bool whole_in(const cJSON *v, double lo, double hi, long long *out)
{
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= lo && v->valuedouble <= hi) ||
        v->valuedouble != (double)(long long)v->valuedouble)
        return false;
    *out = (long long)v->valuedouble;
    return true;
}

static bool int_key(const cJSON *o, const char *k, int lo, int hi, int *out)
{
    long long v;
    if (!whole_in(item(o, k), lo, hi, &v)) return false;
    *out = (int)v;
    return true;
}

static long long unix_key(const cJSON *v)
{
    long long t;
    return whole_in(v, 1, 1e13, &t) ? t : 0;
}

static void text_key(const cJSON *o, const char *k, char *dst, size_t cap)
{
    const cJSON *v = item(o, k);
    dst[0] = 0;
    if (!cJSON_IsString(v) || !v->valuestring) return;
    size_t n = strlen(v->valuestring);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)v->valuestring[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, v->valuestring, n);
    dst[n] = 0;
}

static bool thumb_ok(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > PRINT_THUMBV_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' ||
              c == '.'))
            return false;
    }
    return true;
}

static void parse_thumb(const cJSON *o, print_block_t *out)
{
    const cJSON *v = item(o, "thumb_version"), *rows = item(o, "thumb_rows");
    long long top, bottom;
    if (!cJSON_IsString(v) || !v->valuestring || !thumb_ok(v->valuestring)) return;
    if (!cJSON_IsArray(rows) || cJSON_GetArraySize(rows) != 2 ||
        !whole_in(cJSON_GetArrayItem(rows, 0), PRINT_PIC_Y0, PRINT_PIC_Y1, &top) ||
        !whole_in(cJSON_GetArrayItem(rows, 1), PRINT_PIC_Y0, PRINT_PIC_Y1, &bottom) || top >= bottom)
        return;
    strcpy(out->thumb_version, v->valuestring);
    out->thumb_top = (int)top;
    out->thumb_bottom = (int)bottom;
}

static void parse_event(const cJSON *e, print_event_t *out)
{
    long long id;
    if (!cJSON_IsObject(e) || !whole_in(item(e, "id"), 1, 9007199254740992.0, &id)) return;
    const cJSON *k = item(e, "kind");
    out->id = id;
    out->kind = cJSON_IsString(k) ? print_ev_kind_from_wire(k->valuestring) : PRINT_EV_UNKNOWN;
    out->at = unix_key(item(e, "at"));
}

bool print_parse(const cJSON *o, print_block_t *out)
{
    memset(out, 0, sizeof *out);
    const cJSON *ph = cJSON_IsObject(o) ? item(o, "phase") : NULL;
    if (!cJSON_IsString(ph) || !ph->valuestring) return false;
    out->phase = print_phase_from_wire(ph->valuestring);
    out->has_progress = int_key(o, "progress", 0, 100, &out->progress);
    out->ends_at = unix_key(item(o, "ends_at"));
    out->started_at = unix_key(item(o, "started_at"));
    out->change_at = unix_key(item(o, "change_at"));
    int t[4];
    out->has_temps = int_key(o, "nozzle", 0, PRINT_TEMP_MAX, &t[0]) && int_key(o, "nozzle_target", 0, PRINT_TEMP_MAX, &t[1]) &&
                     int_key(o, "bed", 0, PRINT_TEMP_MAX, &t[2]) && int_key(o, "bed_target", 0, PRINT_TEMP_MAX, &t[3]);
    if (out->has_temps) {
        out->nozzle = t[0];
        out->nozzle_target = t[1];
        out->bed = t[2];
        out->bed_target = t[3];
    }
    text_key(o, "name", out->name, sizeof out->name);
    text_key(o, "material", out->material, sizeof out->material);
    text_key(o, "printer", out->printer, sizeof out->printer);
    parse_thumb(o, out);
    parse_event(item(o, "event"), &out->event);
    return true;
}

long long print_left_s(const print_block_t *b, double now)
{
    if (!b || !b->ends_at || now <= 0) return -1;
    double left = (double)b->ends_at - now;
    return left > 0 ? (long long)ceil(left) : 0;
}

int print_left_min(const print_block_t *b, double now)
{
    long long s = print_left_s(b, now);
    return s < 0 ? -1 : (int)((s + 59) / 60);
}

void print_left_text(int min, char *val, size_t vcap, const char **unit)
{
    if (min < 0) min = 0;
    if (min >= 60) {
        snprintf(val, vcap, "%d:%02d", min / 60, min % 60);
        *unit = "h left";
    } else {
        snprintf(val, vcap, "%d", min);
        *unit = "min left";
    }
}

void print_hhmm(long long t, int utc_offset, char *buf, size_t cap)
{
    long long d = (t + utc_offset) % 86400;
    if (d < 0) d += 86400;
    snprintf(buf, cap, "%02d:%02d", (int)(d / 3600), (int)(d / 60 % 60));
}

void print_duration_text(long long s, char *buf, size_t cap)
{
    long long m = s < 0 ? 0 : (s + 30) / 60;
    if (m >= 60) snprintf(buf, cap, "%lld h %02lld m", m / 60, m % 60);
    else snprintf(buf, cap, "%lld min", m);
}

long long print_took_s(const print_block_t *b)
{
    if (b->event.kind != PRINT_EV_FINISHED || !b->started_at || b->event.at <= b->started_at) return -1;
    return b->event.at - b->started_at;
}

print_look_t print_look(const print_block_t *b)
{
    if (!b) return PRINT_LOOK_NONE;
    switch (b->phase) {
    case PRINT_OFFLINE: return PRINT_LOOK_OFFLINE;
    case PRINT_UNKNOWN:
    case PRINT_IDLE: return PRINT_LOOK_IDLE;
    case PRINT_HEATING: return PRINT_LOOK_HEATING;
    case PRINT_PREPARING:
    case PRINT_PRINTING: return PRINT_LOOK_PROGRESS;
    case PRINT_FINISHING: return PRINT_LOOK_FINISHING;
    case PRINT_PAUSED: return PRINT_LOOK_PAUSED;
    case PRINT_ATTENTION: return PRINT_LOOK_ATTENTION;
    case PRINT_FINISHED: return PRINT_LOOK_DONE;
    case PRINT_STOPPED: return PRINT_LOOK_STOPPED;
    case PRINT_ERROR: return PRINT_LOOK_ERROR;
    }
    return PRINT_LOOK_IDLE;
}

print_face_t print_face_open(const print_block_t *b)
{
    (void)b;
    return PRINT_FACE_K1;
}

void print_faces_open(print_faces_t *f) { f->pick = PRINT_FACE_AUTO; }

print_face_t print_faces_shown(const print_faces_t *f, const print_block_t *b)
{
    return f->pick >= 0 && f->pick < PRINT_FACES ? (print_face_t)f->pick : print_face_open(b);
}

bool print_faces_turn(print_faces_t *f, const print_block_t *b, int detents)
{
    if (!detents) return false;
    int base = print_faces_shown(f, b);
    int next = (base + detents % PRINT_FACES + PRINT_FACES) % PRINT_FACES;
    f->pick = next;
    return next != base;
}

int print_clip_row(int top, int bottom, int progress)
{
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    return bottom - (bottom - top) * progress / 100;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static float heat_k(const print_block_t *b)
{
    if (!b->has_temps) return 0;
    float n = b->nozzle_target > 25 ? clamp01((b->nozzle - 25.0f) / (b->nozzle_target - 25.0f)) : 0;
    float d = b->bed_target > 22 ? clamp01((b->bed - 22.0f) / (b->bed_target - 22.0f)) : 0;
    return n * 0.6f + d * 0.4f;
}

float print_ring_frac(const print_block_t *b)
{
    switch (print_look(b)) {
    case PRINT_LOOK_NONE:
    case PRINT_LOOK_OFFLINE:
    case PRINT_LOOK_IDLE: return 0;
    case PRINT_LOOK_HEATING: return heat_k(b);
    case PRINT_LOOK_FINISHING:
    case PRINT_LOOK_DONE: return 1;
    default: return b->has_progress ? b->progress / 100.0f : 0;
    }
}

uint32_t print_mix(uint32_t a, uint32_t b, float k)
{
    uint32_t out = 0;
    for (int s = 0; s <= 16; s += 8) {
        float x = (float)((a >> s) & 0xFF), y = (float)((b >> s) & 0xFF);
        int v = (int)lroundf(x + (y - x) * k);
        out |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v) << s;
    }
    return out;
}

uint32_t print_scale(uint32_t c, float k) { return print_mix(0, c, k); }

void print_ring_look(print_look_t l, print_ring_look_t *out)
{
    *out = (print_ring_look_t){.col = PRINT_COL_ACCENT, .head = true};
    switch (l) {
    case PRINT_LOOK_HEATING: out->col = PRINT_COL_HEAT; break;
    case PRINT_LOOK_PAUSED: out->col = print_scale(PRINT_COL_ACCENT, 0.5f); out->head = false; break;
    case PRINT_LOOK_ATTENTION: out->col = PRINT_COL_WAIT; out->pulse = true; break;
    case PRINT_LOOK_DONE: out->col = PRINT_COL_DONE; out->head = false; break;
    case PRINT_LOOK_STOPPED: out->col = PRINT_COL_GREY; out->head = false; break;
    case PRINT_LOOK_ERROR: out->col = PRINT_COL_ERR; out->head = false; out->pulse = true; break;
    case PRINT_LOOK_NONE:
    case PRINT_LOOK_OFFLINE:
    case PRINT_LOOK_IDLE: out->col = PRINT_COL_TRACK; out->head = false; break;
    default: break;
    }
}

#define SEP " \xE2\x80\xA2 "
#define CHANGE_SOON_S 600

static void name_line(const print_block_t *b, char *out, size_t cap)
{
    if (b->name[0] && b->material[0]) snprintf(out, cap, "%s" SEP "%s", b->name, b->material);
    else snprintf(out, cap, "%s", b->name[0] ? b->name : b->material);
}

static void word(print_text_t *t, const char *w, uint32_t col)
{
    snprintf(t->word, sizeof t->word, "%s", w);
    t->word_col = col;
}

static void sub(print_text_t *t, uint32_t col, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void sub(print_text_t *t, uint32_t col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->sub, sizeof t->sub, fmt, ap);
    va_end(ap);
    t->sub_col = col;
}

static bool change_soon(const print_block_t *b, double now, int *min)
{
    if (!b->change_at || now <= 0) return false;
    double in = (double)b->change_at - now;
    if (in < 0 || in > CHANGE_SOON_S) return false;
    *min = (int)ceil(in / 60);
    return true;
}

static void progress_text(const print_block_t *b, print_face_t face, double now, bool has_off, int off, print_text_t *t)
{
    int min = print_left_min(b, now);
    if (min < 0) {
        word(t, b->phase == PRINT_PREPARING ? "Preparing" : "Printing", PRINT_COL_ACCENT);
        if (b->has_progress && b->phase == PRINT_PRINTING) sub(t, PRINT_COL_SUB, "%d%% done", b->progress);
        return;
    }
    char left[12], ready[8] = "";
    const char *unit;
    print_left_text(min, left, sizeof left, &unit);
    if (has_off) print_hhmm(b->ends_at, off, ready, sizeof ready);
    int ch;
    bool soon = change_soon(b, now, &ch);
    if (face == PRINT_FACE_K1) {
        snprintf(t->val, sizeof t->val, "%s", left);
        snprintf(t->unit, sizeof t->unit, "%s", unit);
        t->val_col = PRINT_COL_TEXT;
        t->unit_col = PRINT_COL_SUB;
        if (soon) sub(t, PRINT_COL_WAIT, "colour change: %d min", ch);
        else if (ready[0]) sub(t, PRINT_COL_ACCENT, "done %s", ready);
        else {
            char d[16];
            print_duration_text((long long)min * 60, d, sizeof d);
            sub(t, PRINT_COL_ACCENT, "done in %s", d);
        }
        return;
    }
    snprintf(t->val, sizeof t->val, "%d", b->has_progress ? b->progress : 0);
    snprintf(t->unit, sizeof t->unit, "%%");
    t->val_col = PRINT_COL_TEXT;
    t->unit_col = PRINT_COL_SUB;
    if (soon) sub(t, PRINT_COL_WAIT, "change in %d min", ch);
    else if (ready[0]) sub(t, PRINT_COL_ACCENT, "%s %s" SEP "%s", left, min >= 60 ? "left" : "min left", ready);
    else sub(t, PRINT_COL_ACCENT, "%s %s", left, min >= 60 ? "left" : "min left");
}

void print_text(const print_block_t *b, print_face_t face, double now, bool has_off, int utc_offset, print_text_t *out)
{
    memset(out, 0, sizeof *out);
    out->small_col = PRINT_COL_SMALL;
    print_text_t *t = out;
    int p = b && b->has_progress ? b->progress : 0;
    print_look_t l = print_look(b);
    bool k1 = face == PRINT_FACE_K1;
    if (b && k1 && (l == PRINT_LOOK_HEATING || l == PRINT_LOOK_PROGRESS)) name_line(b, t->small, sizeof t->small);
    switch (l) {
    case PRINT_LOOK_NONE: word(t, "Printer off", PRINT_COL_DIMWORD); break;
    case PRINT_LOOK_OFFLINE:
        word(t, "Printer off", PRINT_COL_DIMWORD);
        sub(t, PRINT_COL_DIMWORD, "%s", b->printer);
        break;
    case PRINT_LOOK_IDLE:
        word(t, "Idle", PRINT_COL_SUB);
        sub(t, PRINT_COL_SMALL, "%s", b->printer);
        break;
    case PRINT_LOOK_HEATING:
        if (!b->has_temps) {
            word(t, "Heating", PRINT_COL_HEAT);
            break;
        }
        if (k1) {
            snprintf(t->val, sizeof t->val, "%d\xC2\xB0", b->nozzle);
            snprintf(t->unit, sizeof t->unit, "/ %d\xC2\xB0", b->nozzle_target);
            t->val_col = print_mix(PRINT_COL_TEXT, PRINT_COL_HEAT, 0.5f);
            t->unit_col = PRINT_COL_SUB;
            sub(t, PRINT_COL_HEAT, "heating" SEP "bed %d\xC2\xB0", b->bed);
        } else {
            snprintf(t->word, sizeof t->word, "Heating %d\xC2\xB0", b->nozzle);
            t->word_col = print_mix(PRINT_COL_TEXT, PRINT_COL_HEAT, 0.5f);
            sub(t, PRINT_COL_HEAT, "bed %d\xC2\xB0 / %d\xC2\xB0", b->bed, b->bed_target);
        }
        break;
    case PRINT_LOOK_PROGRESS: progress_text(b, face, now, has_off, utc_offset, t); break;
    case PRINT_LOOK_FINISHING:
        if (k1) {
            snprintf(t->val, sizeof t->val, "100");
            snprintf(t->unit, sizeof t->unit, "%%");
            t->val_col = PRINT_COL_TEXT;
            t->unit_col = PRINT_COL_SUB;
            sub(t, PRINT_COL_ACCENT, "finishing up");
        } else {
            word(t, "Finishing", PRINT_COL_ACCENT);
            sub(t, PRINT_COL_SUB, "cooling the bed");
        }
        break;
    case PRINT_LOOK_PAUSED:
        word(t, "Paused", PRINT_COL_WAIT);
        sub(t, PRINT_COL_SUB, "at %d%%", p);
        break;
    case PRINT_LOOK_ATTENTION:
        word(t, "Check printer", PRINT_COL_WAIT);
        if (p > 0) sub(t, PRINT_COL_SUB, k1 ? "paused" SEP "%d%% done" : "paused at %d%%", p);
        else sub(t, PRINT_COL_SUB, "see the printer");
        break;
    case PRINT_LOOK_DONE: {
        word(t, "Done", PRINT_COL_DONE);
        long long took = print_took_s(b);
        char d[16];
        if (took >= 0) print_duration_text(took, d, sizeof d);
        if (k1 && took >= 0) sub(t, PRINT_COL_SUB, "%s" SEP "take it out", d);
        else if (k1) sub(t, PRINT_COL_SUB, "take it out");
        else if (took >= 0) sub(t, PRINT_COL_SUB, "took %s", d);
        break;
    }
    case PRINT_LOOK_STOPPED:
        word(t, "Stopped", PRINT_COL_SUB);
        sub(t, PRINT_COL_SUB, "at %d%%", p);
        break;
    case PRINT_LOOK_ERROR:
        word(t, "Printer error", PRINT_COL_ERR);
        sub(t, PRINT_COL_SUB, "see the printer");
        break;
    }
}

void print_text_link(print_text_t *t, bool fed, bool offline)
{
    if (!fed) {
        memset(t, 0, sizeof *t);
        word(t, "Connecting", PRINT_COL_DIMWORD);
        t->small_col = PRINT_COL_SMALL;
        return;
    }
    if (!offline) return;
    sub(t, PRINT_COL_SUB, "offline");
    t->small[0] = 0;
}
