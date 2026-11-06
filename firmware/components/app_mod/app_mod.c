#include "app_mod.h"

#include <stddef.h>

static int count(const app_mods_t *m) { return m->n < APP_MODS_MAX ? m->n : APP_MODS_MAX; }

bool app_mods_running(app_mods_t *m, int i) { return i >= 0 && i < count(m) && (atomic_load(&m->on) >> i & 1u); }

void app_mods_settings(app_mods_t *m, const knob_settings_t *ks)
{
    for (int i = 0; i < count(m); i++) {
        const app_mod_t *a = m->mods[i];
        unsigned bit = 1u << i;
        if (!(atomic_load(&m->on) & bit)) {
            if (a->start && a->start(ks)) atomic_fetch_or(&m->on, bit);
            continue;
        }
        if (!a->settings || a->settings(ks)) continue;
        atomic_fetch_and(&m->on, ~bit);
        if (a->stop) a->stop();
    }
}

void app_mods_frame(app_mods_t *m, double t)
{
    unsigned on = atomic_load(&m->on);
    for (int i = 0; i < count(m); i++)
        if (on >> i & 1u && m->mods[i]->frame) m->mods[i]->frame(t);
}

void app_mods_checkin(app_mods_t *m, dev_checkin_t *c)
{
    unsigned on = atomic_load(&m->on);
    for (int i = 0; i < count(m); i++)
        if (on >> i & 1u && m->mods[i]->checkin) m->mods[i]->checkin(c);
}

void app_mods_diag(app_mods_t *m, dev_diag_t *d)
{
    unsigned on = atomic_load(&m->on);
    for (int i = 0; i < count(m); i++)
        if (on >> i & 1u && m->mods[i]->diag) m->mods[i]->diag(d);
}
