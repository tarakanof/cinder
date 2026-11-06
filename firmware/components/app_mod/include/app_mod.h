#pragma once

#include <stdatomic.h>
#include <stdbool.h>

#include "device_api.h"
#include "knob_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MODS_MAX 32

/* One app module's hooks; any may be NULL. Every hook but frame runs on the owner task (the ember task), so they never race each other; frame runs on the LVGL task and may race a stop: keep it safe on a stopped module. */
typedef struct {
    const char *name;
    bool (*start)(const knob_settings_t *ks);
    void (*stop)(void);
    void (*frame)(double t);
    void (*checkin)(dev_checkin_t *c);
    bool (*settings)(const knob_settings_t *ks);
    void (*diag)(dev_diag_t *d);
} app_mod_t;

/* mods[0..n): table order; at most APP_MODS_MAX. on: bit i while mods[i] runs. */
typedef struct {
    const app_mod_t *const *mods;
    int n;
    atomic_uint on;
} app_mods_t;

/* Owner task. A stopped module starts when start says the settings enable it (no start: it never runs); a running one gets settings and stops when that returns false. Table order. */
void app_mods_settings(app_mods_t *m, const knob_settings_t *ks);
bool app_mods_running(app_mods_t *m, int i);
/* The running modules' hooks, in table order. */
void app_mods_frame(app_mods_t *m, double t);
void app_mods_checkin(app_mods_t *m, dev_checkin_t *c);
void app_mods_diag(app_mods_t *m, dev_diag_t *d);

#ifdef __cplusplus
}
#endif
