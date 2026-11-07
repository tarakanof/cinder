#include "print_jump_ctl.h"

#include <string.h>

#include "config_store.h"
#include "ember_client.h"
#include "esp_attr.h"
#include "input_route.h"
#include "knob_settings.h"
#include "moments.h"
#include "page_fx.h"
#include "print_jump.h"
#include "scene.h"
#include "usage_view.h"

static pages_nav_t *s_nav;
static const uint32_t *s_nav_gen;
static double s_input_t = -1e9;
static EXT_RAM_BSS_ATTR knob_settings_t s_ks_jump;
static uint32_t s_jump_gen;
static bool s_jump_have, s_jump_listed;

void print_jump_ctl_init(const print_jump_ctx_t *ctx)
{
    s_nav = ctx->nav;
    s_nav_gen = ctx->nav_gen;
    s_input_t = -1e9;
    s_jump_have = false;
}

void print_jump_ctl_input(double t) { s_input_t = t; }

static bool print_listed_in_stored_settings(void)
{
    uint32_t gen = config_store_settings_gen();
    if (s_jump_have && gen == s_jump_gen) return s_jump_listed;
    const char *ids[PAGES_N];
    int idx[PAGES_N], home;
    config_store_settings(&s_ks_jump);
    int n = knob_settings_page_order(&s_ks_jump, ids, pages_ids(ids, PAGES_N), pages_visible(), idx, &home);
    s_jump_listed = false;
    for (int i = 0; i < n; i++)
        if (strcmp(PAGES[idx[i]].id, "print") == 0) s_jump_listed = true;
    s_jump_gen = gen;
    s_jump_have = true;
    return s_jump_listed;
}

void print_jump_ctl_hold(double t)
{
    ember_demo_status_t ds;
    ember_client_demo_status(&ds);
    int pos = pages_find(s_nav, "print");
    bool listed = config_store_settings_gen() == *s_nav_gen ? pos >= 0 : print_listed_in_stored_settings();
    bool real = scene_real(ds.view_on) && !ember_client_offline();
    moments_print_jump(t, listed, pos >= 0 && s_nav->pos == pos, real, true);
}

bool print_jump_ctl_frame(double t, bool real)
{
    int pos = pages_find(s_nav, "print");
    bot_mood_t mood = BOT_IDLE;
    pj_gate_t gate = {.bot_listed = pages_find(s_nav, "bot") >= 0, .mood_known = ember_client_mood(&mood),
                      .waiting = mood == BOT_WAITING, .usage_open = usage_view_open(), .down = input_route_held(),
                      .t = t, .input_t = s_input_t};
    if (!moments_print_jump(t, pos >= 0, pos >= 0 && s_nav->pos == pos, real, pj_blocked(&gate))) return false;
    page_jump(s_nav, pos, t);
    return true;
}
