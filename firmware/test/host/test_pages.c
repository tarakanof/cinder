#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "knob_settings.h"
#include "page_stubs.h"
#include "pages.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *dir;

static int index_of(const char *id)
{
    for (int i = 0; i < PAGES_N; i++)
        if (strcmp(PAGES[i].id, id) == 0) return i;
    return -1;
}

static char *load(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        failures++;
        printf("FAIL: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    size_t got = s ? fread(s, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!s) return NULL;
    s[got] = 0;
    return s;
}

static void test_ids(void)
{
    static const char *const WANT[] = {"bot", "pomodoro", "weather", "nowplaying"};
    enum { N_WANT = sizeof WANT / sizeof WANT[0] };
    CHECK(PAGES_N == N_WANT && PAGES_N <= KS_MAX_PAGES, "page count %d", PAGES_N);
    const char *ids[PAGES_N + 2];
    int n = pages_ids(ids, PAGES_N + 2);
    CHECK(n == PAGES_N, "pages_ids: %d", n);
    for (int i = 0; i < n; i++) {
        CHECK(i >= N_WANT || strcmp(ids[i], WANT[i]) == 0, "id %d: %s", i, ids[i]);
        CHECK(ids[i] == PAGES[i].id, "id %d is the table's", i);
        CHECK(PAGES[i].show != NULL, "%s: no show", ids[i]);
        for (int k = 0; k < i; k++) CHECK(strcmp(ids[i], ids[k]) != 0, "duplicate %s", ids[i]);
        char json[128];
        knob_settings_t ks;
        snprintf(json, sizeof json, "{\"pages\":[{\"id\":\"%s\",\"on\":true}]}", ids[i]);
        CHECK(knob_settings_parse(json, &ks) && ks.n_pages == 1 && strcmp(ks.pages[0].id, ids[i]) == 0,
              "%s: not a valid settings page id", ids[i]);
    }
    CHECK(pages_ids(ids, 2) == 2 && ids[1] == PAGES[1].id, "pages_ids caps at max");
    CHECK(pages_ids(ids, 0) == 0, "pages_ids max 0");
}

static void test_defaults(void)
{
    knob_settings_t d;
    knob_settings_defaults(&d);
    const char *ids[PAGES_N];
    int order[PAGES_N], home = -1;
    int n = knob_settings_page_order(&d, ids, pages_ids(ids, PAGES_N), order, &home);
    CHECK(n == d.n_pages, "every default page is in the table: %d of %d", n, d.n_pages);
    for (int i = 0; i < n; i++) CHECK(order[i] == i, "default page %d is table entry %d", i, order[i]);
    CHECK(home == 0 && strcmp(PAGES[0].id, d.home) == 0, "boot page (table entry 0) is the default home");
    for (int i = n; i < PAGES_N; i++)
        for (int k = 0; k < d.n_pages; k++) CHECK(strcmp(PAGES[i].id, d.pages[k].id) != 0, "%s", PAGES[i].id);
}

static void test_unknown_kept_not_shown(void)
{
    knob_settings_t ks;
    CHECK(knob_settings_parse("{\"pages\":[{\"id\":\"media\",\"on\":true},{\"id\":\"nowplaying\",\"on\":true},"
                              "{\"id\":\"bot\",\"on\":true}],\"home\":\"media\"}",
                              &ks),
          "parse");
    CHECK(ks.n_pages == 3 && strcmp(ks.pages[0].id, "media") == 0, "unknown id kept");
    const char *ids[PAGES_N];
    int order[PAGES_N], home = -1;
    int n = knob_settings_page_order(&ks, ids, pages_ids(ids, PAGES_N), order, &home);
    CHECK(n == 2 && order[0] == 3 && order[1] == 0 && home == 0, "shown: nowplaying, bot; home falls back: n %d", n);
}

#define SAME_IN(a, ...) same_in(a, (page_input_t){__VA_ARGS__})

static bool same_in(page_input_t a, page_input_t b)
{
    return a.detents == b.detents && a.pushes == b.pushes && a.longs == b.longs && a.touches == b.touches &&
           a.wakes == b.wakes;
}

static void test_route(void)
{
    const page_input_t in = {.detents = 2, .pushes = 1, .longs = 3, .touches = 4, .wakes = 5};
    CHECK(SAME_IN(pages_route(&PAGES[index_of("bot")], in), .detents = 2, .pushes = 5, .longs = 3), "bot");
    CHECK(SAME_IN(pages_route(&PAGES[index_of("pomodoro")], in), .pushes = 5, .longs = 3), "pomodoro: no detents");
    CHECK(SAME_IN(pages_route(&PAGES[index_of("weather")], in), 0), "weather drops input");
    CHECK(SAME_IN(pages_route(&PAGES[index_of("nowplaying")], in), .detents = 2, .pushes = 1, .longs = 3,
                  .touches = 4, .wakes = 5),
          "nowplaying: raw taps");
    CHECK(PAGES[index_of("bot")].flags & PAGE_GLINT, "bot runs the glint");
    for (int i = 0; i < PAGES_N; i++)
        if (strcmp(PAGES[i].id, "bot") != 0) CHECK(!(PAGES[i].flags & PAGE_GLINT), "%s: no glint", PAGES[i].id);
}

static pages_nav_t nav_at(int page)
{
    pages_nav_t nav = {.n = PAGES_N, .pos = page, .page = page};
    for (int i = 0; i < PAGES_N; i++) nav.order[i] = i;
    return nav;
}

static void dispatch(const pages_nav_t *nav, page_input_t raw)
{
    page_log_reset();
    pages_dispatch(nav, raw, 1.0);
}

static void test_dispatch(void)
{
    const page_input_t raw = {.detents = 2, .pushes = 3, .longs = 1, .touches = 4, .wakes = 5};
    pages_nav_t nav = nav_at(index_of("bot"));
    dispatch(&nav, raw);
    CHECK(strcmp(page_log, "bot.in ") == 0 && SAME_IN(page_stub_in, .detents = 2, .pushes = 7, .longs = 1),
          "bot gets turns, pushes + taps, longs: %s", page_log);
    nav = nav_at(index_of("pomodoro"));
    dispatch(&nav, raw);
    CHECK(strcmp(page_log, "pomo.in ") == 0 && SAME_IN(page_stub_in, .pushes = 7, .longs = 1),
          "pomodoro: detents dropped, taps are pushes: %s d %d", page_log, page_stub_in.detents);
    nav = nav_at(index_of("weather"));
    dispatch(&nav, raw);
    CHECK(page_log[0] == 0 && page_stub_inputs == 0, "weather: no input hook: %s", page_log);
    nav = nav_at(index_of("nowplaying"));
    dispatch(&nav, raw);
    CHECK(strcmp(page_log, "np.in ") == 0 &&
              SAME_IN(page_stub_in, .detents = 2, .pushes = 3, .longs = 1, .touches = 4, .wakes = 5),
          "nowplaying gets raw input: %s", page_log);
    dispatch(&nav, (page_input_t){.touches = 1, .wakes = 1});
    CHECK(SAME_IN(page_stub_in, .touches = 1, .wakes = 1), "nowplaying: a tap wakes, never a push (play/pause)");
    nav = nav_at(index_of("bot"));
    dispatch(&nav, (page_input_t){.pushes = 3});
    CHECK(page_stub_inputs == 1 && page_stub_in.pushes == 3, "repeated pushes reach the page as a count");
    dispatch(&nav, (page_input_t){.touches = 2});
    CHECK(page_stub_in.pushes == 2 && page_stub_in.touches == 0, "bot: repeated taps are pushes");

    pages_nav_t np_first = {.order = {index_of("nowplaying"), index_of("bot")}, .n = 2, .pos = 0,
                            .page = index_of("nowplaying")};
    dispatch(&np_first, (page_input_t){.touches = 1});
    CHECK(strcmp(page_log, "np.in ") == 0, "dispatch follows the shown page, not the slot: %s", page_log);
    pages_nav_t bot_second = {.order = {index_of("pomodoro"), index_of("bot")}, .n = 2, .pos = 1,
                              .page = index_of("bot")};
    dispatch(&bot_second, (page_input_t){.detents = 1});
    CHECK(strcmp(page_log, "bot.in ") == 0 && page_stub_in.detents == 1, "pos 1 shows bot: %s", page_log);
    page_log_reset();
    pages_frame(&bot_second, NULL);
    pages_frame(&np_first, NULL);
    CHECK(strcmp(page_log, "bot.frame np.frame ") == 0, "frame follows the shown page: %s", page_log);
    nav = nav_at(index_of("weather"));
    page_log_reset();
    pages_frame(&nav, NULL);
    nav = nav_at(index_of("pomodoro"));
    pages_frame(&nav, NULL);
    CHECK(strcmp(page_log, "pomo.frame ") == 0, "weather has no frame hook: %s", page_log);
}

#define CHECK_NAV(nav) \
    CHECK((nav).page == (nav).order[(nav).pos], "page %d is order[pos %d] = %d", (nav).page, (nav).pos, (nav).order[(nav).pos])

static void test_switch(void)
{
    pages_nav_t nav = nav_at(index_of("bot"));
    page_log_reset();
    pages_show(&nav, index_of("nowplaying"));
    CHECK(strcmp(page_log, "bot- pomo- weather- np+ ") == 0 && nav.page == index_of("nowplaying"),
          "bot -> nowplaying hides the rest: %s", page_log);
    page_log_reset();
    pages_hide_all();
    CHECK(strcmp(page_log, "bot- pomo- weather- np- ") == 0, "pause hides every page: %s", page_log);
    nav = (pages_nav_t){.order = {index_of("nowplaying"), index_of("bot")}, .n = 2, .pos = 1, .page = 0};
    page_log_reset();
    pages_resume(&nav);
    CHECK_NAV(nav);
    CHECK(strcmp(page_log, "bot+ pomo- weather- np- ") == 0 && nav.page == index_of("bot"),
          "resume shows order[pos]: %s", page_log);

    knob_settings_t ks;
    knob_settings_defaults(&ks);
    nav = (pages_nav_t){.n = 1};
    page_log_reset();
    pages_settings(&nav, &ks, true);
    CHECK_NAV(nav);
    CHECK(nav.n == 3 && nav.pos == 0 && nav.page == index_of("bot") && strcmp(page_log, "bot+ pomo- weather- np- ") == 0,
          "first settings show home: %s", page_log);
    page_log_reset();
    CHECK(pages_step(&nav, 1) && nav.pos == 1 && nav.page == index_of("pomodoro") &&
              strcmp(page_log, "bot- pomo+ weather- np- ") == 0,
          "step forward: %s", page_log);
    CHECK_NAV(nav);
    CHECK(pages_step(&nav, -2) && nav.pos == 2 && nav.page == index_of("weather"), "step back wraps");
    CHECK_NAV(nav);
    page_log_reset();
    CHECK(!pages_step(&nav, 3) && nav.pos == 2 && page_log[0] == 0, "full lap: no change, no show");
    CHECK_NAV(nav);

    knob_settings_parse("{\"pages\":[{\"id\":\"weather\",\"on\":true},{\"id\":\"bot\",\"on\":true},"
                        "{\"id\":\"pomodoro\",\"on\":true}],\"home\":\"pomodoro\"}",
                        &ks);
    page_log_reset();
    pages_settings(&nav, &ks, false);
    CHECK_NAV(nav);
    CHECK(nav.n == 3 && nav.pos == 0 && nav.page == index_of("weather") && page_log[0] == 0,
          "reorder keeps the shown page in its new slot, no show: pos %d %s", nav.pos, page_log);
    CHECK(pages_step(&nav, 1) && nav.pos == 1 && nav.page == index_of("bot"), "step shows order[pos]: %d", nav.page);
    CHECK_NAV(nav);
    CHECK(pages_step(&nav, -1) && nav.pos == 0 && nav.page == index_of("weather"), "step back: %d", nav.page);
    CHECK_NAV(nav);

    knob_settings_parse("{\"pages\":[{\"id\":\"weather\",\"on\":false},{\"id\":\"bot\",\"on\":true},"
                        "{\"id\":\"pomodoro\",\"on\":true}],\"home\":\"pomodoro\"}",
                        &ks);
    page_log_reset();
    pages_settings(&nav, &ks, false);
    CHECK_NAV(nav);
    CHECK(nav.n == 2 && nav.pos == 1 && nav.page == index_of("pomodoro") &&
              strcmp(page_log, "bot- pomo+ weather- np- ") == 0,
          "shown page disabled: fall back to home: pos %d %s", nav.pos, page_log);

    knob_settings_parse("{\"pages\":[{\"id\":\"nowplaying\",\"on\":true},{\"id\":\"pomodoro\",\"on\":false}],"
                        "\"home\":\"media\"}",
                        &ks);
    page_log_reset();
    pages_settings(&nav, &ks, false);
    CHECK_NAV(nav);
    CHECK(nav.n == 1 && nav.pos == 0 && nav.page == index_of("nowplaying") &&
              strcmp(page_log, "bot- pomo- weather- np+ ") == 0,
          "unknown home: first shown page: %s", page_log);
    page_log_reset();
    CHECK(!pages_step(&nav, 1) && page_log[0] == 0, "one page: no step");
    CHECK_NAV(nav);
}

static void test_ops(void)
{
    static const struct {
        const char *id, *show, *input, *frame;
    } WANT[] = {
        {"bot", "bot+ ", "bot.in ", "bot.frame "},
        {"pomodoro", "pomo+ ", "pomo.in ", "pomo.frame "},
        {"weather", "weather+ ", NULL, NULL},
        {"nowplaying", "np+ ", "np.in ", "np.frame "},
    };
    for (int w = 0; w < (int)(sizeof WANT / sizeof WANT[0]); w++) {
        int i = index_of(WANT[w].id);
        CHECK(i == w, "%s at %d", WANT[w].id, i);
        if (i < 0) continue;
        const page_desc_t *p = &PAGES[i];
        page_log_reset();
        p->show(true);
        CHECK(strcmp(page_log, WANT[w].show) == 0, "%s show: %s", p->id, page_log);
        CHECK(!p->input == !WANT[w].input && !p->frame == !WANT[w].frame, "%s: hooks", p->id);
        page_input_t in = {0};
        page_log_reset();
        if (p->input) p->input(&in, 0);
        if (p->frame) p->frame(NULL);
        char want[64];
        snprintf(want, sizeof want, "%s%s", WANT[w].input ? WANT[w].input : "", WANT[w].frame ? WANT[w].frame : "");
        CHECK(strcmp(page_log, want) == 0, "%s hooks: %s", p->id, page_log);
    }
}

static void test_ember_fixtures(void)
{
    char *j = load("view_full.json");
    cJSON *v = j ? cJSON_Parse(j) : NULL;
    for (int i = 0; v && i < PAGES_N; i++)
        CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(v, PAGES[i].view_block)), "%s: view block %s",
              PAGES[i].id, PAGES[i].view_block);
    CHECK(v, "view_full.json parses");
    cJSON_Delete(v);
    free(j);

    j = load("config_default.json");
    cJSON *c = j ? cJSON_Parse(j) : NULL;
    const cJSON *pages = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(c, "config"), "pages");
    CHECK(cJSON_GetArraySize(pages) == PAGES_N, "Ember default page list has every table page");
    int i = 0;
    const cJSON *pg;
    cJSON_ArrayForEach(pg, pages) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(pg, "id");
        CHECK(i < PAGES_N && cJSON_IsString(id) && strcmp(id->valuestring, PAGES[i].id) == 0, "Ember page %d", i);
        i++;
    }
    cJSON_Delete(c);
    free(j);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <fixture dir>\n", argv[0]);
        return 2;
    }
    dir = argv[1];
    test_ids();
    test_defaults();
    test_unknown_kept_not_shown();
    test_route();
    test_dispatch();
    test_switch();
    test_ops();
    test_ember_fixtures();
    if (failures) {
        printf("pages: %d failure(s)\n", failures);
        return 1;
    }
    printf("pages: all tests passed\n");
    return 0;
}
