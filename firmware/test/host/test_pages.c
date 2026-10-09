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
    CHECK(PAGES_N == 4 && PAGES_N <= KS_MAX_PAGES, "page count %d", PAGES_N);
    const char *ids[PAGES_N + 2];
    int n = pages_ids(ids, PAGES_N + 2);
    CHECK(n == PAGES_N, "pages_ids: %d", n);
    for (int i = 0; i < n && i < 4; i++) {
        CHECK(ids[i] == PAGES[i].id && strcmp(ids[i], WANT[i]) == 0, "id %d: %s", i, ids[i]);
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

static int index_of(const char *id)
{
    for (int i = 0; i < PAGES_N; i++)
        if (strcmp(PAGES[i].id, id) == 0) return i;
    return -1;
}

static void test_route(void)
{
    const page_input_t in = {.detents = 2, .pushes = 1, .longs = 3, .touches = 4, .wakes = 5};
    page_input_t r = pages_route(&PAGES[index_of("bot")], in);
    CHECK(r.detents == 2 && r.pushes == 5 && r.longs == 3 && r.touches == 0 && r.wakes == 0, "bot: taps are pushes");
    r = pages_route(&PAGES[index_of("pomodoro")], in);
    CHECK(r.detents == 2 && r.pushes == 5 && r.longs == 3 && r.touches == 0 && r.wakes == 0, "pomodoro: taps are pushes");
    r = pages_route(&PAGES[index_of("weather")], in);
    CHECK(!r.detents && !r.pushes && !r.longs && !r.touches && !r.wakes, "weather drops input");
    r = pages_route(&PAGES[index_of("nowplaying")], in);
    CHECK(r.detents == 2 && r.pushes == 1 && r.longs == 3 && r.touches == 4 && r.wakes == 5, "nowplaying: raw taps");
    CHECK(PAGES[index_of("bot")].flags & PAGE_GLINT, "bot runs the glint");
    for (int i = 0; i < PAGES_N; i++)
        if (strcmp(PAGES[i].id, "bot") != 0) CHECK(!(PAGES[i].flags & PAGE_GLINT), "%s: no glint", PAGES[i].id);
}

static void test_ops(void)
{
    static const struct {
        const char *id, *show, *input, *frame;
    } WANT[] = {
        {"bot", "page_bot_show", "page_bot_input", "page_bot_frame"},
        {"pomodoro", "page_pomo_show", "page_pomo_input", "page_pomo_frame"},
        {"weather", "page_weather_show", NULL, NULL},
        {"nowplaying", "page_np_show", "page_np_input", "page_np_frame"},
    };
    for (int w = 0; w < 4; w++) {
        int i = index_of(WANT[w].id);
        CHECK(i == w, "%s at %d", WANT[w].id, i);
        if (i < 0) continue;
        const page_desc_t *p = &PAGES[i];
        page_stub_last = NULL;
        p->show(true);
        CHECK(page_stub_last && strcmp(page_stub_last, WANT[w].show) == 0 && page_stub_on, "%s show", p->id);
        p->show(false);
        CHECK(!page_stub_on, "%s hide", p->id);
        CHECK(!p->input == !WANT[w].input && !p->frame == !WANT[w].frame, "%s: hooks", p->id);
        page_input_t in = {0};
        if (p->input) {
            p->input(&in, 0);
            CHECK(strcmp(page_stub_last, WANT[w].input) == 0, "%s input", p->id);
        }
        if (p->frame) {
            p->frame(NULL);
            CHECK(strcmp(page_stub_last, WANT[w].frame) == 0, "%s frame", p->id);
        }
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
    test_ops();
    test_ember_fixtures();
    if (failures) {
        printf("pages: %d failure(s)\n", failures);
        return 1;
    }
    printf("pages: all tests passed\n");
    return 0;
}
