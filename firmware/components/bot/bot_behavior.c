#include "bot_behavior.h"

#include <math.h>
#include <string.h>

#define REST_X 0.67
#define REST_Y 0.77
#define VIEWER_X 0.0
#define VIEWER_Y 0.1
#define BADGED_REST_X 0.5
#define BADGED_REST_Y 0.6
#define MICRO_HOLD_S 0.6
#define MICRO_DUR_S 0.025
#define TRANSITION_S 0.7
#define HOP_LEN_S BOT_HOP_DURATION_S
#define HOP_CURVE_LEN 0.62
#define ASLEEP_X 0.0
#define ASLEEP_Y -0.35
#define ASLEEP_CLOSE_S 1.2
#define STIR_OPEN_S 0.6
#define READ_RIGHT 0.7
#define READ_FLOOR -0.35
#define READ_MOMENT_FLOOR -0.19
#define READ_MOMENT_TOP 0.15
#define READ_OVERSHOOT 0.071
#define MARK_X 0.0
#define COVER_AMP 0.6
#define LID_SHUT 0.9
#define WAIT_FIRST_HOP_S 0.6
#define BURST_H 0.6
#define REMIND_GAP_S 1.05
#define PULSE 0.18

enum { EASE_IN_OUT, EASE_OUT_BACK };
enum { SAC_NORMAL, SAC_GLIDE, SAC_MICRO, SAC_SWEEP, SAC_TURN, SAC_PEEK };

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

static bot_eyes_t eyes_now(const bot_t *b) { return b->act_eyes >= 0 ? (bot_eyes_t)b->act_eyes : eyes_for(b->mood); }

static double lid_curve(double u, double speed)
{
    double close = 0.075 * speed, hold = 0.035 * speed, open = 0.15 * speed;
    if (u <= 0) return 0;
    if (u < close) { double v = u / close; return v * v; }
    if (u < close + hold) return 1;
    if (u < close + hold + open) { double v = (u - close - hold) / open; return (1 - v) * (1 - v); }
    return 0;
}
static double heavy_curve(double u, double hold)
{
    if (u <= 0) return 0;
    if (u < 0.25) { double v = u / 0.25; return v * v; }
    if (u < 0.25 + hold) return 1;
    if (u < 0.55 + hold) return 1 - ease(EASE_OUT_BACK, (u - 0.25 - hold) / 0.3);
    return 0;
}
static double blink_length(const bot_t *b) { return b->blink_heavy ? 0.55 + b->blink_hold : (0.075 + 0.035 + 0.15) * b->blink_speed; }
static bool blink_on(const bot_t *b, double t)
{
    return b->blinking && (b->double_blink_pending || t - b->blink_start - fmax(b->blink_lag, 0) < blink_length(b));
}
static double blink_shut_at(const bot_t *b) { return b->blink_start + fmax(b->blink_lag, 0) + 0.075 * b->blink_speed * sqrt(LID_SHUT); }
static double blink_lid(const bot_t *b, double u) { return b->blink_heavy ? heavy_curve(u, b->blink_hold) : lid_curve(u, b->blink_speed); }

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

static double read_floor(const bot_t *b) { return b->rd_moment ? READ_MOMENT_FLOOR : READ_FLOOR; }

static double read_line_y(const bot_t *b)
{
    int last = b->rd_moment ? 2 : 4;
    return -0.05 - 0.06 * (b->rd_line < last ? b->rd_line : last);
}

static void read_new_line(bot_t *b)
{
    b->rd_steps = 3 + (int)(rng_unit(b) * 4);
    b->read_x = -0.65 + jitter(b, 0.03);
}

static void read_new_page(bot_t *b)
{
    b->rd_turn = false;
    b->rd_line = 0;
    b->rd_lines = 4 + (int)(rng_unit(b) * 3);
    read_new_line(b);
}

static void read_target(bot_t *b, double *x, double *y, double *fix, double *mul, bool *blink)
{
    if (b->rd_turn) {
        read_new_page(b);
        *x = b->read_x; *y = read_line_y(b) + jitter(b, 0.02); *mul = 1.3;
        return;
    }
    double r = rng_unit(b);
    if (r < 0.07) { *x = VIEWER_X + jitter(b, 0.05); *y = VIEWER_Y; return; }
    if (r < 0.12 && !b->rd_moment) { *x = MARK_X + jitter(b, 0.04); *y = READ_FLOOR; *fix = rng_range(b, 0.6, 1.0); return; }
    if (b->rd_steps <= 0 || b->read_x >= READ_RIGHT) {
        if (++b->rd_line >= b->rd_lines) {
            b->rd_turn = true;
            *y = rng_unit(b) < 0.5 && !b->rd_moment ? 0.3 : VIEWER_Y;
            *x = jitter(b, 0.08); *fix = 1.2; *blink = true;
            return;
        }
        read_new_line(b);
        *x = b->read_x; *y = read_line_y(b) + jitter(b, 0.02); *mul = 1.3;
        return;
    }
    if (b->read_x > -0.35 && rng_unit(b) < 0.12) {
        b->read_x -= rng_range(b, 0.10, 0.22);
    } else {
        b->read_x = fmin(READ_RIGHT, b->read_x + rng_range(b, 0.18, 0.38));
        b->rd_steps--;
    }
    *x = b->read_x; *y = read_line_y(b) + jitter(b, 0.02);
}

static void next_target(bot_t *b, double *x, double *y)
{
    double r = rng_unit(b);
    switch (b->mood) {
    case BOT_IDLE:
        if (r < 0.45) { *x = REST_X + jitter(b, 0.1); *y = REST_Y + jitter(b, 0.08); return; }
        if (r < 0.7) { *x = VIEWER_X + jitter(b, 0.08); *y = VIEWER_Y + jitter(b, 0.08); return; }
        *x = jitter(b, 0.9); *y = rng_range(b, -0.6, 0.8); return;
    case BOT_WAITING:
        if (r < 0.75) { *x = VIEWER_X + jitter(b, 0.05); *y = VIEWER_Y + jitter(b, 0.05); return; }
        *x = r < 0.875 ? -0.7 : 0.7; *y = jitter(b, 0.2); return;
    case BOT_SLEEPY:
        *x = jitter(b, 0.5); *y = rng_range(b, -0.5, -0.2); return;
    case BOT_ERROR:
        *x = jitter(b, 0.9); *y = jitter(b, 0.7); return;
    case BOT_DONE:
    default:
        if (r < 0.5) { *x = BADGED_REST_X + jitter(b, 0.08); *y = BADGED_REST_Y + jitter(b, 0.06); return; }
        *x = VIEWER_X + jitter(b, 0.06); *y = VIEWER_Y + jitter(b, 0.06); return;
    }
}

static void start_blink(bot_t *b, double t)
{
    b->blinking = true;
    b->blink_start = t;
    b->last_blink_at = t;
    b->blink_lag = rng_range(b, 0, 0.02);
    b->blink_speed = 1;
    b->double_blink_pending = rng_unit(b) < 0.12;
    b->blink_heavy = b->mood == BOT_SLEEPY;
    if (b->blink_heavy) {
        b->blink_hold = rng_range(b, 0.4, 1.2);
        b->double_blink_pending = false;
        b->next_blink_at = t + lognormal(b, 7, 0.4, 3, 15);
    } else {
        b->next_blink_at = t + blink_interval(b);
    }
}

static double breath_period(const bot_t *b)
{
    if (b->asleep) return 6.0;
    switch (b->mood) {
    case BOT_SLEEPY:  return 5.5;
    case BOT_WORKING: return 3.2;
    case BOT_WAITING: return 2.6;
    case BOT_ERROR:   return 3.0;
    case BOT_DONE:    return 3.6;
    default:          return 4.2;
    }
}

static void breathe(bot_t *b, double t)
{
    double dt = t - b->breath_t;
    b->breath_t = t;
    if (dt > 0) b->breath_ph += dt / b->breath_per;
    if (b->breath_ph >= 1) {
        b->breath_ph -= floor(b->breath_ph);
        b->breath_per = breath_period(b) * rng_range(b, 0.85, 1.15);
    }
    b->breath = sin(2 * M_PI * b->breath_ph);
}

static double sleepy_lid(const bot_t *b, double slump) { return slump * (0.80 + 0.12 * b->breath); }

static double heavy_lid(const bot_t *b, double t)
{
    double slump = tween_value(&b->slump, t), heavy;
    if (b->asleep) heavy = tween_value(&b->sleep_lid, t);
    else if (b->mood == BOT_SLEEPY) heavy = fmax(sleepy_lid(b, slump), tween_value(&b->sleep_lid, t));
    else heavy = slump * 0.55;
    return fmax(heavy, tween_value(&b->act_lid, t));
}

static double micro_gap(bot_t *b)
{
    return b->mood == BOT_SLEEPY ? lognormal(b, 2, 0.4, 0.8, 5) : lognormal(b, 0.8, 0.4, 0.3, 2.5);
}

static double blink_cover(bot_t *b, double t)
{
    if (!blink_on(b, t)) {
        start_blink(b, t);
    } else if (b->blink_start > t) {
        b->blink_start = t;
    } else if (t > blink_shut_at(b)) {
        double c = 0.075 * b->blink_speed, u = t - b->blink_start;
        double l = sqrt(lid_curve(u, b->blink_speed)), r = sqrt(lid_curve(u - b->blink_lag, b->blink_speed));
        b->blink_start = t - c * l;
        b->blink_lag = c * (l - r);
        b->last_blink_at = t;
    }
    b->double_blink_pending = false;
    return fmax(0, blink_shut_at(b) - t);
}

static void start_saccade_to(bot_t *b, double t, int kind, double tx, double ty)
{
    bool glide = kind == SAC_GLIDE;
    double fx, fy;
    current_gaze(b, t, &fx, &fy);
    double amp = hypot(tx - fx, ty - fy);
    b->gaze_from_x = fx; b->gaze_from_y = fy;
    b->gaze_to_x = tx; b->gaze_to_y = ty;
    b->gaze_start = t;
    if (kind == SAC_MICRO) {
        b->gaze_dur = MICRO_DUR_S;
        b->gaze_ease = EASE_IN_OUT;
        b->next_micro_at = t + micro_gap(b);
        return;
    }
    b->fix_x = tx; b->fix_y = ty;
    b->gaze_dur = glide ? 0.42 : 0.025 + 0.045 * amp;
    b->gaze_ease = glide ? EASE_IN_OUT : EASE_OUT_BACK;
    if (kind == SAC_TURN || kind == SAC_PEEK || (kind == SAC_SWEEP && amp > COVER_AMP)) {
        double lead = blink_cover(b, t);
        if (amp > COVER_AMP) b->gaze_start += lead;
    } else if (!glide && amp > 0.8 && !blink_on(b, t) && t - b->last_blink_at > 1.2 && rng_unit(b) < 0.6) {
        start_blink(b, t);
    }
    double s = b->gaze_start, lean_dur = glide ? 0.5 : 0.2;
    b->lean_x = tween(tween_value(&b->lean_x, t), tx * 0.045, s + 0.03, lean_dur, EASE_IN_OUT);
    b->lean_y = tween(tween_value(&b->lean_y, t), ty * 0.03, s + 0.03, lean_dur, EASE_IN_OUT);
    b->next_saccade_at = s + b->gaze_dur + fixation(b);
    b->next_micro_at = s + b->gaze_dur + fmax(MICRO_HOLD_S, micro_gap(b));
}

static void start_saccade(bot_t *b, double t, bool glide)
{
    double tx, ty, fix = -1, mul = 1;
    bool blink = false;
    if (b->mood == BOT_WORKING) read_target(b, &tx, &ty, &fix, &mul, &blink);
    else next_target(b, &tx, &ty);
    start_saccade_to(b, t, glide ? SAC_GLIDE : blink ? SAC_TURN : mul != 1 ? SAC_SWEEP : SAC_NORMAL, tx, ty);
    if (fix >= 0 || mul != 1) {
        double hold = b->next_saccade_at - b->gaze_start - b->gaze_dur;
        b->next_saccade_at = b->gaze_start + b->gaze_dur + (fix >= 0 ? fix : hold * mul);
    }
    if (b->mood == BOT_WORKING && !glide && ty - READ_OVERSHOOT * (b->gaze_from_y - ty) < read_floor(b)) b->gaze_ease = EASE_IN_OUT;
}

static void start_micro(bot_t *b, double t)
{
    double cx, cy;
    current_gaze(b, t, &cx, &cy);
    double amp = rng_range(b, 0.012, 0.025) * (b->mood == BOT_SLEEPY ? 0.5 : 1);
    double dx = b->fix_x - cx, dy = b->fix_y - cy;
    double ang = hypot(dx, dy) > 0.015 ? atan2(dy, dx) + jitter(b, 1.0) : rng_range(b, 0, 2 * M_PI);
    double ty = cy + amp * sin(ang);
    if (b->mood == BOT_WORKING && b->fix_y >= read_floor(b) && ty < read_floor(b)) ty = cy - amp * sin(ang);
    start_saccade_to(b, t, SAC_MICRO, cx + amp * cos(ang), ty);
}

static void fall_asleep(bot_t *b, double t)
{
    double from = sleepy_lid(b, tween_value(&b->slump, t));
    b->asleep = true;
    b->sleep_lid = tween(from, 1, t, ASLEEP_CLOSE_S, EASE_IN_OUT);
    start_saccade_to(b, t, SAC_GLIDE, ASLEEP_X, ASLEEP_Y);
    b->next_saccade_at = b->next_blink_at = b->next_micro_at = INFINITY;
}

static void rouse(bot_t *b, double t)
{
    if (b->mood == BOT_SLEEPY && !b->asleep && b->asleep_at < t + BOT_ASLEEP_AFTER_S) b->asleep_at = t + BOT_ASLEEP_AFTER_S;
    if (!b->asleep) return;
    b->asleep = false;
    b->asleep_at = t + BOT_ASLEEP_AFTER_S;
    b->sleep_lid = tween(tween_value(&b->sleep_lid, t), 0, t, STIR_OPEN_S, EASE_IN_OUT);
    b->next_blink_at = t + STIR_OPEN_S + lognormal(b, 7, 0.4, 3, 15);
    b->next_saccade_at = t + STIR_OPEN_S + fixation(b);
    b->next_micro_at = t + STIR_OPEN_S + MICRO_HOLD_S;
}

static void sleep_resume(bot_t *b, double t)
{
    if (b->sleep_left < 0) return;
    if (b->mood == BOT_SLEEPY && !b->asleep) b->asleep_at = t + b->sleep_left;
    b->sleep_left = -1;
}

static void start_hop(bot_t *b, double t, double h, double len)
{
    b->hopping = true;
    b->hop_start = t;
    b->hop_h = h;
    b->hop_len = len;
}

static void wait_restart(bot_t *b, double t)
{
    b->w_t0 = t;
    b->w_phase = 1;
    b->w_pending = false;
    b->next_hop_at = fmin(b->next_hop_at, t + WAIT_FIRST_HOP_S);
    b->burst_hop_at = b->next_peek_at = b->next_nudge_at = b->next_rem_at = INFINITY;
}

static void peek(bot_t *b, double t)
{
    double x = VIEWER_X + jitter(b, 0.05), y = VIEWER_Y + jitter(b, 0.05);
    if (b->acting || b->tracking || t < b->gaze_hold) return;
    start_saccade_to(b, t, SAC_PEEK, x, y);
    b->double_blink_pending = true;
}

static void wait_tick(bot_t *b, double t)
{
    if (b->mood != BOT_WAITING) return;
    double c = b->w_scene ? BOT_WAIT_DEMO_C_S : BOT_WAIT_C_S, rem = b->w_scene ? BOT_WAIT_DEMO_REMIND_S : BOT_WAIT_REMIND_S;
    if (b->w_pending && t - b->w_t0 >= rem) wait_restart(b, t);
    double el = t - b->w_t0;
    int ph = el < BOT_WAIT_B_S ? 1 : el < c ? 2 : 3;
    if (ph != b->w_phase) {
        b->w_phase = ph;
        if (ph == 2) b->next_peek_at = t + rng_range(b, 1.5, 3);
        if (ph == 3) {
            b->next_hop_at = b->next_peek_at = INFINITY;
            b->next_nudge_at = t + rng_range(b, 1, 3);
            b->next_rem_at = b->w_t0 + rem;
        }
    }
    bool loud = bot_anim_allowed(BOT_ANIM_WAIT_HOP, b->calm);
    if (t >= b->next_hop_at && !b->hopping) {
        double h = rng_range(b, 0.8, 1.15), len = rng_range(b, 0.9, 1.1);
        if (loud) start_hop(b, t, h, len);
        if (ph == 1) {
            if (rng_unit(b) < 0.25 && loud) {
                b->burst_hop_at = t + len;
                b->burst_h = BURST_H;
            }
            b->next_hop_at = t + lognormal(b, 2.8, 0.25, 2, 4.5);
        } else {
            b->next_hop_at = ph == 2 ? t + lognormal(b, 8, 0.3, 5, 14) : INFINITY;
        }
    }
    if (t >= b->burst_hop_at && !b->hopping) {
        double len = rng_range(b, 0.9, 1.1);
        if (loud) start_hop(b, t, b->burst_h, len);
        b->burst_hop_at = INFINITY;
    }
    if (ph == 2 && t >= b->next_peek_at) {
        peek(b, t);
        b->next_peek_at = t + rng_range(b, 4, 6);
    }
    if (ph == 3 && t >= b->next_nudge_at && !b->hopping && b->burst_hop_at == INFINITY) {
        if (loud) b->nudge_at = t;
        b->next_nudge_at = t + rng_range(b, 12, 20);
    }
    if (ph == 3 && t >= b->next_rem_at && !b->hopping && t - b->nudge_at >= BOT_NUDGE_S) {
        double len = rng_range(b, 0.9, 1.1);
        if (loud) {
            start_hop(b, t, 1, len);
            b->burst_hop_at = t + fmax(REMIND_GAP_S, len);
            b->burst_h = 1;
        }
        b->next_rem_at += rem;
    }
}

static void beat(bot_t *b, double t)
{
    bool badged = b->mood == BOT_WAITING || b->mood == BOT_DONE;
    b->face = b->mood;
    b->badge = tween(tween_value(&b->badge, t), badged ? 1 : 0, t, 0.35, badged ? EASE_OUT_BACK : EASE_IN_OUT);
    if (b->mood != BOT_DONE) return;
    b->done_hop_at = t + BOT_DONE_HOP_S;
    b->giggle_at = t + BOT_GIGGLE_S;
    b->lap_at = t + BOT_LAP_AT_S;
}

static void swap_eyes(bot_t *b, double t)
{
    if (b->face != b->mood) beat(b, t);
    b->eyes = eyes_now(b);
    b->swap_eyes_on_close = false;
}

static double giggle_lid(const bot_t *b, double t)
{
    for (int k = 0; k < 2; k++) {
        double u = t - b->giggle_at - k * (BOT_GIGGLE_SQUEEZE_S + BOT_GIGGLE_GAP_S);
        if (u >= 0 && u < BOT_GIGGLE_SQUEEZE_S) return BOT_GIGGLE_LID * sin(M_PI * u / BOT_GIGGLE_SQUEEZE_S);
    }
    return 0;
}

static bool lap_on(const bot_t *b, double t) { return t >= b->lap_at && t < b->lap_at + BOT_LAP_S; }

static void reclose(bot_t *b, double t)
{
    b->blinking = true;
    b->blink_start = t;
    b->blink_lag = 0;
    b->blink_heavy = false;
    b->double_blink_pending = false;
    if (b->blink_speed < 1.6) b->blink_speed = 1.6;
}

static void enter(bot_t *b, bot_mood_t m, double t)
{
    bool waking = b->mood == BOT_SLEEPY;
    bool shut = b->asleep || (b->blinking && b->blink_heavy && blink_lid(b, t - b->blink_start) >= 0.5);
    double was = heavy_lid(b, t);
    if (b->blinking) was += (1 - was) * fmax(blink_lid(b, t - b->blink_start), blink_lid(b, t - b->blink_start - b->blink_lag));
    b->mood = m;
    b->mood_since = t;
    b->beat_by = t + BOT_BEAT_MAX_S;
    b->swap_eyes_on_close = true;
    b->asleep = false;
    b->asleep_at = t + BOT_ASLEEP_AFTER_S;
    b->sleep_left = -1;
    b->sleep_lid = tween_const(0);
    if (!b->blinking || b->blink_heavy) {
        start_blink(b, t);
        b->blink_heavy = false;
        if (b->blink_speed < 1.6) b->blink_speed = 1.6;
        if (waking) {
            double h = heavy_lid(b, t), bl = shut || h >= 1 ? 1 : clamp01((was - h) / (1 - h));
            b->blink_start = t - 0.075 * b->blink_speed * sqrt(bl) - b->blink_lag;
        }
    } else {
        b->double_blink_pending = true;
    }
    if (waking) b->double_blink_pending = true;
    if (m == BOT_SLEEPY) b->next_blink_at = t + lognormal(b, 7, 0.4, 3, 15);

    double morph = 0.45;
    b->triangle = tween(tween_value(&b->triangle, t), m == BOT_ERROR ? 1 : 0, t, morph, EASE_IN_OUT);
    b->slump = tween(tween_value(&b->slump, t), m == BOT_SLEEPY ? 1 : 0, t, m == BOT_SLEEPY ? 1.6 : morph, EASE_IN_OUT);
    b->done_hop_at = b->giggle_at = b->lap_at = INFINITY;
    b->popping = m != BOT_SLEEPY && bot_anim_allowed(BOT_ANIM_MOOD_POP, b->calm);
    b->pop_start = t;
    if (m == BOT_WORKING) b->rd_turn = true;
    start_saccade(b, t, true);
    b->w_phase = 0;
    b->next_hop_at = b->burst_hop_at = INFINITY;
    b->nudge_at = -INFINITY;
    if (m == BOT_WAITING) wait_restart(b, t);
}

void bot_set_sleep_after(bot_t *b, double s) { b->sleep_after_s = s < 0 ? 0 : s; }

bool bot_calm(bool quiet, bool calm_setting) { return quiet && calm_setting; }

bool bot_anim_allowed(bot_anim_t a, bool calm) { return !calm || a == BOT_ANIM_USER_INPUT; }

bool bot_chase_on(bool glint, bool calm, bool chase_busy)
{
    return glint && (bot_anim_allowed(BOT_ANIM_GLINT_CHASE, calm) || chase_busy);
}

void bot_set_calm(bot_t *b, bool calm, double t)
{
    if (calm == b->calm) return;
    b->calm = calm;
    if (!calm) bot_wait_restart(b, t);
}

void bot_wait_restart(bot_t *b, double t)
{
    double rem = b->w_scene ? BOT_WAIT_DEMO_REMIND_S : BOT_WAIT_REMIND_S;
    if (b->mood != BOT_WAITING) return;
    if (t - b->w_t0 >= rem) wait_restart(b, t);
    else b->w_pending = true;
}

void bot_wait_scene(bot_t *b, unsigned scene, double t)
{
    if (scene == b->w_scene) return;
    b->w_scene = scene;
    if (b->mood == BOT_WAITING) wait_restart(b, t);
}

void bot_init(bot_t *b, uint64_t seed, double now)
{
    memset(b, 0, sizeof(*b));
    b->rng = seed;
    b->mood = b->face = BOT_IDLE;
    b->eyes = BOT_EYES_DASH;
    b->rim_ready = b->ring = true;
    b->done_hop_at = b->giggle_at = b->lap_at = INFINITY;
    b->mood_since = now;
    b->sleep_after_s = BOT_SLEEP_AFTER_S;
    b->gaze_from_x = b->gaze_to_x = REST_X;
    b->gaze_from_y = b->gaze_to_y = REST_Y;
    b->gaze_start = -1;
    b->gaze_ease = EASE_OUT_BACK;
    b->next_saccade_at = now + 1;
    b->fix_x = REST_X;
    b->fix_y = REST_Y;
    b->next_micro_at = now + 0.8;
    b->read_x = -0.6;
    b->blink_speed = 1;
    b->next_blink_at = now + 0.8;
    b->last_blink_at = -INFINITY;
    b->triangle = b->slump = b->badge = b->lean_x = b->lean_y = b->act_lid = b->sleep_lid = tween_const(0);
    b->breath_per = breath_period(b);
    b->breath_t = now;
    b->asleep_at = INFINITY;
    b->sleep_left = -1;
    b->act_eyes = -1;
    b->next_hop_at = b->burst_hop_at = INFINITY;
    b->next_peek_at = b->next_nudge_at = b->next_rem_at = INFINITY;
    b->nudge_at = b->gaze_hold = -INFINITY;
    b->hop_h = 1;
    b->hop_len = HOP_LEN_S;
    b->animating = true;
}

bool bot_set_mood(bot_t *b, bot_mood_t m, double t)
{
    if (m == b->mood || (m == BOT_IDLE && b->mood == BOT_SLEEPY)) return false;
    enter(b, m, t);
    return true;
}

void bot_rim_ready(bot_t *b, bool ready) { b->rim_ready = ready; }

void bot_set_ring(bot_t *b, bool on) { b->ring = on; }

bool bot_set_mood_user(bot_t *b, bot_mood_t m, double t)
{
    if (!bot_set_mood(b, m, t)) return false;
    if (m != BOT_SLEEPY && bot_anim_allowed(BOT_ANIM_USER_INPUT, b->calm)) {
        b->popping = true;
        b->pop_start = t;
    }
    return true;
}

bot_pose_t bot_pose(bot_t *b, double t)
{
    if (b->mood == BOT_IDLE && b->sleep_after_s > 0 && t - b->mood_since >= b->sleep_after_s) enter(b, BOT_SLEEPY, t);
    if (b->mood == BOT_SLEEPY && !b->asleep && t >= b->asleep_at) fall_asleep(b, t);
    if (!b->blinking && t >= b->next_blink_at && !b->asleep) start_blink(b, t);
    if (t >= b->next_saccade_at && !b->acting && !b->asleep) start_saccade(b, t, false);
    bool moving = b->gaze_start >= 0 && t < b->gaze_start + b->gaze_dur;
    if (!b->acting && !b->tracking && !b->asleep && !moving && t >= b->next_micro_at) {
        if (t + 0.05 < b->next_saccade_at) start_micro(b, t);
        else b->next_micro_at = b->next_saccade_at + MICRO_HOLD_S;
    }
    wait_tick(b, t);
    bool due = b->face != b->mood;
    if (due && t - b->breath_t > BOT_BEAT_GAP_S) b->beat_by = fmax(b->beat_by, t + BOT_BEAT_MAX_S);
    bool loud = bot_anim_allowed(BOT_ANIM_DONE_REWARD, b->calm);
    if (t >= b->done_hop_at && !b->hopping) {
        if (loud) start_hop(b, t, BOT_DONE_HOP_H, HOP_LEN_S);
        b->done_hop_at = INFINITY;
    }

    breathe(b, t);
    bot_pose_t p = {.mood = b->mood, .scale_x = 1, .scale_y = 1, .breath = b->breath, .asleep = b->asleep};
    double slump = tween_value(&b->slump, t), heavy = heavy_lid(b, t);
    double bl = 0, br = 0;
    bool now = !due || b->rim_ready || t >= b->beat_by;
    if (due && !b->blinking) {
        if (heavy >= 0.9 && now) swap_eyes(b, t);
        else if (!b->asleep) reclose(b, t);
    }
    if (b->blinking) {
        double u = t - b->blink_start, shut = 0.075 * b->blink_speed + fmax(b->blink_lag, 0);
        if (due && !b->rim_ready && !b->blink_heavy && t < b->beat_by && u > shut && b->blink_start + shut > b->mood_since - 1e-6) {
            b->blink_start = t - shut;
            u = shut;
        }
        bl = blink_lid(b, u);
        br = blink_lid(b, u - b->blink_lag);
        if (b->swap_eyes_on_close && heavy + (1 - heavy) * fmax(bl, br) >= 0.9 && now) swap_eyes(b, t);
        if (u - fmax(b->blink_lag, 0) >= blink_length(b)) {
            bool beat_due = b->face != b->mood;
            if (b->swap_eyes_on_close && !b->double_blink_pending && !beat_due) swap_eyes(b, t);
            b->blinking = false;
            if (b->double_blink_pending) {
                b->double_blink_pending = false;
                b->blinking = true;
                b->blink_start = t + 0.06;
            } else if (beat_due && !b->asleep) {
                reclose(b, t);
            }
        }
    }
    p.face = b->face;
    p.eyes = b->eyes;
    p.lid_l = heavy + (1 - heavy) * bl;
    p.lid_r = heavy + (1 - heavy) * br;
    double giggle = loud ? giggle_lid(b, t) : 0;
    p.lid_l = fmax(p.lid_l, giggle);
    p.lid_r = fmax(p.lid_r, giggle);
    p.lap = loud && b->ring && lap_on(b, t);
    if (p.lap) p.lap_at = b->lap_at;

    bool sac_active = b->gaze_start >= 0 && t < b->gaze_start + b->gaze_dur;
    current_gaze(b, t, &p.gaze_x, &p.gaze_y);
    if (b->tracking) {
        track_step(b, t);
        p.gaze_x = b->trk_gx;
        p.gaze_y = b->trk_gy;
    }
    p.triangle = tween_value(&b->triangle, t);
    p.slump = slump;
    p.badge = tween_value(&b->badge, t);
    p.offset_x = tween_value(&b->lean_x, t);
    p.offset_y = tween_value(&b->lean_y, t);

    if (b->hopping) {
        double u = t - b->hop_start;
        if (u < b->hop_len) {
            double sx, sy, dy;
            hop_curve(u * HOP_CURVE_LEN / b->hop_len, &sx, &sy, &dy);
            p.scale_x *= 1 + (sx - 1) * BOT_HOP_SQUASH;
            p.scale_y *= 1 + (sy - 1) * BOT_HOP_SQUASH;
            p.offset_y += dy * b->hop_h;
            p.hop_dy = dy * b->hop_h;
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
    double nu = t - b->nudge_at;
    bool nudging = nu >= 0 && nu < BOT_NUDGE_S;
    if (nudging) {
        double sx = 1, sy = 1;
        if (nu < 0.1) bot_rim_variant(-2, &sx, &sy);
        else if (nu < 0.2) bot_rim_variant(1, &sx, &sy);
        p.scale_x *= sx;
        p.scale_y *= sy;
        p.badge *= 1 + PULSE * sin(M_PI * nu / BOT_NUDGE_S);
    }
    b->animating = b->blinking || sac_active || b->tracking || b->hopping || b->popping || nudging || giggle > 0 || p.lap ||
                   tween_active(&b->triangle, t) || tween_active(&b->slump, t) || tween_active(&b->badge, t) ||
                   tween_active(&b->lean_x, t) || tween_active(&b->lean_y, t) || tween_active(&b->act_lid, t) ||
                   tween_active(&b->sleep_lid, t);
    (void)TRANSITION_S;
    return p;
}

static bool act_input(bot_t *b)
{
    if (b->acting && b->sleep_left >= 0) b->sleep_left = BOT_ASLEEP_AFTER_S;
    return b->acting;
}

void bot_look(bot_t *b, double x, double y, double t)
{
    if (act_input(b)) return;
    rouse(b, t);
    start_saccade_to(b, t, SAC_NORMAL, x, y);
    b->next_saccade_at = b->gaze_hold = t + 2.5;
}

#define TRACK_OMEGA 14.0

void bot_track(bot_t *b, double x, double y, double t)
{
    rouse(b, t);
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
    b->next_saccade_at = b->gaze_hold = t + 2.5;
}

void bot_track_end(bot_t *b, double t)
{
    if (!b->tracking) return;
    b->tracking = false;
    b->gaze_from_x = b->gaze_to_x = b->trk_gx;
    b->gaze_from_y = b->gaze_to_y = b->trk_gy;
    b->gaze_start = t;
    b->gaze_dur = 0;
    b->fix_x = b->trk_gx;
    b->fix_y = b->trk_gy;
    b->next_saccade_at = b->gaze_hold = t + 1.0;
    b->next_micro_at = t + MICRO_HOLD_S;
}

void bot_blink(bot_t *b, double t)
{
    if (!b->blinking) start_blink(b, t);
}

void bot_look_still(bot_t *b, double x, double y, double t)
{
    rouse(b, t);
    start_saccade_to(b, t, SAC_NORMAL, x, y);
    b->lean_x = tween(tween_value(&b->lean_x, t), 0, t, 0.2, EASE_IN_OUT);
    b->lean_y = tween(tween_value(&b->lean_y, t), 0, t, 0.2, EASE_IN_OUT);
    b->next_saccade_at = b->gaze_hold = t + 2.5;
}

void bot_read_moment(bot_t *b, bool on, double t)
{
    if (on == b->rd_moment) return;
    b->rd_moment = on;
    if (b->rd_line > 2) b->rd_steps = 0;
    if (!on || b->mood != BOT_WORKING || b->acting || b->tracking) return;
    double gx, gy;
    current_gaze(b, t, &gx, &gy);
    bool moving = b->gaze_start >= 0 && t < b->gaze_start + b->gaze_dur;
    double lo = fmin(fmin(gy, b->fix_y), b->gaze_to_y), hi = fmax(fmax(gy, b->fix_y), b->gaze_to_y);
    if (lo < READ_MOMENT_FLOOR || hi > READ_MOMENT_TOP) start_saccade(b, t, false);
    else if (!moving || b->gaze_ease != EASE_OUT_BACK) return;
    if (t < b->gaze_start && gy >= READ_MOMENT_FLOOR && gy <= READ_MOMENT_TOP) {
        double over = b->gaze_to_y - READ_OVERSHOOT * (gy - b->gaze_to_y);
        if (over < READ_MOMENT_FLOOR || over > READ_MOMENT_TOP) b->gaze_ease = EASE_IN_OUT;
        return;
    }
    b->gaze_start = t - b->gaze_dur;
}

void bot_hold_gaze(bot_t *b, double until)
{
    if (b->next_saccade_at < until) b->next_saccade_at = until;
    if (b->next_micro_at < until) b->next_micro_at = until;
    if (b->gaze_hold < until) b->gaze_hold = until;
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
    if (act_input(b) || !bot_anim_allowed(BOT_ANIM_USER_INPUT, b->calm)) return;
    rouse(b, t);
    start_hop(b, t, 1, HOP_LEN_S);
    b->popping = true; b->pop_start = t;
}

void bot_hop(bot_t *b, double t) { start_hop(b, t, 1, HOP_LEN_S); }

void bot_pop(bot_t *b, double t)
{
    b->popping = true;
    b->pop_start = t;
}

void bot_wake(bot_t *b, double t)
{
    if (b->mood == BOT_SLEEPY) enter(b, BOT_IDLE, t);
}

void bot_sleep(bot_t *b, double t)
{
    if (b->mood != BOT_SLEEPY) enter(b, BOT_SLEEPY, t);
    if (!b->asleep) fall_asleep(b, t);
}

void bot_sleep_in(bot_t *b, double in, double t)
{
    if (b->mood != BOT_SLEEPY) return;
    if (in <= 0) {
        if (!b->asleep) fall_asleep(b, t);
        return;
    }
    rouse(b, t);
    b->asleep_at = t + in;
    b->sleep_left = -1;
}

double bot_asleep_in(const bot_t *b, double t)
{
    if (b->mood != BOT_SLEEPY || b->asleep) return 0;
    if (b->sleep_left >= 0) return b->sleep_left;
    return b->asleep_at > t ? b->asleep_at - t : 0;
}

bool bot_pose_same(const bot_pose_t *a, const bot_pose_t *b, double radius_px)
{
    if (a->mood != b->mood || a->face != b->face || a->eyes != b->eyes || a->asleep != b->asleep || a->lap != b->lap) return false;
    double step = 0.5 / (radius_px < 1 ? 1 : radius_px);
    double breath = step / (a->asleep ? BOT_BREATH_BOB : BOT_BREATH_REACH), lid = step / BOT_LID_REACH;
#define Q(v, s) round((v) / (s))
#define SAME(f, s) (Q(a->f, s) == Q(b->f, s))
    return SAME(gaze_x, step) && SAME(gaze_y, step) && SAME(lid_l, lid) && SAME(lid_r, lid) && SAME(breath, breath) &&
           SAME(triangle, step) && SAME(slump, step) && SAME(badge, step) && SAME(offset_x, step) &&
           SAME(offset_y, step) && SAME(hop_dy, step) && SAME(orbit, step) && SAME(scale_x, step / 2) &&
           SAME(scale_y, step / 2);
#undef SAME
#undef Q
}

int bot_rim_dy(const bot_pose_t *p, double radius_px, double hop_scale)
{
    double x = -radius_px * p->hop_dy * hop_scale;
    int even = 2 * (int)floor(fabs(x) / 2 + 0.5);
    return x < 0 ? -even : even;
}

void bot_rim_variant(int step, double *sx, double *sy)
{
    double u = (double)step / BOT_RIM_STEPS, q = BOT_HOP_SQUASH;
    *sx = u < 0 ? 1 - 0.10 * q * u : 1 - 0.07 * q * u;
    *sy = u < 0 ? 1 + 0.14 * q * u : 1 + 0.10 * q * u;
}

bool bot_label_mark(bot_mood_t m) { return m != BOT_ERROR; }

int bot_rim_tone(bot_mood_t m, bool asleep) { return (int)m | (asleep ? BOT_RIM_ASLEEP : 0); }

int bot_rim_key(bot_mood_t m, bool asleep) { return bot_rim_tone(m, asleep) | (m == BOT_ERROR ? BOT_RIM_TRIANGLE : 0); }

int bot_lap_step(double lap_at, double t, double step_s, int steps)
{
    double idx = floor((t - lap_at) / step_s);
    return idx >= 0 && idx < steps ? (int)idx : -1;
}

bot_mood_t bot_rim_mood(int tone) { return (bot_mood_t)(tone & ~(BOT_RIM_ASLEEP | BOT_RIM_TRIANGLE)); }

float bot_rim_alpha(int tone)
{
    if (tone & BOT_RIM_ASLEEP) return 0.25f;
    bot_mood_t m = bot_rim_mood(tone);
    return m == BOT_IDLE || m == BOT_SLEEPY ? 0.55f : 1.0f;
}

void bot_act_begin(bot_t *b, double t)
{
    if (b->asleep) rouse(b, t);
    if (b->mood == BOT_SLEEPY && b->sleep_left < 0) {
        b->sleep_left = b->asleep_at > t ? b->asleep_at - t : 0;
        b->asleep_at = INFINITY;
    }
    b->acting = true;
    start_saccade_to(b, t, SAC_GLIDE, VIEWER_X, VIEWER_Y);
    b->lean_x = tween(tween_value(&b->lean_x, t), VIEWER_X * 0.045, t, 0.42, EASE_IN_OUT);
    b->lean_y = tween(tween_value(&b->lean_y, t), VIEWER_Y * 0.03, t, 0.42, EASE_IN_OUT);
}

void bot_act_gaze(bot_t *b, double x, double y, bool glide, double t)
{
    bot_tween_t lx = b->lean_x, ly = b->lean_y;
    start_saccade_to(b, t, glide ? SAC_GLIDE : SAC_NORMAL, x, y);
    b->lean_x = lx;
    b->lean_y = ly;
}

void bot_act_lid(bot_t *b, double lid, double dur, double t)
{
    b->act_lid = tween(tween_value(&b->act_lid, t), lid, t, dur, EASE_IN_OUT);
}

void bot_act_eyes(bot_t *b, int eyes, double t)
{
    b->act_eyes = eyes;
    if (b->eyes == eyes_now(b)) return;
    b->swap_eyes_on_close = true;
    if (b->blinking) return;
    start_blink(b, t);
    b->blink_heavy = false;
    if (b->blink_speed < 2.2 && b->mood == BOT_SLEEPY) b->blink_speed = 2.2;
}

void bot_act_blink(bot_t *b, double speed, double t)
{
    if (b->blinking) return;
    start_blink(b, t);
    b->blink_heavy = false;
    b->blink_speed = speed;
    b->double_blink_pending = false;
}

void bot_act_hop(bot_t *b, double t) { start_hop(b, t, 1, HOP_LEN_S); }

void bot_act_end(bot_t *b, bool glide, double lid_dur, double t)
{
    if (!b->acting) return;
    start_saccade_to(b, t, glide ? SAC_GLIDE : SAC_NORMAL, VIEWER_X, VIEWER_Y);
    bot_act_lid(b, 0, lid_dur, t);
    bot_act_eyes(b, -1, t);
    b->acting = false;
    sleep_resume(b, t);
}

void bot_act_drop(bot_t *b, double t)
{
    if (!b->acting) return;
    bot_act_lid(b, 0, 0.3, t);
    bot_act_eyes(b, -1, t);
    b->acting = false;
    sleep_resume(b, t);
}
