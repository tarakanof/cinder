#include "dim_ctl.h"

#include <stdatomic.h>

#include "ember_client.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_client.h"
#include "panel_check.h"

static const char *TAG = "dim";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static atomic_int s_dim_start = -1;
static EXT_RAM_BSS_ATTR dim_fade_t s_fade;
static bool s_fade_on;
static int s_bright_target = -1;
static int s_bright_want = -1;
static bool s_quiet_dim;
static atomic_int s_mood = -1;

static EXT_RAM_BSS_ATTR struct {
    dim_idle_t clock;
    dim_idle_rules_t rules;
    int floor;
    int quiet_dim;
    bool ready;
} s_idle;
static dim_idle_state_t s_idle_seen;
static atomic_int s_awake_level = -1;
static atomic_int s_lit = -1;
static atomic_int s_panel_level = -1;

static int64_t idle_ms(void) { return esp_timer_get_time() / 1000; }

static atomic_int s_wake_logged = DIM_WAKE_NONE;

static void idle_wake_log(dim_wake_t wake)
{
    if (atomic_exchange(&s_wake_logged, wake) != (int)wake) ESP_LOGI(TAG, "idle wake source -> %s", dim_wake_name(wake));
}

static dim_idle_state_t idle_locked(int64_t now, bool verifying)
{
    if (verifying) dim_idle_wake(&s_idle.clock, now, DIM_WAKE_OTA_TRIAL);
    else if (dim_mood_holds(atomic_load(&s_mood))) dim_idle_wake(&s_idle.clock, now, DIM_WAKE_MOOD);
    return dim_idle_state(&s_idle.rules, &s_idle.clock, now);
}

static dim_idle_state_t idle_read(dim_wake_t *wake)
{
    taskENTER_CRITICAL(&s_mux);
    dim_idle_state_t st = idle_locked(idle_ms(), ota_client_verifying());
    *wake = s_idle.clock.wake;
    taskEXIT_CRITICAL(&s_mux);
    return st;
}

static dim_idle_state_t idle_state(void)
{
    dim_wake_t wake;
    dim_idle_state_t st = idle_read(&wake);
    idle_wake_log(wake);
    return st;
}

void dim_ctl_settings(const knob_settings_t *ks)
{
    taskENTER_CRITICAL(&s_mux);
    s_idle.rules = (dim_idle_rules_t){.dim_after_s = ks->idle_dim_after_s,
                                      .dim_level = ks->idle_dim_level,
                                      .off_after_s = ks->idle_off_after_s};
    s_idle.floor = ks->floor;
    s_idle.quiet_dim = ks->quiet_dim;
    s_idle.ready = true;
    taskEXIT_CRITICAL(&s_mux);
}

void dim_ctl_diag(dev_diag_t *d)
{
    (void)idle_state();
    int64_t now = idle_ms();
    taskENTER_CRITICAL(&s_mux);
    dim_idle_state_t st = dim_idle_state(&s_idle.rules, &s_idle.clock, now);
    int age = dim_idle_age_s(&s_idle.clock, now);
    dim_wake_t wake = s_idle.clock.wake;
    taskEXIT_CRITICAL(&s_mux);
    d->has_idle = true;
    d->idle_state = dim_idle_state_name(st);
    d->idle_wake = dim_wake_name(wake);
    d->idle_input_age_s = age;
}

static int fade_floor(void)
{
    return dim_idle_floor(dim_quiet_floor(s_idle.floor, s_quiet_dim, s_idle.quiet_dim), s_idle_seen, &s_idle.rules);
}

static void set_brightness_target(int level)
{
    s_bright_target = level;
    if (s_fade_on) dim_fade_set_target(&s_fade, level);
}

void dim_ctl_apply(void)
{
    static const char *const NAMES[] = {"awake", "dimmed", "off"};
    taskENTER_CRITICAL(&s_mux);
    dim_idle_state_t idle = idle_locked(idle_ms(), ota_client_verifying());
    dim_wake_t wake = s_idle.clock.wake;
    dim_idle_state_t seen = s_idle_seen;
    int target = s_bright_target;
    s_idle_seen = idle;
    if (s_fade_on) dim_fade_set_floor(&s_fade, fade_floor());
    int awake = s_bright_want < 0 ? -1 : dim_quiet_level(s_bright_want, s_idle.floor, s_quiet_dim, s_idle.quiet_dim);
    atomic_store(&s_awake_level, awake);
    int level = awake < 0 ? -1 : dim_idle_level(awake, idle, &s_idle.rules);
    if (level > 0) set_brightness_target(level);
    if (s_fade_on) dim_fade_set_off(&s_fade, idle == DIM_IDLE_OFF);
    taskEXIT_CRITICAL(&s_mux);
    idle_wake_log(wake);
    if (idle != seen) ESP_LOGI(TAG, "idle: %s", NAMES[idle]);
    if (level > 0 && level != target) ESP_LOGI(TAG, "brightness -> %d", level);
}

void dim_ctl_want(int level)
{
    taskENTER_CRITICAL(&s_mux);
    s_bright_want = level;
    taskEXIT_CRITICAL(&s_mux);
    dim_ctl_apply();
}

void dim_ctl_quiet(bool dim)
{
    taskENTER_CRITICAL(&s_mux);
    s_quiet_dim = dim;
    taskEXIT_CRITICAL(&s_mux);
}

void dim_ctl_mood(int mood) { atomic_store(&s_mood, mood); }

static void fade_start(void)
{
    int start = atomic_load(&s_dim_start);
    if (s_fade_on || start < 0) return;
    taskENTER_CRITICAL(&s_mux);
    if (s_idle.ready) {
        dim_fade_init(&s_fade, start);
        dim_fade_set_floor(&s_fade, fade_floor());
        s_fade_on = true;
        if (s_bright_target >= 0) dim_fade_set_target(&s_fade, s_bright_target);
        dim_fade_set_off(&s_fade, s_idle_seen == DIM_IDLE_OFF);
    }
    taskEXIT_CRITICAL(&s_mux);
}

static void fade_step(void)
{
    if (!s_fade_on) return;
    int taken = atomic_exchange(&s_lit, -1);
    dim_wake_t wake;
    dim_idle_state_t idle = idle_read(&wake);
    taskENTER_CRITICAL(&s_mux);
    int lit = dim_fade_take(&s_fade, taken, idle);
    bool moved = idle != s_idle_seen;
    taskEXIT_CRITICAL(&s_mux);
    if (lit >= 0) panel_check_brightness((uint8_t)lit);
    if (moved) dim_ctl_apply();
    uint8_t v;
    bool set;
    int64_t now = idle_ms();
    taskENTER_CRITICAL(&s_mux);
    (void)dim_fade_step(&s_fade, now, &v, &set);
    int cur = s_fade.cur;
    taskEXIT_CRITICAL(&s_mux);
    if (set) panel_check_brightness(v);
    atomic_store(&s_panel_level, cur);
}

void dim_ctl_frame(void)
{
    fade_start();
    fade_step();
}

static bool idle_touch(bool input, dim_wake_t src)
{
    int64_t now = idle_ms();
    taskENTER_CRITICAL(&s_mux);
    dim_idle_state_t was = dim_idle_state(&s_idle.rules, &s_idle.clock, now);
    bool swallow = false;
    if (input) swallow = dim_idle_input(&s_idle.clock, &s_idle.rules, now, src);
    else dim_idle_wake(&s_idle.clock, now, src);
    taskEXIT_CRITICAL(&s_mux);
    int lit = dim_idle_light(was, atomic_load(&s_awake_level), atomic_load(&s_panel_level));
    if (lit >= 0) {
        panel_check_brightness((uint8_t)lit);
        atomic_store(&s_panel_level, lit);
        atomic_store(&s_lit, lit);
    }
    if (was != DIM_IDLE_AWAKE) ember_client_wake();
    return swallow;
}

bool dim_ctl_input(dim_wake_t src) { return idle_touch(true, src); }

void dim_ctl_wake(dim_wake_t src) { (void)idle_touch(false, src); }

bool dim_ctl_off(void) { return idle_state() == DIM_IDLE_OFF; }

void dim_ctl_enable(uint8_t current_level) { atomic_store(&s_dim_start, current_level); }
