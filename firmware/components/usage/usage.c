#include "usage.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "np.h"
#include "wx_clock.h"

static const cJSON *item(const cJSON *o, const char *k) { return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL; }

static bool pct_in(const cJSON *o, int *out)
{
    const cJSON *v = item(o, "pct");
    if (!cJSON_IsNumber(v) || v->valuedouble != v->valuedouble) return false;
    double d = v->valuedouble < 0 ? 0 : v->valuedouble > 100 ? 100 : v->valuedouble;
    *out = (int)(d + 0.5);
    return true;
}

static long long unix_in(const cJSON *o, const char *k)
{
    const cJSON *v = item(o, k);
    if (!cJSON_IsNumber(v) || !(v->valuedouble > 0 && v->valuedouble < 1e13) || v->valuedouble != (double)(long long)v->valuedouble)
        return 0;
    return (long long)v->valuedouble;
}

static void win_in(const cJSON *o, usage_win_t *w)
{
    *w = (usage_win_t){0};
    if (!pct_in(o, &w->pct)) return;
    w->have = true;
    w->resets_at = unix_in(o, "resets_at");
    w->starts_at = unix_in(o, "starts_at");
    if (!w->resets_at || w->starts_at >= w->resets_at) w->starts_at = 0;
}

static void fold(const cJSON *o, const char *k, char *out, size_t cap)
{
    const cJSON *v = item(o, k);
    np_text_fold(cJSON_IsString(v) && v->valuestring ? v->valuestring : "", out, cap);
}

static int tool_in(const cJSON *o)
{
    const char *s = cJSON_GetStringValue(item(o, "tool"));
    if (!s) return USAGE_TOOL_OTHER;
    return strcmp(s, "claude") == 0 ? USAGE_TOOL_CLAUDE : strcmp(s, "codex") == 0 ? USAGE_TOOL_CODEX : USAGE_TOOL_OTHER;
}

bool usage_parse(const cJSON *o, usage_block_t *out)
{
    memset(out, 0, sizeof *out);
    const cJSON *list = item(o, "accounts");
    for (const cJSON *e = cJSON_IsArray(list) ? list->child : NULL; e && out->n < USAGE_MAX; e = e->next) {
        const char *id = cJSON_GetStringValue(item(e, "id"));
        if (!id || !id[0] || strlen(id) > USAGE_ID_MAX) continue;
        usage_acct_t *a = &out->a[out->n++];
        snprintf(a->id, sizeof a->id, "%s", id);
        a->tool = tool_in(e);
        fold(e, "name", a->name, sizeof a->name);
        win_in(item(e, "five_hour"), &a->five);
        win_in(item(e, "seven_day"), &a->week);
        const cJSON *models = item(e, "models");
        for (const cJSON *m = cJSON_IsArray(models) ? models->child : NULL; m && a->n_models < USAGE_MODELS; m = m->next) {
            usage_model_t *md = &a->models[a->n_models];
            if (!cJSON_IsString(item(m, "name")) || !pct_in(m, &md->pct)) continue;
            fold(m, "name", md->name, sizeof md->name);
            if (md->name[strspn(md->name, " ")]) a->n_models++;
        }
        a->stale = cJSON_IsTrue(item(e, "stale"));
        a->updated_at = unix_in(e, "updated_at");
    }
    return out->n > 0;
}

static int five_pct(const usage_acct_t *a) { return a->five.have ? a->five.pct : -1; }

int usage_highest(const usage_block_t *b)
{
    int best = b->n > 0 ? 0 : -1;
    for (int i = 1; i < b->n; i++)
        if (five_pct(&b->a[i]) > five_pct(&b->a[best])) best = i;
    return best;
}

bool usage_hint(const usage_block_t *b, double now)
{
    for (int i = 0; i < b->n; i++) {
        const usage_acct_t *a = &b->a[i];
        bool reset = a->five.resets_at && now > 0 && now >= (double)a->five.resets_at;
        if (!a->stale && !reset && five_pct(a) >= USAGE_HINT_PCT) return true;
    }
    return false;
}

usage_acct_t usage_as_stale(const usage_acct_t *a, long long fed_at)
{
    usage_acct_t s = *a;
    if (!s.stale) s.updated_at = fed_at;
    s.stale = true;
    return s;
}

uint32_t usage_scale_rgb(int pct) { return pct >= 90 ? USAGE_RED : pct >= 70 ? USAGE_AMBER : USAGE_GREEN; }

static void nav_at(usage_nav_t *nav, const usage_block_t *b, int idx)
{
    nav->idx = idx;
    snprintf(nav->id, sizeof nav->id, "%s", b->a[idx].id);
}

void usage_nav_close(usage_nav_t *nav)
{
    nav->open = false;
    nav->id[0] = 0;
}

usage_act_t usage_input(usage_nav_t *nav, const usage_block_t *b, const usage_in_t *in, double t)
{
    if (!nav->open) {
        if (in->taps <= 0 || b->n <= 0) return USAGE_ACT_NONE;
        nav->open = true;
        nav->since = t;
        nav_at(nav, b, usage_highest(b));
        return USAGE_ACT_OPEN;
    }
    if (in->taps || in->pushes || in->longs || in->detents || in->hswipes || in->wakes) nav->since = t;
    if (in->taps > 0 || in->pushes > 0 || in->longs > 0) {
        usage_nav_close(nav);
        return USAGE_ACT_CLOSE;
    }
    int steps = in->detents + in->hswipes;
    if (!steps || b->n <= 0) return USAGE_ACT_NONE;
    int i = (nav->idx + steps) % b->n;
    nav_at(nav, b, i < 0 ? i + b->n : i);
    return USAGE_ACT_STEP;
}

bool usage_nav_sync(usage_nav_t *nav, const usage_block_t *b)
{
    if (!nav->open) return false;
    if (b->n <= 0) {
        usage_nav_close(nav);
        return true;
    }
    for (int i = 0; i < b->n; i++)
        if (strcmp(b->a[i].id, nav->id) == 0) {
            nav->idx = i;
            return false;
        }
    nav_at(nav, b, nav->idx < b->n ? nav->idx : b->n - 1);
    return false;
}

bool usage_nav_tick(usage_nav_t *nav, double t)
{
    if (!nav->open || t - nav->since < USAGE_IDLE_S) return false;
    usage_nav_close(nav);
    return true;
}

void usage_dur(long long s, char *out, size_t cap)
{
    if (s < 60) snprintf(out, cap, "<1 min");
    else if (s < 3600) snprintf(out, cap, "%lld min", s / 60);
    else if (s < 86400) snprintf(out, cap, "%lldh %02lldm", s / 3600, s / 60 % 60);
    else snprintf(out, cap, "%lldd %lldh", s / 86400, s / 3600 % 24);
}

static void day_hhmm(long long t, int off, char *out, size_t cap)
{
    char hm[8];
    wx_hhmm(t, off, hm, sizeof hm);
    snprintf(out, cap, "%s %s", wx_weekday_name(wx_local_wday(t, off)), hm);
}

static void when(const char *at, const char *in, long long t, double now, bool has_offset, int off, char *out, size_t cap)
{
    char s[24];
    if (has_offset) {
        wx_hhmm(t, off, s, sizeof s);
        snprintf(out, cap, "%s %s", at, s);
    } else {
        usage_dur(t - (long long)now, s, sizeof s);
        snprintf(out, cap, "%s %s", in, s);
    }
}

static uint32_t scale_by(uint32_t rgb, float k)
{
    uint32_t r = (uint32_t)(((rgb >> 16) & 0xFF) * k + 0.5f), g = (uint32_t)(((rgb >> 8) & 0xFF) * k + 0.5f),
             b = (uint32_t)((rgb & 0xFF) * k + 0.5f);
    return (r << 16) | (g << 8) | b;
}

static long long five_start(const usage_win_t *w) { return w->starts_at ? w->starts_at : w->resets_at - USAGE_FIVE_H_S; }

static void reset_line(const usage_acct_t *a, double now, bool has_offset, int off, usage_face_t *f)
{
    const usage_win_t *w = &a->five;
    char s[24];
    f->reset_rgb = USAGE_TEXT;
    if (a->stale) {
        f->reset_rgb = USAGE_DIM;
        if (a->updated_at && now > 0) {
            usage_dur((long long)now - a->updated_at, s, sizeof s);
            snprintf(f->reset, sizeof f->reset, "no update for %s", s);
        } else {
            snprintf(f->reset, sizeof f->reset, "no update");
        }
        return;
    }
    if (!w->have) return;
    bool timed = w->resets_at && now > 0 && now < w->resets_at;
    if (w->pct >= 100) {
        f->reset_rgb = USAGE_RED;
        if (timed) when("limited \xE2\x80\xA2 back at", "limited \xE2\x80\xA2 back in", w->resets_at, now, has_offset, off, f->reset, sizeof f->reset);
        else snprintf(f->reset, sizeof f->reset, "limited");
        return;
    }
    if (!w->resets_at) {
        snprintf(f->reset, sizeof f->reset, "new 5h window");
        return;
    }
    if (!timed) {
        if (now > 0) snprintf(f->reset, sizeof f->reset, "resets now");
        return;
    }
    double elapsed = now - (double)five_start(w);
    if (w->pct > 0 && elapsed >= USAGE_FORECAST_MIN_S) {
        double full = (double)five_start(w) + elapsed * 100.0 / w->pct;
        if (full < (double)w->resets_at) {
            char lead[28];
            when("limit by", "limit in", (long long)full, now, has_offset, off, lead, sizeof lead);
            snprintf(f->reset, sizeof f->reset, "%s at this pace", lead);
            f->reset_rgb = f->value_rgb;
            return;
        }
    }
    usage_dur(w->resets_at - (long long)now, s, sizeof s);
    snprintf(f->reset, sizeof f->reset, "resets in %s", s);
}

static void models_line(const usage_acct_t *a, bool initials, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < a->n_models && n < cap; i++) {
        const char *name = a->models[i].name + strspn(a->models[i].name, " ");
        int k = initials ? snprintf(out + n, cap - n, "%s%.2s %d%%", i ? " \xE2\x80\xA2 " : "", name, a->models[i].pct)
                         : snprintf(out + n, cap - n, "%s%s %d%%", i ? " \xE2\x80\xA2 " : "", name, a->models[i].pct);
        if (k < 0) return;
        n += (size_t)k;
    }
}

static void week_line(const usage_acct_t *a, double now, bool has_offset, int off, usage_face_t *f)
{
    if (a->n_models > 0) {
        models_line(a, false, f->week_line, sizeof f->week_line);
        models_line(a, true, f->week_short, sizeof f->week_short);
        return;
    }
    const usage_win_t *w = &a->week;
    if (!w->have) return;
    int n = snprintf(f->week_line, sizeof f->week_line, "week %d%%", w->pct);
    if (n <= 0 || (size_t)n >= sizeof f->week_line || !w->resets_at || now <= 0 || now >= w->resets_at) return;
    char s[24];
    if (has_offset) day_hhmm(w->resets_at, off, s, sizeof s);
    else {
        char d[16];
        usage_dur(w->resets_at - (long long)now, d, sizeof d);
        snprintf(s, sizeof s, "in %s", d);
    }
    snprintf(f->week_line + n, sizeof f->week_line - (size_t)n, " \xE2\x80\xA2 %s", s);
}

void usage_face(const usage_acct_t *a, double now, bool has_offset, int utc_offset, usage_face_t *f)
{
    memset(f, 0, sizeof *f);
    f->tool = a->tool;
    f->mark_rgb = a->stale ? USAGE_GREY : a->tool == USAGE_TOOL_CLAUDE ? 0xD77757 : a->tool == USAGE_TOOL_CODEX ? 0xFFFFFF : USAGE_GREY;
    const usage_win_t *w5 = &a->five, *w7 = &a->week;
    f->five_frac = w5->have ? (float)w5->pct / 100.0f : 0;
    f->week_frac = w7->have ? (float)w7->pct / 100.0f : 0;
    f->five_rgb = a->stale || !w5->have ? USAGE_GREY : usage_scale_rgb(w5->pct);
    f->week_rgb = scale_by(a->stale || !w7->have ? USAGE_GREY : usage_scale_rgb(w7->pct), 0.8f);
    f->value_rgb = f->five_rgb;
    if (w5->have) snprintf(f->value, sizeof f->value, "%d", w5->pct);
    else snprintf(f->value, sizeof f->value, "--");
    if (!a->stale && w5->have && w5->resets_at && now > 0) {
        double s = (double)five_start(w5), span = (double)w5->resets_at - s;
        if (span > 0 && now >= s && now <= w5->resets_at) {
            f->tick = true;
            f->tick_frac = (float)((now - s) / span);
        }
    }
    reset_line(a, now, has_offset, utc_offset, f);
    week_line(a, now, has_offset, utc_offset, f);
}

const char *usage_week_text(const usage_face_t *f, int label_w, int (*text_w)(const char *s, const void *ctx), const void *ctx)
{
    if (!f->week_short[0]) return f->week_line;
    return text_w(f->week_line, ctx) > label_w ? f->week_short : f->week_line;
}
