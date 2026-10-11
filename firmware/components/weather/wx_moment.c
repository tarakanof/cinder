#include "wx_moment.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wx_rain.h"
#include "wx_wind.h"

#define F (1.0f / WXM_FPS)
#define H ((float)WXM_HOLD_END / WXM_FPS)
#define DEG "\xC2\xB0"

static bool fresh(const wx_obs_t *o)
{
    return o && o->valid && o->has_temp && isfinite(o->temp_c) && !wx_look_from_obs(o).still;
}

static int temp_of(const wx_obs_t *o)
{
    float c = o->temp_c < -99 ? -99 : o->temp_c > 99 ? 99 : o->temp_c;
    return (int)lroundf(c);
}

static int precip_of(const wx_rain_t *r)
{
    if (!r->on) return 0;
    return r->kind == WX_PRECIP_STORM ? 3 : r->kind == WX_PRECIP_SNOW ? 2 : 1;
}

void wxm_sig_from(const wx_obs_t *o, const wxm_sig_t *prev, wxm_sig_t *s)
{
    memset(s, 0, sizeof *s);
    if (!fresh(o)) return;
    s->have = true;
    wx_rain_t r;
    wx_rain_from(o, &r);
    s->precip = precip_of(&r);
    int c = temp_of(o);
    bool kept = prev && prev->have;
    s->hot = c >= (kept && prev->hot ? WXM_HOT_KEEP_C : WXM_HOT_C);
    s->frost = c <= (kept && prev->frost ? WXM_FROST_KEEP_C : WXM_FROST_C);
    s->windy = wx_wind_strong(o, kept && prev->windy);
}

static wxm_kind_t top(wxm_kind_t a, wxm_kind_t b) { return a > b ? a : b; }

wxm_kind_t wxm_change(const wxm_sig_t *a, const wxm_sig_t *b)
{
    if (!a->have || !b->have) return WXM_NONE;
    wxm_kind_t k = WXM_NONE;
    if (b->precip != a->precip) {
        static const wxm_kind_t TO[4] = {WXM_NONE, WXM_RAIN, WXM_SNOW, WXM_STORM};
        k = b->precip >= 1 && b->precip <= 3 ? TO[b->precip] : a->precip == 1 || a->precip == 3 ? WXM_AFTERRAIN : WXM_NONE;
    }
    if (b->windy && !a->windy) k = top(k, WXM_WIND);
    if (b->hot && !a->hot) k = top(k, WXM_HEAT);
    if (b->frost && !a->frost) k = top(k, WXM_FROST);
    return k;
}

bool wxm_holds(wxm_kind_t k, const wxm_sig_t *s)
{
    if (!s->have) return false;
    switch (k) {
    case WXM_RAIN: return s->precip == 1;
    case WXM_SNOW: return s->precip == 2;
    case WXM_STORM: return s->precip == 3;
    case WXM_AFTERRAIN: return s->precip == 0;
    case WXM_WIND: return s->windy;
    case WXM_HEAT: return s->hot;
    case WXM_FROST: return s->frost;
    default: return false;
    }
}

static wxm_kind_t now_kind(const wx_temp_t *t)
{
    switch (precip_of(&t->rain)) {
    case 1: return WXM_RAIN;
    case 2: return WXM_SNOW;
    case 3: return WXM_STORM;
    default: break;
    }
    return t->night && t->cloud_band <= 1 ? WXM_NIGHT : WXM_GLANCE;
}

wxm_kind_t wxm_now_kind(const wx_obs_t *o, wx_temp_t *t)
{
    if (!fresh(o)) return WXM_NONE;
    wx_temp_from(o, t);
    return now_kind(t);
}

bool wxm_moment_from(wxm_kind_t kind, const wx_obs_t *o, bool quiet, wx_temp_t *t, wxm_moment_t *m)
{
    memset(m, 0, sizeof *m);
    if (!fresh(o) || kind <= WXM_NONE || kind >= WXM_KINDS) return false;
    wx_temp_from(o, t);
    m->kind = kind;
    m->quiet = quiet;
    int c = t->value;
    uint32_t trgb = wx_temp_rgb((float)c);
    const char *sky = wx_temp_sky(o, t);
    switch (kind) {
    case WXM_GLANCE:
        snprintf(m->text, sizeof m->text, "%d" DEG " %s", c, sky ? sky : "");
        m->rgb = trgb;
        break;
    case WXM_NIGHT:
        snprintf(m->text, sizeof m->text, "%s %d" DEG, sky ? sky : "Clear", c);
        m->rgb = trgb;
        m->moon = t->hero == WX_HERO_MOON || t->hero == WX_HERO_MOON_SMALL;
        m->moon_phase = t->moon_phase;
        m->moon_illum = t->moon_illum;
        break;
    case WXM_FROST:
        snprintf(m->text, sizeof m->text, "Frost %d" DEG, c);
        m->rgb = trgb;
        break;
    case WXM_HEAT:
        snprintf(m->text, sizeof m->text, "Hot %d" DEG, c);
        m->rgb = trgb;
        break;
    case WXM_SNOW:
        snprintf(m->text, sizeof m->text, "Snow %d" DEG, c);
        m->rgb = trgb;
        break;
    case WXM_WIND: {
        int kmh = o->has_wind ? (o->wind_kmh < 0 ? 0 : o->wind_kmh > WX_WIND_MAX_KMH ? WX_WIND_MAX_KMH : o->wind_kmh) : 0;
        snprintf(m->text, sizeof m->text, "Wind %d km/h", kmh);
        m->rgb = wx_wind_rgb(kmh);
        break;
    }
    case WXM_RAIN:
        if (t->rain.has_until)
            snprintf(m->text, sizeof m->text, "Rain until %02d:%02d", t->rain.until_min / 60, t->rain.until_min % 60);
        else
            snprintf(m->text, sizeof m->text, "%s", t->rain.on && t->rain.kind == WX_PRECIP_RAIN ? t->rain.sub : "Rain");
        m->rgb = WXM_BLUE;
        break;
    case WXM_AFTERRAIN:
        snprintf(m->text, sizeof m->text, "Rain stops");
        m->rgb = WXM_BLUE;
        break;
    case WXM_STORM:
        snprintf(m->text, sizeof m->text, "Storm");
        m->rgb = WXM_STORM_RGB;
        break;
    default: break;
    }
    if (kind == WXM_GLANCE || kind == WXM_FROST) m->sun = !t->night && t->cloud_band <= 3 && wx_look_from_obs(o).face != WX_FOG;
    return true;
}

void wxm_init(wxm_trigger_t *tr)
{
    memset(tr, 0, sizeof *tr);
    tr->last_at = -INFINITY;
    tr->next_every = INFINITY;
    tr->steady_at = -INFINITY;
    for (int k = 0; k < WXM_KINDS; k++) tr->kind_at[k] = -INFINITY;
}

bool wxm_tick(wxm_trigger_t *tr, const wxm_in_t *in, wxm_moment_t *m)
{
    double t = in->t;
    if (in->shown != tr->shown || in->mood != tr->mood || in->bot_mood != tr->bot_mood) {
        tr->shown = in->shown;
        tr->mood = in->mood;
        tr->bot_mood = in->bot_mood;
        tr->steady_at = t;
    }
    wxm_sig_t s;
    wxm_sig_from(in->obs, tr->base ? &tr->sig : NULL, &s);
    if (s.have) {
        wxm_kind_t k = tr->base ? wxm_change(&tr->sig, &s) : WXM_NONE;
        tr->base = true;
        tr->sig = s;
        if (k != WXM_NONE && (tr->pending == WXM_NONE || k >= tr->pending || !wxm_holds(tr->pending, &s))) {
            tr->pending = k;
            tr->pending_at = t;
        }
    }
    if (tr->pending != WXM_NONE && (t - tr->pending_at > WXM_WAIT_S || !wxm_holds(tr->pending, &tr->sig))) tr->pending = WXM_NONE;

    double every = in->every_s;
    bool avail = in->shown && in->mood != WXM_BUSY;
    bool reset = in->every_s != tr->every;
    tr->every = in->every_s;
    if (every <= 0 || !in->on) tr->next_every = INFINITY;
    else if (reset || !isfinite(tr->next_every) || (!avail && tr->next_every < t + every)) tr->next_every = t + every;

    if (!in->on || in->blocked || !avail || t - tr->steady_at < WXM_SETTLE_S || t - tr->last_at < WXM_GAP_S) return false;
    wxm_kind_t k = WXM_NONE;
    bool periodic = false;
    if (tr->pending != WXM_NONE && t - tr->kind_at[tr->pending] >= WXM_SAME_S) k = tr->pending;
    if (k == WXM_NONE && t >= tr->next_every) {
        k = wxm_now_kind(in->obs, &tr->scratch);
        periodic = true;
    }
    if (k == WXM_NONE || !wxm_moment_from(k, in->obs, in->mood == WXM_WORKING, &tr->scratch, m)) return false;
    if (periodic && (strcmp(m->text, tr->last_text) == 0 || t - tr->kind_at[k] < WXM_SAME_S)) {
        tr->next_every = t + every;
        return false;
    }
    return true;
}

void wxm_shown(wxm_trigger_t *tr, const wxm_moment_t *m, double t)
{
    if (tr->pending == m->kind) tr->pending = WXM_NONE;
    if (tr->every > 0) tr->next_every = t + tr->every;
    tr->last_at = t;
    tr->kind_at[m->kind] = t;
    snprintf(tr->last_text, sizeof tr->last_text, "%s", m->text);
}

typedef struct {
    float t, x, y, o;
} key_t;

static void track(const key_t *k, int n, float period, float delay, int frame, float *x, float *y, float *o)
{
    double p = period, u = fmod((double)frame / WXM_FPS - delay + 1e-6, p);
    if (u < 0) u += p;
    int i = 1;
    while (i < n - 1 && u >= k[i].t) i++;
    float span = k[i].t - k[i - 1].t, f = span > 0 ? (u - k[i - 1].t) / span : 1;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    *x = k[i - 1].x + (k[i].x - k[i - 1].x) * f;
    *y = k[i - 1].y + (k[i].y - k[i - 1].y) * f;
    *o = k[i - 1].o + (k[i].o - k[i - 1].o) * f;
}

#define NK(a) ((int)(sizeof a / sizeof a[0]))

static const key_t DRIFT[] = {{0, -4, 0, 1}, {1.6f, 4, 0, 1}, {3.2f, -4, 0, 1}};
static const key_t DROP[] = {{0, 3, 0, 0}, {2 * F, 2.5f, 12, 1}, {10 * F, -2, 64, 1}, {12 * F, -3, 78, 0}};
static const key_t FLAKE[] = {{0, 0, -6, 0}, {0.4f, 3, 2, 1}, {1.2f, -3, 20, 1}, {2.0f, 3, 33, 1}, {2.4f, 0, 40, 0}};
static const key_t GUST[] = {{0, 0, 0, 0}, {0.2f, 22, 0, 0.9f}, {0.7f, 78, 0, 0.9f}, {0.9f, 92, 0, 0}};
static const key_t LEAF[] = {{0, 0, 0, 0}, {0.2f, 18, -6, 1}, {0.8f, 70, 6, 1}, {1.4f, 122, -4, 1}, {1.6f, 136, 2, 0}};
static const key_t PULSE[] = {{0, 0, 0, 0.35f}, {0.8f, 0, 0, 1}, {1.6f, 0, 0, 0.35f}};
static const key_t TWINKLE[] = {{0, 0, 0, 1}, {0.8f, 0, 0, 0.3f}, {1.6f, 0, 0, 1}};
static const float GUST_DELAY[4] = {0, -6 * F, -3 * F, -9 * F};
static const int FLASH[3] = {12, 18, 24};

static int px(float v) { return (int)lroundf(v * WXM_K); }
static int opa(float o) { return (int)lroundf((o < 0 ? 0 : o > 1 ? 1 : o) * 255); }

static void moved(wxm_el_t *e, const key_t *k, int n, float period, float delay, int frame)
{
    float x, y, o;
    track(k, n, period, delay, frame, &x, &y, &o);
    e->x += px(x);
    e->y += px(y);
    e->opa = opa(o);
}

static bool flash(int k)
{
    for (int i = 0; i < 3; i++)
        if (k >= FLASH[i] && k < FLASH[i] + 2) return true;
    return false;
}

void wxm_frame(const wxm_moment_t *m, const wxm_layout_t *l, int k, wxm_frame_t *f)
{
    memset(f, 0, sizeof *f);
    if (k < 0 || k >= WXM_FRAMES) return;
    if (k <= WXM_SPRITE_FROM) {
        f->dy = WXM_RISE;
    } else if (k < WXM_SPRITE_IN) {
        float u = (float)(k - WXM_SPRITE_FROM) / (WXM_SPRITE_IN - WXM_SPRITE_FROM);
        f->opa = opa(u);
        f->dy = (int)lroundf(WXM_RISE * (1 - u));
    } else if (k <= WXM_HOLD_END) {
        f->opa = 255;
    } else if (k < WXM_SPRITE_OUT) {
        float u = (float)(k - WXM_HOLD_END) / (WXM_SPRITE_OUT - WXM_HOLD_END);
        f->opa = opa(1 - u);
        f->dy = (int)lroundf(WXM_RISE * u);
    } else {
        f->dy = WXM_RISE;
    }
    static const int TEXT[3] = {87, 171, 255};
    f->text_opa = k < WXM_TEXT_IN ? TEXT[k] : k < WXM_TEXT_OUT ? 255 : k < WXM_FRAMES - 1 ? TEXT[WXM_FRAMES - 2 - k] : 0;
    f->icon_off = m->quiet && k < WXM_FRAMES - 1;
    for (int i = 0; i < l->ne && i < WXM_ELEMS; i++)
        f->el[i] = (wxm_el_t){l->el[i].x, l->el[i].y + f->dy, 255};
    wxm_el_t *e = f->el;
    switch (m->kind) {
    case WXM_GLANCE:
    case WXM_FROST:
        moved(&e[m->sun ? 1 : 0], DRIFT, NK(DRIFT), 3.2f, 0, k);
        break;
    case WXM_RAIN:
        for (int i = 0; i < 4; i++) moved(&e[i], DROP, NK(DROP), 12 * F, -(float)i * 3 * F, k);
        e[4].opa = k < 4 ? 255 : 0;
        e[5].opa = k >= 4 && k < 6 ? 255 : 0;
        e[6].opa = k >= 6 ? 255 : 0;
        break;
    case WXM_STORM:
        e[0].opa = e[1].opa = flash(k) ? 255 : 0;
        e[2].opa = flash(k) ? 0 : 255;
        break;
    case WXM_SNOW:
        for (int i = 0; i < 5; i++) moved(&e[i], FLAKE, NK(FLAKE), 2.4f, -(float)i * 7 * F, k);
        break;
    case WXM_WIND:
        for (int i = 0; i < 4; i++) moved(&e[i], GUST, NK(GUST), 0.9f, GUST_DELAY[i], k);
        moved(&e[4], LEAF, NK(LEAF), 1.6f, -8 * F, k);
        break;
    case WXM_HEAT:
        moved(&e[0], PULSE, NK(PULSE), 1.6f, 0, k);
        break;
    case WXM_NIGHT: {
        int s = m->moon ? 1 : 0;
        moved(&e[s], TWINKLE, NK(TWINKLE), 1.6f, 0, k);
        moved(&e[s + 1], TWINKLE, NK(TWINKLE), 1.6f, -12 * F, k);
        break;
    }
    case WXM_AFTERRAIN:
        e[0].opa = opa((float)(k - WXM_SPRITE_IN) / WXM_FPS);
        break;
    default: break;
    }
}

int wxm_diff(const wxm_layout_t *l, const wxm_frame_t *a, const wxm_frame_t *b)
{
    static const wxm_frame_t none;
    if (!a) a = &none;
    int d = 0;
    if (a->text_opa != b->text_opa || a->icon_off != b->icon_off) d |= WXM_DIRTY_TEXT;
    if (a->opa != b->opa) return d | WXM_DIRTY_SPRITE;
    if (!b->opa) return d;
    if (a->dy != b->dy) return d | WXM_DIRTY_SPRITE;
    for (int i = 0; i < l->ne && i < WXM_ELEMS; i++) {
        const wxm_el_t *p = &a->el[i], *q = &b->el[i];
        if (!p->opa && !q->opa) continue;
        if (p->opa != q->opa || p->x != q->x || p->y != q->y) return d | WXM_DIRTY_SPRITE;
    }
    return d;
}

int wxm_abort_frame(int k)
{
    if (k < 0) return WXM_FRAMES - 1;
    if (k < WXM_TEXT_IN) return WXM_FRAMES - 2 - k;
    if (k <= WXM_SPRITE_FROM) return WXM_SPRITE_OUT;
    if (k < WXM_SPRITE_IN) return WXM_SPRITE_OUT + WXM_SPRITE_FROM - k;
    if (k < WXM_HOLD_END) return WXM_HOLD_END;
    return k;
}

static const wxm_cue_t GLANCE[] = {{F, WXM_GLIDE, 0, 0.25f, 0}, {2.4f, WXM_BLINK, 1, 0, 0}, {H, WXM_END, 1, 0.3f, 0}};
static const wxm_cue_t RAIN[] = {{F, WXM_GLIDE, 0, 0.25f, 0}, {1.6f, WXM_BLINK, 1, 0, 0}, {H, WXM_END, 1, 0.3f, 0}};
static const wxm_cue_t STORM[] = {
    {F, WXM_GLIDE, 0, 0.18f, 0},    {0.8f, WXM_LID, 0.45f, 0.075f, 0}, {1.0f, WXM_LID, 0.12f, 0.15f, 0},
    {1.2f, WXM_LID, 0.45f, 0.075f, 0}, {1.4f, WXM_LID, 0.12f, 0.15f, 0}, {1.6f, WXM_LID, 0.45f, 0.075f, 0},
    {1.8f, WXM_LID, 0.12f, 0.15f, 0}, {2.4f, WXM_LID, 0, 0.4f, 0},     {H, WXM_END, 1, 0.3f, 0},
};
static const wxm_cue_t SNOW[] = {
    {2 * F, WXM_EYES, 0, 0, WXM_EYES_ROUND}, {0.4f, WXM_GLIDE, 0, 0.15f, 0}, {1.4f, WXM_GLIDE, -0.15f, 0, 0},
    {2.4f, WXM_GLIDE, 0.15f, 0.05f, 0},      {3.2f, WXM_GLIDE, 0, 0.12f, 0}, {H, WXM_END, 1, 0.3f, 0},
};
static const wxm_cue_t WIND[] = {
    {F, WXM_SACCADE, 0.38f, 0.1f, 0},     {F, WXM_LID, 0.45f, 0.25f, 0},       {1.2f, WXM_SACCADE, 0.46f, 0.12f, 0},
    {1.7f, WXM_SACCADE, 0.33f, 0.08f, 0}, {2.5f, WXM_SACCADE, 0.46f, 0.1f, 0}, {3.2f, WXM_SACCADE, 0.36f, 0.12f, 0},
    {H, WXM_END, 0, 0.3f, 0},
};
static const wxm_cue_t HEAT[] = {{2 * F, WXM_EYES, 0, 0, WXM_EYES_HAPPY}, {0.4f, WXM_GLIDE, 0, 0.2f, 0}, {H, WXM_END, 1, 0.3f, 0}};
static const wxm_cue_t NIGHT[] = {
    {F, WXM_GLIDE, -0.1f, -0.15f, 0}, {F, WXM_LID, 0.4f, 0.6f, 0}, {2.2f, WXM_BLINK, 2.2f, 0, 0}, {H, WXM_END, 1, 0.4f, 0},
};
static const wxm_cue_t AFTERRAIN[] = {
    {2 * F, WXM_EYES, 0, 0, WXM_EYES_HAPPY}, {0.4f, WXM_GLIDE, 0, 0.12f, 0}, {1.2f, WXM_HOP, 0, 0, 0}, {H, WXM_END, 1, 0.3f, 0},
};

const wxm_cue_t *wxm_script(const wxm_moment_t *m, int *n)
{
    *n = 0;
    if (m->quiet) return NULL;
    const wxm_cue_t *c = NULL;
    switch (m->kind) {
    case WXM_GLANCE:
    case WXM_FROST: c = GLANCE, *n = NK(GLANCE); break;
    case WXM_RAIN: c = RAIN, *n = NK(RAIN); break;
    case WXM_STORM: c = STORM, *n = NK(STORM); break;
    case WXM_SNOW: c = SNOW, *n = NK(SNOW); break;
    case WXM_WIND: c = WIND, *n = NK(WIND); break;
    case WXM_HEAT: c = HEAT, *n = NK(HEAT); break;
    case WXM_NIGHT: c = NIGHT, *n = NK(NIGHT); break;
    case WXM_AFTERRAIN: c = AFTERRAIN, *n = NK(AFTERRAIN); break;
    default: break;
    }
    return c;
}
