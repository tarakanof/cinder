#pragma once

#include <stdatomic.h>
#include <stdbool.h>

#include "bot_behavior.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOT_ORBIT_TAB_N 360
#define BOT_ORBIT_KINDS (BOT_EYES_ANGRY + 1)

typedef struct {
    double eye_scale, outer;
    float gaze[BOT_ORBIT_KINDS][BOT_ORBIT_TAB_N];
    atomic_bool ready[BOT_ORBIT_KINDS];
} bot_orbit_table_t;

void bot_orbit_table_init(bot_orbit_table_t *t, double eye_scale, double outer);
/* Any task. Fills entries [from, from + n) and returns the next index; BOT_ORBIT_TAB_N when the row is done and published. */
int bot_orbit_table_fill_part(bot_orbit_table_t *t, bot_eyes_t eyes, int from, int n);
/* bot_orbit_gaze(p, eye_scale, deg, outer): from the table when the row is ready and p has no
 * lids, triangle or slump, else solved. deg: screen degrees, any range. */
double bot_orbit_table_get(const bot_orbit_table_t *t, const bot_pose_t *p, double deg);

#ifdef __cplusplus
}
#endif
