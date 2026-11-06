#include <stdio.h>
#include <string.h>

#include "app_mod.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char log_buf[512];

static void note(const char *what)
{
    size_t n = strlen(log_buf);
    snprintf(log_buf + n, sizeof log_buf - n, "%s ", what);
}

static bool a_wants = true, b_wants, a_start_ok = true;

static bool a_start(const knob_settings_t *ks)
{
    (void)ks;
    note("a.start");
    return a_wants && a_start_ok;
}
static void a_stop(void) { note("a.stop"); }
static void a_frame(double t)
{
    (void)t;
    note("a.frame");
}
static void a_checkin(dev_checkin_t *c)
{
    c->has_room = true;
    note("a.checkin");
}
static bool a_settings(const knob_settings_t *ks)
{
    (void)ks;
    note("a.settings");
    return a_wants;
}
static void a_diag(dev_diag_t *d)
{
    d->n_tasks++;
    note("a.diag");
}

static bool b_start(const knob_settings_t *ks)
{
    note(ks->room_enabled ? "b.start+" : "b.start-");
    return b_wants && ks->room_enabled;
}
static void b_frame(double t)
{
    note(t > 1 ? "b.frame>1" : "b.frame");
}
static bool b_settings(const knob_settings_t *ks)
{
    note("b.settings");
    return ks->room_enabled;
}

static const app_mod_t A = {"a", a_start, a_stop, a_frame, a_checkin, a_settings, a_diag};
static const app_mod_t B = {.name = "b", .start = b_start, .frame = b_frame, .settings = b_settings};
static const app_mod_t NONE = {.name = "none"};

static void reset(void) { log_buf[0] = 0; }

static void test_order_and_gating(void)
{
    const app_mod_t *const mods[] = {&A, &NONE, &B};
    app_mods_t m = {.mods = mods, .n = 3};
    knob_settings_t ks;
    knob_settings_defaults(&ks);
    dev_checkin_t c = {0};
    dev_diag_t d = {0};

    reset();
    app_mods_frame(&m, 0);
    app_mods_checkin(&m, &c);
    app_mods_diag(&m, &d);
    CHECK(log_buf[0] == 0 && !c.has_room && d.n_tasks == 0, "nothing runs before the first settings: %s", log_buf);

    reset();
    app_mods_settings(&m, &ks);
    CHECK(strcmp(log_buf, "a.start b.start- ") == 0, "the first settings start in table order; no start: never runs: %s", log_buf);
    CHECK(app_mods_running(&m, 0) && !app_mods_running(&m, 1) && !app_mods_running(&m, 2), "a runs, b's settings keep it off");

    reset();
    app_mods_frame(&m, 0.5);
    app_mods_checkin(&m, &c);
    app_mods_diag(&m, &d);
    CHECK(strcmp(log_buf, "a.frame a.checkin a.diag ") == 0 && c.has_room && d.n_tasks == 1, "only running modules: %s", log_buf);

    b_wants = true;
    ks.room_enabled = true;
    reset();
    app_mods_settings(&m, &ks);
    CHECK(strcmp(log_buf, "a.settings b.start+ ") == 0, "a running module gets settings, a stopped one is offered a start: %s", log_buf);
    CHECK(app_mods_running(&m, 2), "b runs");
    reset();
    app_mods_frame(&m, 2);
    CHECK(strcmp(log_buf, "a.frame b.frame>1 ") == 0, "frame in table order with t: %s", log_buf);
    reset();
    app_mods_checkin(&m, &c);
    app_mods_diag(&m, &d);
    CHECK(strcmp(log_buf, "a.checkin a.diag ") == 0, "NULL hooks are skipped: %s", log_buf);

    ks.room_enabled = false;
    a_wants = false;
    reset();
    app_mods_settings(&m, &ks);
    CHECK(strcmp(log_buf, "a.settings a.stop b.settings ") == 0, "settings false stops a module (no stop hook: just off): %s", log_buf);
    CHECK(!app_mods_running(&m, 0) && !app_mods_running(&m, 2), "both off");
    reset();
    app_mods_frame(&m, 0);
    CHECK(log_buf[0] == 0, "a stopped module's frame does not run: %s", log_buf);

    a_wants = true;
    a_start_ok = false;
    reset();
    app_mods_settings(&m, &ks);
    CHECK(strcmp(log_buf, "a.start b.start- ") == 0 && !app_mods_running(&m, 0), "a start that fails stays off: %s", log_buf);
    a_start_ok = true;
    reset();
    app_mods_settings(&m, &ks);
    CHECK(strcmp(log_buf, "a.start b.start- ") == 0 && app_mods_running(&m, 0), "and is retried on the next settings: %s", log_buf);
    CHECK(!app_mods_running(&m, -1) && !app_mods_running(&m, 3), "out of range: not running");
}

static void test_empty_table(void)
{
    const app_mod_t *const mods[] = {NULL};
    app_mods_t m = {.mods = mods, .n = 0};
    knob_settings_t ks;
    knob_settings_defaults(&ks);
    dev_checkin_t c = {0};
    app_mods_settings(&m, &ks);
    app_mods_frame(&m, 1);
    app_mods_checkin(&m, &c);
    CHECK(!c.has_room && !app_mods_running(&m, 0), "an empty table (CONFIG_CINDER_ROOM off) does nothing");
}

int main(void)
{
    test_order_and_gating();
    test_empty_table();
    if (failures) {
        printf("app_mod: %d failure(s)\n", failures);
        return 1;
    }
    printf("app_mod: all tests passed\n");
    return 0;
}
