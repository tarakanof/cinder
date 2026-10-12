#include "demo.h"

#include <stdio.h>
#include <string.h>

static const char *const PAGE_IDS[DEMO_PAGES] = {"bot", "pomodoro", "weather", "nowplaying"};
static const char *const MOMENTS[] = {"glance", "night", "frost", "heat", "wind", "afterrain", "snow", "rain", "storm"};
static const char *const FACES[] = {"temp", "rain", "cast", "wind", "days", "sun", "air"};

#define N_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int find(const char *const *names, int n, const char *s, size_t len)
{
    for (int i = 0; i < n; i++)
        if (strlen(names[i]) == len && strncmp(names[i], s, len) == 0) return i;
    return -1;
}

bool demo_act_parse(const char *s, demo_act_t *out)
{
    *out = (demo_act_t){.kind = DEMO_ACT_NONE};
    if (!s) return false;
    if (strcmp(s, "hop") == 0) out->kind = DEMO_ACT_HOP;
    else if (strcmp(s, "pop") == 0) out->kind = DEMO_ACT_POP;
    else if (strcmp(s, "blink") == 0) out->kind = DEMO_ACT_BLINK;
    else if (strcmp(s, "sleepy") == 0) out->kind = DEMO_ACT_SLEEPY;
    else if (strcmp(s, "chase:full") == 0) out->kind = DEMO_ACT_CHASE;
    else if (strcmp(s, "chase:half") == 0) {
        out->kind = DEMO_ACT_CHASE;
        out->chase_style = 1;
    } else if (strncmp(s, "moment:", 7) == 0) {
        const char *k = s + 7, *c = strchr(k, ':');
        int kind = c ? find(MOMENTS, N_OF(MOMENTS), k, (size_t)(c - k)) : -1;
        if (kind < 0 || (strcmp(c + 1, "full") != 0 && strcmp(c + 1, "quiet") != 0)) return false;
        out->kind = DEMO_ACT_MOMENT;
        out->moment = kind + 1;
        out->quiet = strcmp(c + 1, "quiet") == 0;
    }
    return out->kind != DEMO_ACT_NONE;
}

bool demo_page_parse(const char *s, demo_page_t *page, int *face)
{
    if (!s) return false;
    *face = -1;
    const char *c = strchr(s, ':');
    int p = find(PAGE_IDS, DEMO_PAGES, s, c ? (size_t)(c - s) : strlen(s));
    if (p < 0) return false;
    if (c) {
        if (p != DEMO_PAGE_WEATHER) return false;
        *face = find(FACES, N_OF(FACES), c + 1, strlen(c + 1));
        if (*face < 0) return false;
    }
    *page = (demo_page_t)p;
    return true;
}

const char *demo_page_id(demo_page_t p) { return (unsigned)p < DEMO_PAGES ? PAGE_IDS[p] : PAGE_IDS[0]; }

bool demo_block_parse(const cJSON *d, demo_block_t *out)
{
    memset(out, 0, sizeof *out);
    out->face = -1;
    if (!cJSON_IsObject(d)) return false;
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(d, "id"));
    if (!id || !id[0] || strlen(id) > DEMO_ID_MAX) return false;
    if (!demo_page_parse(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(d, "page")), &out->page, &out->face))
        return false;
    snprintf(out->id, sizeof out->id, "%s", id);
    demo_act_parse(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(d, "act")), &out->act);
    out->has_view = cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(d, "view"));
    out->offline = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(d, "offline"));
    out->hold = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(d, "hold"));
    out->present = true;
    return true;
}

void demo_init(demo_t *d) { memset(d, 0, sizeof *d); }

static bool seen(const demo_t *d, const char *id)
{
    for (int i = 0; i < DEMO_SEEN; i++)
        if (d->seen[i][0] && strcmp(d->seen[i], id) == 0) return true;
    return false;
}

static bool natural(demo_act_kind_t k) { return k != DEMO_ACT_NONE && k != DEMO_ACT_SLEEPY; }

static void end(demo_t *d)
{
    d->phase = DEMO_DONE;
    d->gen++;
}

demo_ev_t demo_offer(demo_t *d, const demo_block_t *b, double t)
{
    bool was = d->phase == DEMO_PLAYING;
    if (!b || !b->present) {
        d->report = false;
        if (!was) return DEMO_EV_NONE;
        end(d);
        return DEMO_EV_STOP;
    }
    d->report = true;
    if (d->phase != DEMO_IDLE && strcmp(d->cur.id, b->id) == 0) return DEMO_EV_NONE;
    d->cur = *b;
    if (seen(d, b->id)) {
        d->phase = DEMO_DONE;
        if (!was) return DEMO_EV_NONE;
        d->gen++;
        return DEMO_EV_STOP;
    }
    snprintf(d->seen[d->seen_next], sizeof d->seen[0], "%s", b->id);
    d->seen_next = (d->seen_next + 1) % DEMO_SEEN;
    d->phase = DEMO_PLAYING;
    d->gen++;
    d->ready = d->taken = d->act_done = false;
    d->start = t;
    d->min_until = d->max_until = t + DEMO_READY_MAX_S;
    return DEMO_EV_START;
}

bool demo_tick(demo_t *d, double t)
{
    if (d->phase != DEMO_PLAYING) return false;
    bool act_over = !natural(d->cur.act.kind) || d->act_done;
    if (!d->ready) {
        if (t < d->max_until) return false;
    } else if (t < d->max_until && (d->cur.hold || t < d->min_until || !act_over)) {
        return false;
    }
    end(d);
    return true;
}

bool demo_input(demo_t *d)
{
    if (d->phase != DEMO_PLAYING) return false;
    end(d);
    return true;
}

void demo_act_finished(demo_t *d, unsigned gen)
{
    if (d->phase == DEMO_PLAYING && d->gen == gen) d->act_done = true;
}

void demo_ready(demo_t *d, double t)
{
    if (d->phase != DEMO_PLAYING || d->ready) return;
    d->ready = true;
    bool act = natural(d->cur.act.kind);
    d->min_until = t + (act && !d->cur.has_view ? 0 : DEMO_VIEW_S);
    d->max_until = t + (d->cur.hold ? DEMO_HOLD_S : act ? DEMO_ACT_MAX_S : DEMO_VIEW_S);
}

bool demo_take_start(demo_t *d, demo_block_t *out)
{
    if (d->phase != DEMO_PLAYING || !d->ready || d->taken) return false;
    d->taken = true;
    *out = d->cur;
    return true;
}

bool demo_playing(const demo_t *d) { return d->phase == DEMO_PLAYING; }

bool demo_view_wanted(const demo_t *d) { return d->phase == DEMO_PLAYING && d->cur.has_view; }

static bool unreserved(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.' || c == '~';
}

static bool put(char *out, size_t cap, size_t *n, const char *s)
{
    size_t l = strlen(s);
    if (*n + l >= cap) return false;
    memcpy(out + *n, s, l + 1);
    *n += l;
    return true;
}

bool demo_query(const demo_t *d, char *out, size_t cap)
{
    if (!cap) return false;
    out[0] = 0;
    if (!d->report || d->phase == DEMO_IDLE) return true;
    static const char HEX[] = "0123456789ABCDEF";
    size_t n = 0;
    bool ok = put(out, cap, &n, "demo_id=");
    for (const unsigned char *p = (const unsigned char *)d->cur.id; ok && *p; p++) {
        char e[4] = {(char)*p, 0};
        if (!unreserved(*p)) {
            e[0] = '%';
            e[1] = HEX[*p >> 4];
            e[2] = HEX[*p & 15];
        }
        ok = put(out, cap, &n, e);
    }
    ok = ok && put(out, cap, &n, d->phase == DEMO_PLAYING ? "&demo_state=playing" : "&demo_state=done");
    if (!ok) out[0] = 0;
    return ok;
}

int demo_mood(const demo_t *d, int real, int view_mood, int frozen)
{
    if (d->phase != DEMO_PLAYING) return real;
    if (d->cur.has_view && view_mood >= 0) return view_mood;
    return frozen >= 0 ? frozen : real;
}

bool demo_quiet_dim(bool playing, bool real_quiet) { return real_quiet && !playing; }

bool demo_calm(bool playing, bool view_on, bool scene_quiet, bool real_quiet, bool calm_setting)
{
    if (view_on) return scene_quiet;
    return !playing && real_quiet && calm_setting;
}

demo_gate_act_t demo_gate(demo_gate_t *g, const demo_gate_in_t *in, double t)
{
    if (in->input) g->quiet_until = t + DEMO_GATE_QUIET_S;
    if (in->input && (in->playing || in->view_on)) g->on = true;
    if (in->playing) return in->input ? DEMO_GATE_END : DEMO_GATE_DROP;
    if (g->on && (in->input || in->held || in->view_on || t < g->quiet_until)) return DEMO_GATE_DROP;
    g->on = false;
    return in->view_on ? DEMO_GATE_DROP : DEMO_GATE_PASS;
}

void demo_gate_start(demo_gate_t *g) { g->on = false; }
