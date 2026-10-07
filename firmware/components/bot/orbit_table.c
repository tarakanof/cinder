#include "orbit_table.h"

#include <math.h>

#include "bot_shape.h"

void bot_orbit_table_init(bot_orbit_table_t *t, double eye_scale, double outer)
{
    t->eye_scale = eye_scale;
    t->outer = outer;
    for (int k = 0; k < BOT_ORBIT_KINDS; k++) atomic_store(&t->ready[k], false);
}

int bot_orbit_table_fill_part(bot_orbit_table_t *t, bot_eyes_t eyes, int from, int n)
{
    int k = (int)eyes;
    if (k < 0 || k >= BOT_ORBIT_KINDS || from < 0 || n < 1 || from >= BOT_ORBIT_TAB_N) return BOT_ORBIT_TAB_N;
    int to = n > BOT_ORBIT_TAB_N - from ? BOT_ORBIT_TAB_N : from + n;
    bot_pose_t ref = {.mood = BOT_WORKING, .eyes = eyes, .scale_x = 1, .scale_y = 1};
    for (int i = from; i < to; i++)
        t->gaze[k][i] = (float)bot_orbit_gaze(&ref, t->eye_scale, i * 360.0 / BOT_ORBIT_TAB_N, t->outer);
    if (to == BOT_ORBIT_TAB_N) atomic_store(&t->ready[k], true);
    return to;
}

double bot_orbit_table_get(const bot_orbit_table_t *t, const bot_pose_t *p, double deg)
{
    int k = (int)p->eyes;
    if (k < 0 || k >= BOT_ORBIT_KINDS || !atomic_load(&t->ready[k]) || p->lid_l != 0 || p->lid_r != 0 ||
        p->triangle != 0 || p->slump != 0)
        return bot_orbit_gaze(p, t->eye_scale, deg, t->outer);
    double x = fmod(deg, 360.0);
    if (x < 0) x += 360.0;
    x *= BOT_ORBIT_TAB_N / 360.0;
    int i = (int)x;
    double f = x - i;
    return t->gaze[k][i % BOT_ORBIT_TAB_N] * (1 - f) + t->gaze[k][(i + 1) % BOT_ORBIT_TAB_N] * f;
}
