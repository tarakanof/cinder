#include "chase_ctl.h"

#include <math.h>
#include <stdatomic.h>

#include "bot_view.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orbit_table.h"

static const char *TAG = "cinder";

#define CHASE_LEAD_DEG 15.0
/* Chase gaze reach is tuned against tearing without TE; the eye rate is CINDER1 chase fps (docs/features.md, chase tearing, chase near the ring). */
#define CHASE_GAZE 0.6
#define CHASE_FPS_DEFAULT 60
#define CHASE_OUTER 0.92
static glint_chase_t s_chase;
static atomic_int s_chase_status = 2;
static atomic_int s_chase_req = -1;
static atomic_int s_chase_fps = CHASE_FPS_DEFAULT;
static gc_half_chase_t s_half;

int chase_ctl_request(int style, int fps, int laps)
{
    int st = atomic_load(&s_chase_status);
    if (st != 0) return st;
    if (fps > 0) atomic_store(&s_chase_fps, fps);
    atomic_store(&s_chase_req, style | laps << 4);
    return st;
}

void chase_ctl_status_busy(void) { atomic_store(&s_chase_status, 1); }

void chase_ctl_hold(bool hold) { s_chase.hold = hold; }

bool chase_ctl_busy(void) { return s_chase.phase != GC_IDLE; }

int chase_ctl_fps(void) { return atomic_load(&s_chase_fps); }

chase_ctl_sample_t chase_ctl_sample(void)
{
    return (chase_ctl_sample_t){.phase = (int)s_chase.phase,
                                .laps = s_chase.laps,
                                .fps = atomic_load(&s_chase_fps),
                                .work_s = s_chase.work_s,
                                .wait_s = s_chase.wait_s};
}

void chase_ctl_pause(glint_chase_t *saved, gc_half_chase_t *saved_half)
{
    *saved = s_chase;
    *saved_half = s_half;
    gc_resume(&s_chase, s_chase.work_s, s_chase.wait_s);
}

void chase_ctl_restore(const glint_chase_t *saved, const gc_half_chase_t *saved_half, double saved_t, double t)
{
    gc_restore(&s_chase, saved, saved_t, t);
    gc_half_chase_restore(&s_half, saved_half);
}

#define ORBIT_CHUNK 8
static EXT_RAM_BSS_ATTR bot_orbit_table_t s_orbit;
static TaskHandle_t s_orbit_task;
static atomic_bool s_orbit_done;

static void orbit_task(void *arg)
{
    (void)arg;
    for (int k = 0; k < BOT_ORBIT_KINDS; k++)
        for (int i = 0; i < BOT_ORBIT_TAB_N; vTaskDelay(1)) i = bot_orbit_table_fill_part(&s_orbit, (bot_eyes_t)k, i, ORBIT_CHUNK);
    atomic_store(&s_orbit_done, true);
    for (;;) vTaskSuspend(NULL);
}

void chase_ctl_orbit_start(void)
{
    if (xTaskCreatePinnedToCoreWithCaps(orbit_task, "orbit", 3328, NULL, 1, &s_orbit_task, 1, MALLOC_CAP_SPIRAM) != pdPASS)
        ESP_LOGE(TAG, "orbit task not created");
}

void chase_ctl_orbit_reap(void)
{
    if (!s_orbit_task || !atomic_load(&s_orbit_done)) return;
    vTaskDeleteWithCaps(s_orbit_task);
    s_orbit_task = NULL;
}

void chase_ctl_init(uint32_t seed)
{
    gc_init(&s_chase, seed);
    bot_orbit_table_init(&s_orbit, BOT_VIEW_EYE_SCALE, CHASE_OUTER);
}

static double chase_gaze(const bot_pose_t *p, double deg, double orbit)
{
    if (orbit <= 0) return CHASE_GAZE;
    double ring = bot_orbit_table_get(&s_orbit, p, deg);
    return CHASE_GAZE + (ring - CHASE_GAZE) * orbit;
}

static void chase_track(bot_t *bot, const bot_pose_t *ref, double deg, double orbit, double t)
{
    double m = chase_gaze(ref, deg, orbit), a = deg * M_PI / 180;
    bot_track(bot, cos(a) * m, -sin(a) * m, t);
}

gc_out_t chase_ctl_frame(bot_t *bot, const chase_ctl_in_t *in)
{
    double t = in->t;
    bool glint = bot_chase_on(in->glint, in->calm, s_chase.phase != GC_IDLE);
    int req = in->scene ? in->scene_req : atomic_exchange(&s_chase_req, -1);
    if (req >= 0 && glint && bot_anim_allowed(BOT_ANIM_GLINT_CHASE, in->calm) && gc_start_now(&s_chase, req & 15, req >> 4)) {
        gc_half_chase_reset(&s_half);
    }
    gc_out_t out = {0};
    gc_out_t *chase = &out;
    gc_tick(&s_chase, t, glint, 3.0, chase);
    atomic_store(&s_chase_status, in->scene ? 1 : !glint ? 2 : s_chase.phase != GC_IDLE ? 1 : 0);
    double psi = fmod(t / 3.0, 1.0) * 360.0 - 90.0;
    bot_pose_t ref = {.mood = BOT_WORKING, .eyes = in->eyes, .scale_x = 1, .scale_y = 1};
    if (chase->chasing && glint && chase->style == GC_STYLE_HALF) {
        gc_half_out_t h;
        double deg = 0;
        gc_aim_t aim = gc_half_chase_step(&s_half, psi, CHASE_LEAD_DEG, &h, &deg);
        if (!h.follow) {
            bot_hold_gaze(bot, t + 0.5);
            if (h.blink) bot_blink(bot, t);
        }
        if (aim == GC_AIM_SNAP) {
            if (bot->tracking) bot_track_end(bot, t);
            bot_look_still(bot, chase_gaze(&ref, 0, chase->orbit), 0, t);
        } else if (aim == GC_AIM_TRACK) {
            chase_track(bot, &ref, deg, chase->orbit, t);
        }
    } else if (chase->chasing && glint) {
        chase_track(bot, &ref, psi + CHASE_LEAD_DEG, chase->orbit, t);
    } else if (bot->tracking) {
        bot_track_end(bot, t);
    }
    return out;
}
