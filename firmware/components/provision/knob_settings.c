#include "knob_settings.h"

#include <string.h>

#include "cJSON.h"
#include "knob_rotation.h"

static const char *const DEFAULT_PAGES[] = {"bot", "day", "weather"};

void knob_settings_defaults(knob_settings_t *ks)
{
    memset(ks, 0, sizeof *ks);
    ks->follow_ember = true;
    ks->level = 153;
    ks->floor = 10;
    ks->startup = 153;
    ks->n_pages = 3;
    for (int i = 0; i < 3; i++) {
        strcpy(ks->pages[i].id, DEFAULT_PAGES[i]);
        ks->pages[i].on = true;
    }
    strcpy(ks->home, "bot");
    ks->poll_ms = 2000;
    ks->sleepy_after_s = 300;
    ks->demo_hold_s = 20;
    ks->source_label = true;
    ks->working_ring = true;
    ks->weather_moments = true;
    ks->weather_every_min = KS_WEATHER_EVERY_MIN_DEFAULT;
    ks->fast_link = true;
    ks->swipe_pages = true;
    ks->stats_interval_s = KS_STATS_INTERVAL_S_DEFAULT;
    ks->live_interval_s = KS_LIVE_INTERVAL_S_DEFAULT;
    ks->quiet_calm = true;
    ks->quiet_dim = KS_QUIET_DIM_DEFAULT;
    ks->idle_dim_level = KS_IDLE_DIM_DEFAULT;
    ks->n_wx_faces = KS_WX_FACES;
    for (int i = 0; i < KS_WX_FACES; i++) ks->wx_faces[i] = (uint8_t)i;
    ks->pomo_focus_rgb = KS_POMO_FOCUS_DEFAULT;
    ks->pomo_break_rgb = KS_POMO_BREAK_DEFAULT;
    ks->np_dim_after_s = KS_NP_DIM_AFTER_S_DEFAULT;
}

static void num(const cJSON *obj, const char *key, int lo, int hi, int *dst)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(v)) return;
    double d = v->valuedouble;
    *dst = d < lo ? lo : d > hi ? hi : (int)d;
}

static bool page_id_valid(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > KS_PAGE_ID_MAX || s[0] < 'a' || s[0] > 'z') return false;
    for (size_t i = 1; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

static void parse_pages(const cJSON *arr, knob_settings_t *ks)
{
    if (!cJSON_IsArray(arr)) return;
    knob_settings_t tmp;
    memset(&tmp, 0, sizeof tmp);
    const cJSON *p;
    cJSON_ArrayForEach(p, arr)
    {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "id"));
        if (!id || !page_id_valid(id) || tmp.n_pages == KS_MAX_PAGES) continue;
        bool dup = false;
        for (int i = 0; i < tmp.n_pages; i++) dup |= strcmp(tmp.pages[i].id, id) == 0;
        if (dup) continue;
        ks_page_t *pg = &tmp.pages[tmp.n_pages++];
        strcpy(pg->id, id);
        pg->on = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(p, "on"));
    }
    if (tmp.n_pages == 0) return;
    ks->n_pages = tmp.n_pages;
    memcpy(ks->pages, tmp.pages, sizeof tmp.pages);
}

static const char *const WX_FACE_NAMES[KS_WX_FACES] = {"temp", "rain", "cast", "wind", "days", "sun", "air"};

static void parse_wx_faces(const cJSON *arr, knob_settings_t *ks)
{
    if (!cJSON_IsArray(arr)) return;
    uint8_t ids[KS_WX_FACES];
    int n = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, arr)
    {
        const char *name = cJSON_GetStringValue(e);
        for (int f = 0; name && f < KS_WX_FACES; f++) {
            if (strcmp(name, WX_FACE_NAMES[f]) != 0) continue;
            bool dup = false;
            for (int i = 0; i < n; i++) dup |= ids[i] == f;
            if (!dup) ids[n++] = (uint8_t)f;
        }
    }
    if (n == 0) return;
    ks->n_wx_faces = (uint8_t)n;
    memcpy(ks->wx_faces, ids, (size_t)n);
}

static void color(const cJSON *obj, const char *key, uint32_t *dst)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj, key));
    if (!s || s[0] != '#' || strlen(s) != 7) return;
    uint32_t rgb = 0;
    for (int i = 1; i < 7; i++) {
        char c = s[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0) return;
        rgb = rgb << 4 | (uint32_t)v;
    }
    *dst = rgb;
}

static int parse_rotation(const cJSON *root)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "display"), "rotation");
    return cJSON_IsNumber(v) && v->valuedouble == (double)v->valueint ? kr_effective(v->valueint) : 0;
}

bool knob_settings_parse(const char *json, knob_settings_t *ks)
{
    knob_settings_defaults(ks);
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    if (cJSON_IsObject(b)) {
        const cJSON *f = cJSON_GetObjectItemCaseSensitive(b, "follow_ember");
        if (cJSON_IsBool(f)) ks->follow_ember = cJSON_IsTrue(f);
        int level = ks->level, floor = ks->floor, startup = ks->startup;
        num(b, "level", 0, 255, &level);
        num(b, "floor", 1, 255, &floor);
        num(b, "startup", 0, 255, &startup);
        if (level < 1) level = 1;
        if (floor > level) floor = level;
        if (startup < floor) startup = floor;
        ks->level = (uint8_t)level;
        ks->floor = (uint8_t)floor;
        ks->startup = (uint8_t)startup;
    }
    parse_pages(cJSON_GetObjectItemCaseSensitive(root, "pages"), ks);
    const char *home = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "home"));
    if (home && page_id_valid(home)) strcpy(ks->home, home);
    const cJSON *sw = cJSON_GetObjectItemCaseSensitive(root, "swipe_pages");
    if (cJSON_IsBool(sw)) ks->swipe_pages = cJSON_IsTrue(sw);
    const char *diag = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "diagnostics"));
    if (diag && strcmp(diag, "basic") == 0) ks->diagnostics = KS_DIAG_BASIC;
    else if (diag && strcmp(diag, "full") == 0) ks->diagnostics = KS_DIAG_FULL;
    num(root, "poll_ms", 1000, 10000, &ks->poll_ms);
    num(root, "stats_interval_s", KS_STATS_INTERVAL_S_MIN, KS_STATS_INTERVAL_S_MAX, &ks->stats_interval_s);
    num(root, "live_interval_s", KS_LIVE_INTERVAL_S_MIN, KS_LIVE_INTERVAL_S_MAX, &ks->live_interval_s);
    const cJSON *bot = cJSON_GetObjectItemCaseSensitive(root, "bot");
    if (cJSON_IsObject(bot)) {
        num(bot, "sleepy_after_s", 0, 86400, &ks->sleepy_after_s);
        num(bot, "demo_hold_s", 1, 600, &ks->demo_hold_s);
        const cJSON *sl = cJSON_GetObjectItemCaseSensitive(bot, "source_label");
        if (cJSON_IsBool(sl)) ks->source_label = cJSON_IsTrue(sl);
        const cJSON *wr = cJSON_GetObjectItemCaseSensitive(bot, "working_ring");
        if (cJSON_IsBool(wr)) ks->working_ring = cJSON_IsTrue(wr);
        const cJSON *wm = cJSON_GetObjectItemCaseSensitive(bot, "weather_moments");
        if (cJSON_IsBool(wm)) ks->weather_moments = cJSON_IsTrue(wm);
        num(bot, "weather_every_min", 0, KS_WEATHER_EVERY_MIN_MAX, &ks->weather_every_min);
    }
    const cJSON *disp = cJSON_GetObjectItemCaseSensitive(root, "display");
    if (cJSON_IsObject(disp)) {
        const cJSON *fl = cJSON_GetObjectItemCaseSensitive(disp, "fast_link");
        if (cJSON_IsBool(fl)) ks->fast_link = cJSON_IsTrue(fl);
    }
    ks->rotation = parse_rotation(root);
    const cJSON *q = cJSON_GetObjectItemCaseSensitive(root, "quiet");
    if (cJSON_IsObject(q)) {
        const cJSON *calm = cJSON_GetObjectItemCaseSensitive(q, "calm");
        if (cJSON_IsBool(calm)) ks->quiet_calm = cJSON_IsTrue(calm);
        int dim = ks->quiet_dim;
        num(q, "dim_level", 1, 255, &dim);
        ks->quiet_dim = (uint8_t)dim;
    }
    const cJSON *idle = cJSON_GetObjectItemCaseSensitive(root, "idle");
    if (cJSON_IsObject(idle)) {
        num(idle, "dim_after_s", 0, KS_IDLE_AFTER_S_MAX, &ks->idle_dim_after_s);
        if (ks->idle_dim_after_s > 0 && ks->idle_dim_after_s < KS_IDLE_DIM_AFTER_S_MIN) ks->idle_dim_after_s = KS_IDLE_DIM_AFTER_S_MIN;
        num(idle, "off_after_s", 0, KS_IDLE_AFTER_S_MAX, &ks->idle_off_after_s);
        if (ks->idle_off_after_s > 0 && ks->idle_off_after_s < KS_IDLE_OFF_AFTER_S_MIN) ks->idle_off_after_s = KS_IDLE_OFF_AFTER_S_MIN;
        int level = ks->idle_dim_level;
        num(idle, "dim_level", 1, 255, &level);
        ks->idle_dim_level = (uint8_t)level;
    }
    const cJSON *apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
    if (cJSON_IsObject(apps)) {
        const cJSON *pomo = cJSON_GetObjectItemCaseSensitive(apps, "pomodoro");
        if (cJSON_IsObject(pomo)) {
            color(pomo, "focus_color", &ks->pomo_focus_rgb);
            color(pomo, "break_color", &ks->pomo_break_rgb);
        }
        const cJSON *wx = cJSON_GetObjectItemCaseSensitive(apps, "weather");
        if (cJSON_IsObject(wx)) parse_wx_faces(cJSON_GetObjectItemCaseSensitive(wx, "faces"), ks);
        const cJSON *np = cJSON_GetObjectItemCaseSensitive(apps, "nowplaying");
        if (cJSON_IsObject(np)) num(np, "dim_after_s", KS_NP_DIM_AFTER_S_MIN, KS_NP_DIM_AFTER_S_MAX, &ks->np_dim_after_s);
    }
    cJSON_Delete(root);
    return true;
}

int knob_settings_page_order(const knob_settings_t *ks, const char *const *known, int n_known, int *out, int *home)
{
    int n = 0;
    *home = 0;
    for (int i = 0; i < ks->n_pages; i++) {
        if (!ks->pages[i].on) continue;
        for (int k = 0; k < n_known; k++) {
            if (strcmp(ks->pages[i].id, known[k]) != 0) continue;
            if (strcmp(known[k], ks->home) == 0) *home = n;
            out[n++] = k;
        }
    }
    if (n == 0) {
        for (int k = 0; k < n_known; k++) {
            if (strcmp(known[k], ks->home) == 0) *home = n;
            out[n++] = k;
        }
    }
    return n;
}
