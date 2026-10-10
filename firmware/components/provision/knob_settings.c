#include "knob_settings.h"

#include <string.h>

#include "cJSON.h"
#include "knob_rotation.h"

static const char *const DEFAULT_PAGES[] = {"bot", "pomodoro", "weather"};

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
    ks->fast_link = true;
    ks->swipe_pages = true;
    ks->stats_interval_s = KS_STATS_INTERVAL_S_DEFAULT;
    ks->live_interval_s = KS_LIVE_INTERVAL_S_DEFAULT;
    ks->quiet_calm = true;
    ks->quiet_dim = KS_QUIET_DIM_DEFAULT;
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
