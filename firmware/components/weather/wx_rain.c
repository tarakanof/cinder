#include "wx_rain.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wx_paint.h"

static const wx_drop_t LEFT[] = {
    {70, 12, 16, 0.9f, 3, 0, 1},   {92, 70, 12, 0.6f, 3, 0, 2},  {64, 118, 18, 0.8f, 5, 0, 1},
    {86, 30, 14, 0.75f, 3, 1, 1},  {66, 88, 16, 1.0f, 3, 1, 2},  {96, 128, 12, 0.55f, 4, 1, 1},
    {82, 42, 14, 0.7f, 3, 0, 3},   {100, 104, 12, 0.6f, 3, 0, 3}, {100, 4, 12, 0.65f, 3, 1, 3},
    {78, 62, 14, 0.7f, 4, 1, 3},
};
static const wx_drop_t RIGHT[] = {
    {376, 20, 16, 0.85f, 3, 0, 1}, {396, 84, 12, 0.6f, 4, 0, 2},  {370, 126, 14, 0.95f, 3, 0, 1},
    {390, 8, 14, 0.7f, 3, 1, 1},   {372, 60, 18, 0.9f, 5, 1, 2},  {398, 110, 12, 0.55f, 3, 1, 1},
    {386, 50, 14, 0.7f, 3, 0, 3},  {358, 96, 12, 0.6f, 3, 0, 3},  {358, 34, 12, 0.65f, 3, 1, 3},
    {384, 136, 14, 0.7f, 4, 1, 3},
};
static const wx_drop_t TOP[] = {
    {172, 10, 16, 0.9f, 3, 0, 1},  {246, 64, 14, 0.7f, 3, 0, 2},  {298, 112, 12, 0.8f, 4, 0, 1},
    {205, 128, 16, 0.6f, 3, 0, 2}, {226, 24, 14, 1.0f, 3, 1, 1},  {186, 80, 12, 0.65f, 5, 1, 2},
    {280, 40, 16, 0.8f, 3, 1, 1},  {160, 136, 14, 0.7f, 3, 1, 1}, {212, 36, 14, 0.7f, 3, 0, 3},
    {270, 92, 12, 0.6f, 3, 0, 3},  {256, 108, 12, 0.65f, 4, 1, 3}, {300, 70, 14, 0.7f, 3, 1, 3},
};

#define N(a) ((int)(sizeof a / sizeof a[0]))

static const wx_sheet_t SHEETS[WX_SHEETS] = {
    {{58, 150, 118, 332}, 20, 6, N(LEFT), LEFT},
    {{348, 150, 402, 332}, 24, 15, N(RIGHT), RIGHT},
    {{150, 46, 316, 132}, 17, 0, N(TOP), TOP},
};

const wx_sheet_t *wx_sheet(int s) { return s >= 0 && s < WX_SHEETS ? &SHEETS[s] : NULL; }

float wx_canopy_y(float x)
{
    float u = (x - 233.0f) / 128.0f;
    float k = 1 - u * u;
    return 198.0f - 103.0f * sqrtf(k > 0 ? k : 0);
}

uint32_t wx_rain_rgb(int intensity) { return intensity <= 1 ? 0x4F81B7 : intensity == 2 ? 0x5C9CE0 : 0x3D7BF5; }

static bool obs_now(const wx_obs_t *o, long long *now)
{
    if (!o->has_obs_age || o->observed_at <= 0) return false;
    double a = o->obs_age_s + (o->age_s > 0 ? o->age_s : 0);
    if (!isfinite(a) || a < 0 || a > 1e9) return false;
    *now = o->observed_at + (long long)a;
    return true;
}

static const char *word(wx_precip_t k, int in)
{
    if (k == WX_PRECIP_STORM) return "Storm";
    if (k == WX_PRECIP_SNOW) return in == 1 ? "Light snow" : in == 3 ? "Heavy snow" : "Snow";
    return in == 1 ? "Light rain" : in == 3 ? "Heavy rain" : "Rain";
}

void wx_rain_from(const wx_obs_t *o, wx_rain_t *r)
{
    memset(r, 0, sizeof *r);
    r->intensity = WX_INT_MODERATE;
    if (!o || !o->valid) return;
    wx_look_t l = wx_look_from_obs(o);
    if (l.still) return;
    long long now = 0;
    bool known = obs_now(o, &now);
    long long at = known ? now : o->observed_at;
    const wx_spell_t *sp = NULL;
    for (int i = 0; at > 0 && i < o->rain_n && i < WX_SPELLS; i++)
        if (o->rain_spell[i].start <= at && at < o->rain_spell[i].end) sp = &o->rain_spell[i];
    bool storm = l.face == WX_STORM;
    bool wet;
    if (o->has_now_rain || o->has_rain) wet = (o->has_now_rain && o->now_rain > 0) || sp != NULL;
    else wet = l.face == WX_RAIN || l.face == WX_SNOW;
    if (wet && known && o->rain_until > 0 && now >= o->rain_until) wet = false;
    if (!wet && !storm) return;
    r->on = true;
    r->kind = storm ? WX_PRECIP_STORM
              : l.face == WX_SNOW ? WX_PRECIP_SNOW
              : l.face == WX_RAIN ? WX_PRECIP_RAIN
              : sp && sp->kind == 1 ? WX_PRECIP_SNOW
                                    : WX_PRECIP_RAIN;
    int in = sp ? sp->intensity : o->has_now_rain && o->now_rain > 0 ? o->now_rain : (int)l.intensity;
    r->intensity = in < 1 ? 1 : in > 3 ? 3 : in;
    r->has_pop = o->has_pop;
    r->pop = o->has_pop ? (o->pop < 0 ? 0 : o->pop > 100 ? 100 : o->pop) : 0;
    if (wet && o->rain_until > 0 && o->has_utc_offset) {
        long long loc = o->rain_until + o->utc_offset;
        r->has_until = true;
        r->until_min = (int)(((loc % 86400) + 86400) % 86400 / 60);
        snprintf(r->sub, sizeof r->sub, "until %02d:%02d", r->until_min / 60, r->until_min % 60);
    } else {
        snprintf(r->sub, sizeof r->sub, "%s", word(r->kind, r->intensity));
    }
}

bool wx_rain_same_scene(const wx_rain_t *a, const wx_rain_t *b)
{
    return a->on == b->on && (!a->on || (a->kind == b->kind && a->intensity == b->intensity));
}

bool wx_drop_shown(const wx_drop_t *d, const wx_rain_t *r) { return d->level <= r->intensity; }

static const int SPLASH_DROP[WX_SPLASHES] = {0, 1, 6};

bool wx_splash_at(int n, wx_splash_at_t *at)
{
    if (n < 0 || n >= WX_SPLASHES) return false;
    const wx_drop_t *d = &TOP[SPLASH_DROP[n]];
    *at = (wx_splash_at_t){(int)d->x - 8, (int)lroundf(wx_canopy_y(d->x) - 3) - 9, 2, SPLASH_DROP[n]};
    return true;
}

int wx_sheet_frames(const wx_rain_t *r, int s)
{
    int f = 2 * SHEETS[s].tile_frames;
    return r && r->kind == WX_PRECIP_SNOW ? 2 * f : f;
}

static int offset(int frames, int a) { return (int)lround((double)WX_SHEET_PERIOD * a / frames) % WX_SHEET_PERIOD; }

int wx_splash_hit(int n)
{
    wx_splash_at_t at;
    if (!wx_splash_at(n, &at)) return 0;
    const wx_sheet_t *s = &SHEETS[at.sheet];
    const wx_drop_t *d = &s->drop[at.drop];
    float ty = d->y + (d->tile ? WX_SHEET_H : 0);
    float t = fmodf(wx_canopy_y(d->x) - 2 - (float)s->box.y0 - ty - d->len, (float)WX_SHEET_PERIOD);
    if (t < 0) t += WX_SHEET_PERIOD;
    int frames = 2 * s->tile_frames;
    for (int a = 0; a < frames; a++)
        if ((float)offset(frames, a) >= t) return a;
    return 0;
}

static const struct {
    int period, start;
} DRIP[WX_DRIPS] = {{24, 5}, {32, 13}};

static const struct {
    int pose, dy, opa;
} DRIP_SEQ[7] = {{0, 0, 153}, {1, 0, 230}, {2, 1, 255}, {2, 6, 255}, {2, 14, 230}, {2, 24, 166}, {2, 34, 89}};

static const struct {
    int dy, opa;
} SPLASH_SEQ[3] = {{1, 179}, {0, 255}, {-2, 153}};

static const int FLASH[6] = {12, 13, 18, 19, 24, 25};

void wx_rain_frame(const wx_rain_t *r, int k, wx_rain_frame_t *f)
{
    memset(f, 0, sizeof *f);
    if (k < 0) k = 0;
    for (int s = 0; s < WX_SHEETS; s++) {
        int frames = wx_sheet_frames(r, s);
        f->off[s] = offset(frames, (k + SHEETS[s].delay_frames) % frames);
    }
    bool snow = r->kind == WX_PRECIP_SNOW;
    for (int i = 0; i < WX_DRIPS; i++) {
        f->drip_pose[i] = -1;
        int j = k % DRIP[i].period - DRIP[i].start;
        if (snow || j < 0 || j >= 7) continue;
        f->drip_pose[i] = DRIP_SEQ[j].pose;
        f->drip_dy[i] = DRIP_SEQ[j].dy;
        f->drip_opa[i] = DRIP_SEQ[j].opa;
    }
    for (int n = 0; n < WX_SPLASHES && !snow; n++) {
        wx_splash_at_t at;
        wx_splash_at(n, &at);
        if (!wx_drop_shown(&SHEETS[at.sheet].drop[at.drop], r)) continue;
        int frames = wx_sheet_frames(r, at.sheet);
        int a = (k + SHEETS[at.sheet].delay_frames) % frames;
        int j = ((a - wx_splash_hit(n)) % frames + frames) % frames;
        if (j >= 3) continue;
        f->splash_dy[n] = SPLASH_SEQ[j].dy;
        f->splash_opa[n] = SPLASH_SEQ[j].opa;
    }
    if (r->kind == WX_PRECIP_STORM)
        for (int i = 0; i < 6; i++) f->bolt |= k % WX_STORM_FRAMES == FLASH[i];
}

void wx_rain_ops(const wx_rain_frame_t *p, int p_opa, const wx_rain_frame_t *f, int opa, wx_rain_ops_t *ops)
{
    memset(ops, 0, sizeof *ops);
    ops->opa = opa != p_opa;
    ops->layer_opa = opa;
    for (int s = 0; s < WX_SHEETS; s++) {
        ops->strip_y[s] = f->off[s] != p->off[s];
        ops->strip_top[s][0] = f->off[s] - WX_SHEET_PERIOD;
        ops->strip_top[s][1] = f->off[s];
    }
    for (int i = 0; i < WX_DRIPS; i++) {
        ops->drip_pose[i] = f->drip_pose[i];
        ops->drip_on[i] = f->drip_pose[i] >= 0;
        if (!ops->drip_on[i]) continue;
        ops->drip_top[i] = WX_DRIP_Y + f->drip_dy[i] - SHEETS[i].box.y0;
        ops->drip_alpha[i] = f->drip_opa[i] * opa / 255;
        bool pose = f->drip_pose[i] != p->drip_pose[i];
        ops->drip_buf[i] = pose;
        ops->drip_y[i] = pose || f->drip_dy[i] != p->drip_dy[i];
        ops->drip_opa[i] = ops->opa || pose || f->drip_opa[i] != p->drip_opa[i];
    }
    for (int n = 0; n < WX_SPLASHES; n++) {
        ops->splash_on[n] = f->splash_opa[n] > 0;
        if (!ops->splash_on[n]) continue;
        wx_splash_at_t at;
        wx_splash_at(n, &at);
        ops->splash_top[n] = at.y + f->splash_dy[n] - SHEETS[at.sheet].box.y0;
        ops->splash_alpha[n] = f->splash_opa[n] * opa / 255;
        bool appear = p->splash_opa[n] <= 0;
        ops->splash_y[n] = appear || f->splash_dy[n] != p->splash_dy[n];
        ops->splash_opa[n] = ops->opa || appear || f->splash_opa[n] != p->splash_opa[n];
    }
    ops->bolt_on = f->bolt;
}
