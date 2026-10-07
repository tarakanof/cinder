#include "bot_behavior.h"

#include <math.h>
#include <string.h>

#define REST_X 0.67
#define REST_Y 0.77
#define VIEWER_X 0.0
#define VIEWER_Y 0.1
#define TRANSITION_S 0.7
#define HOP_LEN_S BOT_HOP_DURATION_S
#define HOP_CURVE_LEN 0.62

enum { EASE_IN_OUT, EASE_OUT_BACK };

static uint64_t rng_next(bot_t *b)
{
    b->rng += 0x9E3779B97F4A7C15ULL;
    uint64_t z = b->rng;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static double rng_unit(bot_t *b) { return (double)(rng_next(b) >> 11) * 0x1.0p-53; }
static double rng_range(bot_t *b, double lo, double hi) { return lo + (hi - lo) * rng_unit(b); }
static double jitter(bot_t *b, double a) { return rng_range(b, -a, a); }

static double clamp01(double u) { return u < 0 ? 0 : (u > 1 ? 1 : u); }
static double ease(int kind, double u)
{
    u = clamp01(u);
    if (kind == EASE_IN_OUT) {
        return u < 0.5 ? 4 * u * u * u : 1 - pow(-2 * u + 2, 3) / 2;
    }
    const double c1 = 1.4, c3 = c1 + 1;
    return 1 + c3 * pow(u - 1, 3) + c1 * pow(u - 1, 2);
}

static bot_tween_t tween_const(double v) { return (bot_tween_t){v, v, 0, 0, EASE_IN_OUT}; }
static bot_tween_t tween(double from, double to, double start, double dur, int e)
{
    return (bot_tween_t){from, to, start, dur, e};
}
static double tween_value(const bot_tween_t *tw, double t)
{
    if (t < tw->start) return tw->from;
    if (tw->dur <= 0 || t >= tw->start + tw->dur) return tw->to;
    return tw->from + (tw->to - tw->from) * ease(tw->ease, (t - tw->start) / tw->dur);
}
static bool tween_active(const bot_tween_t *tw, double t) { return tw->from != tw->to && t < tw->start + tw->dur; }

static double lognormal(bot_t *b, double median, double sigma, double lo, double hi)
{
    double u1 = rng_unit(b), u2 = rng_unit(b);
    if (u1 < 1e-300) u1 = 1e-300;
    double n = sqrt(-2 * log(u1)) * cos(2 * M_PI * u2);
    double v = median * exp(sigma * n);
    return v < lo ? lo : (v > hi ? hi : v);
}

static bot_eyes_t eyes_for(bot_mood_t m)
{
    switch (m) {
    case BOT_WAITING: return BOT_EYES_ROUND;
    case BOT_DONE:    return BOT_EYES_HAPPY;
    case BOT_ERROR:   return BOT_EYES_ANGRY;
    default:          return BOT_EYES_DASH;
    }
}

static double lid_curve(double u, double speed)
{
    double close = 0.075 * speed, hold = 0.035 * speed, open = 0.15 * speed;
    if (u <= 0) return 0;
    if (u < close) { double v = u / close; return v * v; }
    if (u < close + hold) return 1;
    if (u < close + hold + open) { double v = (u - close - hold) / open; return (1 - v) * (1 - v); }
    return 0;
}
static double blink_length(double speed) { return (0.075 + 0.035 + 0.15) * speed; }

static void hop_curve(double u, double *sx, double *sy, double *dy)
{
#define LERP(a, b, k) ((a) + ((b) - (a)) * clamp01(k))
    if (u < 0.12) {
        double k = ease(EASE_IN_OUT, u / 0.12);
        *sx = 1 + 0.1 * k; *sy = 1 - 0.14 * k; *dy = 0;
    } else if (u < 0.30) {
        double v = (u - 0.12) / 0.18;
        if (v < 0.3) { *sx = LERP(1.1, 0.93, v / 0.3); *sy = LERP(0.86, 1.1, v / 0.3); }
        else { *sx = LERP(0.93, 1, (v - 0.3) / 0.7); *sy = LERP(1.1, 1, (v - 0.3) / 0.7); }
        *dy = 0.22 * (1 - (1 - v) * (1 - v));
    } else if (u < 0.46) {
        double v = (u - 0.30) / 0.16;
        *sx = LERP(1, 0.95, v); *sy = LERP(1, 1.07, v); *dy = 0.22 * (1 - v * v);
    } else {
        double v = (u - 0.46) / 0.16; if (v > 1) v = 1;
        double k = sin(M_PI * v);
        *sx = 1 + 0.1 * k; *sy = 1 - 0.12 * k; *dy = 0;
    }
#undef LERP
}

static void track_step(bot_t *b, double t);

static void current_gaze(const bot_t *b, double t, double *x, double *y)
{
    if (!(b->gaze_start >= 0 && t < b->gaze_start + b->gaze_dur)) { *x = b->gaze_to_x; *y = b->gaze_to_y; return; }
    double k = ease(b->gaze_ease, (t - b->gaze_start) / b->gaze_dur);
    *x = b->gaze_from_x + (b->gaze_to_x - b->gaze_from_x) * k;
    *y = b->gaze_from_y + (b->gaze_to_y - b->gaze_from_y) * k;
}

static double blink_interval(bot_t *b)
{
    double med;
    switch (b->mood) {
    case BOT_WORKING: med = 6.0; break;
    case BOT_WAITING: med = 3.0; break;
    case BOT_SLEEPY:  med = 2.8; break;
    case BOT_ERROR:   med = 2.2; break;
    case BOT_DONE:    med = 3.2; break;
    default:          med = 4.0; break;
    }
    return lognormal(b, med, 0.45, 1.0, 12);
}

static double fixation(bot_t *b)
{
    switch (b->mood) {
    case BOT_WORKING: return lognormal(b, 0.9, 0.35, 0.35, 2);
    case BOT_WAITING: return lognormal(b, 2.5, 0.4, 0.8, 6);
    case BOT_SLEEPY:  return lognormal(b, 8, 0.4, 4, 20);
    case BOT_ERROR:   return lognormal(b, 0.9, 0.4, 0.3, 2.5);
    case BOT_DONE:    return lognormal(b, 2.5, 0.4, 0.8, 6);
    default:          return lognormal(b, 2.2, 0.5, 0.6, 7);
    }
}

static void next_target(bot_t *b, double *x, double *y)
{
    double r = rng_unit(b);
    switch (b->mood) {
    case BOT_IDLE:
        if (r < 0.45) { *x = REST_X + jitter(b, 0.1); *y = REST_Y + jitter(b, 0.08); return; }
        if (r < 0.7) { *x = VIEWER_X + jitter(b, 0.08); *y = VIEWER_Y + jitter(b, 0.08); return; }
        *x = jitter(b, 0.9); *y = rng_range(b, -0.6, 0.8); return;
    case BOT_WORKING:
        if (r < 0.1) { *x = VIEWER_X + jitter(b, 0.05); *y = VIEWER_Y; return; }
        b->read_x += rng_range(b, 0.28, 0.45);
        if (b->read_x > 0.7) b->read_x = -0.7;
        *x = b->read_x; *y = -0.15 + jitter(b, 0.04); return;
    case BOT_WAITING:
        if (r < 0.75) { *x = VIEWER_X + jitter(b, 0.05); *y = VIEWER_Y + jitter(b, 0.05); return; }
        *x = r < 0.875 ? -0.7 : 0.7; *y = jitter(b, 0.2); return;
    case BOT_SLEEPY:
        *x = jitter(b, 0.5); *y = rng_range(b, -0.5, -0.2); return;
    case BOT_ERROR:
        *x = jitter(b, 0.9); *y = jitter(b, 0.7); return;
    case BOT_DONE:
    default:
        if (r < 0.5) { *x = REST_X + jitter(b, 0.08); *y = REST_Y + jitter(b, 0.06); return; }
        *x = VIEWER_X + jitter(b, 0.06); *y = VIEWER_Y + jitter(b, 0.06); return;
    }
}

static void start_blink(bot_t *b, double t)
{
    b->blinking = true;
    b->blink_start = t;
    b->last_blink_at = t;
    b->blink_lag = rng_range(b, 0, 0.02);
    b->blink_speed = b->mood == BOT_SLEEPY ? 2.2 : 1;
    b->double_blink_pending = rng_unit(b) < 0.12;
    b->next_blink_at = t + blink_interval(b);
}

static void start_saccade_to(bot_t *b, double t, bool glide, double tx, double ty)
{
    double fx, fy;
    current_gaze(b, t, &fx, &fy);
    double amp = hypot(tx - fx, ty - fy);
    b->gaze_from_x = fx; b->gaze_from_y = fy;
    b->gaze_to_x = tx; b->gaze_to_y = ty;
    b->gaze_start = t;
    b->gaze_dur = glide ? 0.42 : 0.025 + 0.045 * amp;
    b->gaze_ease = glide ? EASE_IN_OUT : EASE_OUT_BACK;
    if (!glide && amp > 0.8 && !b->blinking && t - b->last_blink_at > 1.2 && rng_unit(b) < 0.6) {
        start_blink(b, t);
    }
    double lean_dur = glide ? 0.5 : 0.2;
    b->lean_x = tween(tween_value(&b->lean_x, t), tx * 0.045, t + 0.03, lean_dur, EASE_IN_OUT);
    b->lean_y = tween(tween_value(&b->lean_y, t), ty * 0.03, t + 0.03, lean_dur, EASE_IN_OUT);
    b->next_saccade_at = t + b->gaze_dur + fixation(b);
}

static void start_saccade(bot_t *b, double t, bool glide)
{
    double tx, ty;
    next_target(b, &tx, &ty);
    start_saccade_to(b, t, glide, tx, ty);
}

static void enter(bot_t *b, bot_mood_t m, double t)
{
    bool waking = b->mood == BOT_SLEEPY;
    b->mood = m;
    b->mood_since = t;
    b->swap_eyes_on_close = true;
    if (!b->blinking) { start_blink(b, t); if (b->blink_speed < 1.6) b->blink_speed = 1.6; }
    else b->double_blink_pending = true;
    if (waking) b->double_blink_pending = true;

    double morph = 0.45;
    b->triangle = tween(tween_value(&b->triangle, t), m == BOT_ERROR ? 1 : 0, t, morph, EASE_IN_OUT);
    b->slump = tween(tween_value(&b->slump, t), m == BOT_SLEEPY ? 1 : 0, t, m == BOT_SLEEPY ? 1.6 : morph, EASE_IN_OUT);
    bool badged = m == BOT_WAITING || m == BOT_DONE;
    b->badge = tween(tween_value(&b->badge, t), badged ? 1 : 0, t + 0.1, 0.35,
                     badged ? EASE_OUT_BACK : EASE_IN_OUT);
    b->popping = m != BOT_SLEEPY;
    b->pop_start = t;
    b->read_x = -0.6;
    start_saccade(b, t, true);
    b->next_hop_at = m == BOT_WAITING ? t + 0.6 : INFINITY;
}

void bot_set_sleep_after(bot_t *b, double s) { b->sleep_after_s = s < 0 ? 0 : s; }

void bot_init(bot_t *b, uint64_t seed, double now)
{
    memset(b, 0, sizeof(*b));
    b->rng = seed;
    b->mood = BOT_IDLE;
    b->eyes = BOT_EYES_DASH;
    b->mood_since = now;
    b->sleep_after_s = BOT_SLEEP_AFTER_S;
    b->gaze_from_x = b->gaze_to_x = REST_X;
    b->gaze_from_y = b->gaze_to_y = REST_Y;
    b->gaze_start = -1;
    b->gaze_ease = EASE_OUT_BACK;
    b->next_saccade_at = now + 1;
    b->read_x = -0.6;
    b->blink_speed = 1;
    b->next_blink_at = now + 0.8;
    b->last_blink_at = -INFINITY;
    b->triangle = b->slump = b->badge = b->lean_x = b->lean_y = tween_const(0);
    b->next_hop_at = INFINITY;
    b->animating = true;
}

bool bot_set_mood(bot_t *b, bot_mood_t m, double t)
{
    if (m == b->mood || (m == BOT_IDLE && b->mood == BOT_SLEEPY)) return false;
    enter(b, m, t);
    return true;
}

bot_pose_t bot_pose(bot_t *b, double t)
{
    if (b->mood == BOT_IDLE && b->sleep_after_s > 0 && t - b->mood_since >= b->sleep_after_s) enter(b, BOT_SLEEPY, t);
    if (!b->blinking && t >= b->next_blink_at) start_blink(b, t);
    if (t >= b->next_saccade_at) start_saccade(b, t, false);
    if (t >= b->next_hop_at) {
        b->hopping = true;
        b->hop_start = t;
        b->next_hop_at = t + lognormal(b, 4.5, 0.35, 2.5, 9);
    }

    bot_pose_t p = {.mood = b->mood, .scale_x = 1, .scale_y = 1};
    double bl = 0, br = 0;
    if (b->blinking) {
        double u = t - b->blink_start;
        bl = lid_curve(u, b->blink_speed);
        br = lid_curve(u - b->blink_lag, b->blink_speed);
        if (b->swap_eyes_on_close && fmax(bl, br) >= 0.9) { b->eyes = eyes_for(b->mood); b->swap_eyes_on_close = false; }
        if (u - b->blink_lag >= blink_length(b->blink_speed)) {
            if (b->swap_eyes_on_close && !b->double_blink_pending) { b->eyes = eyes_for(b->mood); b->swap_eyes_on_close = false; }
            b->blinking = false;
            if (b->double_blink_pending) {
                b->double_blink_pending = false;
                b->blinking = true;
                b->blink_start = t + 0.06;
            }
        }
    }
    p.eyes = b->eyes;
    double heavy = tween_value(&b->slump, t) * 0.55;
    p.lid_l = heavy + (1 - heavy) * bl;
    p.lid_r = heavy + (1 - heavy) * br;

    bool sac_active = b->gaze_start >= 0 && t < b->gaze_start + b->gaze_dur;
    current_gaze(b, t, &p.gaze_x, &p.gaze_y);
    if (b->tracking) {
        track_step(b, t);
        p.gaze_x = b->trk_gx;
        p.gaze_y = b->trk_gy;
    }
    p.triangle = tween_value(&b->triangle, t);
    p.slump = tween_value(&b->slump, t);
    p.badge = tween_value(&b->badge, t);
    p.offset_x = tween_value(&b->lean_x, t);
    p.offset_y = tween_value(&b->lean_y, t);

    if (b->hopping) {
        double u = t - b->hop_start;
        if (u < HOP_LEN_S) {
            double sx, sy, dy;
            hop_curve(u * HOP_CURVE_LEN / HOP_LEN_S, &sx, &sy, &dy);
            p.scale_x *= 1 + (sx - 1) * BOT_HOP_SQUASH;
            p.scale_y *= 1 + (sy - 1) * BOT_HOP_SQUASH;
            p.offset_y += dy;
        } else {
            b->hopping = false;
        }
    }
    if (b->popping) {
        double u = (t - b->pop_start) / 0.4;
        if (u < 1) {
            double k = 1 + 0.1 * sin(M_PI * u) * (1 - u);
            p.scale_x *= k; p.scale_y *= k;
        } else {
            b->popping = false;
        }
    }
    b->animating = b->blinking || sac_active || b->tracking || b->hopping || b->popping ||
                   tween_active(&b->triangle, t) || tween_active(&b->slump, t) || tween_active(&b->badge, t) ||
                   tween_active(&b->lean_x, t) || tween_active(&b->lean_y, t);
    (void)TRANSITION_S;
    return p;
}

void bot_look(bot_t *b, double x, double y, double t)
{
    start_saccade_to(b, t, false, x, y);
    b->next_saccade_at = t + 2.5;
}

#define TRACK_OMEGA 14.0

void bot_track(bot_t *b, double x, double y, double t)
{
    if (!b->tracking) {
        b->tracking = true;
        current_gaze(b, t, &b->trk_gx, &b->trk_gy);
        b->trk_vx = b->trk_vy = 0;
        b->trk_t = t;
        b->lean_x = tween(tween_value(&b->lean_x, t), 0, t, 0.3, EASE_IN_OUT);
        b->lean_y = tween(tween_value(&b->lean_y, t), 0, t, 0.3, EASE_IN_OUT);
    }
    b->trk_x = x;
    b->trk_y = y;
    b->next_saccade_at = t + 2.5;
}

void bot_track_end(bot_t *b, double t)
{
    if (!b->tracking) return;
    b->tracking = false;
    b->gaze_from_x = b->gaze_to_x = b->trk_gx;
    b->gaze_from_y = b->gaze_to_y = b->trk_gy;
    b->gaze_start = t;
    b->gaze_dur = 0;
    b->next_saccade_at = t + 1.0;
}

void bot_blink(bot_t *b, double t)
{
    if (!b->blinking) start_blink(b, t);
}

void bot_look_still(bot_t *b, double x, double y, double t)
{
    start_saccade_to(b, t, false, x, y);
    b->lean_x = tween(tween_value(&b->lean_x, t), 0, t, 0.2, EASE_IN_OUT);
    b->lean_y = tween(tween_value(&b->lean_y, t), 0, t, 0.2, EASE_IN_OUT);
    b->next_saccade_at = t + 2.5;
}

void bot_hold_gaze(bot_t *b, double until)
{
    if (b->next_saccade_at < until) b->next_saccade_at = until;
}

static void track_step(bot_t *b, double t)
{
    double dt = t - b->trk_t;
    b->trk_t = t;
    if (dt <= 0) return;
    if (dt > 0.1) dt = 0.1;
    const double w = TRACK_OMEGA;
    for (int k = 0; k < 2; k++) {
        double h = dt / 2;
        b->trk_vx += (w * w * (b->trk_x - b->trk_gx) - 2 * w * b->trk_vx) * h;
        b->trk_vy += (w * w * (b->trk_y - b->trk_gy) - 2 * w * b->trk_vy) * h;
        b->trk_gx += b->trk_vx * h;
        b->trk_gy += b->trk_vy * h;
    }
}

void bot_push(bot_t *b, double t)
{
    b->hopping = true; b->hop_start = t;
    b->popping = true; b->pop_start = t;
}

bool bot_pose_same(const bot_pose_t *a, const bot_pose_t *b, double radius_px)
{
    if (a->mood != b->mood || a->eyes != b->eyes) return false;
    double step = 0.5 / (radius_px < 1 ? 1 : radius_px);
#define Q(v, s) round((v) / (s))
#define SAME(f, s) (Q(a->f, s) == Q(b->f, s))
    return SAME(gaze_x, step) && SAME(gaze_y, step) && SAME(lid_l, step) && SAME(lid_r, step) &&
           SAME(triangle, step) && SAME(slump, step) && SAME(badge, step) && SAME(offset_x, step) &&
           SAME(offset_y, step) && SAME(orbit, step) && SAME(scale_x, step / 2) && SAME(scale_y, step / 2);
#undef SAME
#undef Q
}
