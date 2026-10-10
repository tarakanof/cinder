#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "cfg.h"
#include "device_api.h"
#include "knob_caps.h"
#include "knob_rotation.h"
#include "knob_view.h"
#include "pages.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static cJSON *body_json(const dev_checkin_t *c, const dev_stats_t *st, char *out, size_t cap)
{
    size_t n = dev_checkin_body(c, st, out, cap);
    CHECK(n > 0 && n < cap, "body fits %zu B", cap);
    cJSON *j = n ? cJSON_Parse(out) : NULL;
    CHECK(cJSON_IsObject(j), "body is JSON: %s", out);
    return j;
}

static bool ids_are(const cJSON *arr, const char *const *want, int n)
{
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) != n) return false;
    for (int i = 0; i < n; i++) {
        const char *s = cJSON_GetStringValue(cJSON_GetArrayItem(arr, i));
        if (!s || strcmp(s, want[i]) != 0) return false;
    }
    return true;
}

static void test_firmware_caps(void)
{
    dev_caps_t caps;
    const char *ids[PAGES_N];
    knob_caps(&caps, ids);
    CHECK(caps.n_pages == PAGES_N, "every page: %d", caps.n_pages);
    for (int i = 0; i < PAGES_N; i++) {
        const char *id = PAGES[i].id;
        bool ok = id[0] >= 'a' && id[0] <= 'z' && strlen(id) <= DEV_CAPS_PAGE_MAX;
        for (const char *p = id; *p; p++) ok &= (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-';
        CHECK(ok, "page id %s matches Ember's ^[a-z][a-z0-9_-]{0,15}$", id);
    }
    for (int i = 0; i < PAGES_N; i++) CHECK(caps.pages[i] == PAGES[i].id, "page %d from the table", i);
    CHECK(caps.view_min == KNOB_VIEW_V_MIN && caps.view_max == KNOB_VIEW_V_MAX, "view range");
    CHECK(caps.view_bytes == KNOB_VIEW_BUF - 1 && caps.view_bytes == 16383, "view_bytes %u", (unsigned)caps.view_bytes);
    CHECK(caps.config_bytes == CFG_SETTINGS_MAX && caps.config_bytes == 1024, "config_bytes %u",
          (unsigned)caps.config_bytes);

    dev_checkin_t c = {.caps = &caps, .fw = "0.9.42", .config_version = 3};
    char out[DEV_CHECKIN_BODY_MAX];
    cJSON *j = body_json(&c, NULL, out, sizeof out);
    CHECK(strncmp(out, "{\"caps\":{\"features\":[", 21) == 0, "caps first, keys sorted: %s", out);
    const cJSON *cj = cJSON_GetObjectItemCaseSensitive(j, "caps");
    const char *pages[PAGES_N];
    for (int i = 0; i < PAGES_N; i++) pages[i] = PAGES[i].id;
    CHECK(ids_are(cJSON_GetObjectItemCaseSensitive(cj, "pages"), pages, PAGES_N), "pages in table order");
    const char *const features[] = {"view_wait", "np_control", "ota_rollback", "coredump", "stats_intervals"};
    CHECK(ids_are(cJSON_GetObjectItemCaseSensitive(cj, "features"), features, 5), "features");
    const cJSON *view = cJSON_GetObjectItemCaseSensitive(cj, "view");
    CHECK(cJSON_GetArraySize(view) == 2 && cJSON_GetArrayItem(view, 0)->valueint == 1 &&
              cJSON_GetArrayItem(view, 1)->valueint == 1,
          "view [1,1]");
    const cJSON *lim = cJSON_GetObjectItemCaseSensitive(cj, "limits");
    CHECK(cJSON_GetObjectItemCaseSensitive(lim, "view_bytes")->valueint == 16383 &&
              cJSON_GetObjectItemCaseSensitive(lim, "config_bytes")->valueint == 1024 && cJSON_GetArraySize(lim) == 2,
          "limits");
    const cJSON *rot = cJSON_GetObjectItemCaseSensitive(cj, "rotations");
    CHECK(cJSON_GetArraySize(rot) == 2 && cJSON_GetArrayItem(rot, 0)->valueint == 0 &&
              cJSON_GetArrayItem(rot, 1)->valueint == 180,
          "rotations [0,180]: %s", out);
    CHECK(strstr(out, "],\"rotations\":[0,180],\"view\":[") != NULL, "rotations sorted between pages and view: %s", out);
    CHECK(cJSON_GetArraySize((cJSON *)cj) == 5, "caps keys: features, limits, pages, rotations, view");
    CHECK(cJSON_GetObjectItemCaseSensitive(j, "config_version")->valueint == 3, "rest of the body follows");
    cJSON_Delete(j);

    c.caps = NULL;
    j = body_json(&c, NULL, out, sizeof out);
    CHECK(!cJSON_GetObjectItemCaseSensitive(j, "caps") && strncmp(out, "{\"config_version\":", 18) == 0,
          "no caps: body as before");
    cJSON_Delete(j);
}

static void test_caps_filtering(void)
{
    const char *pages[] = {"bot", "", NULL, "a\"b", "future-page", "Bot", "1bot", "-bot", "abcdefghijklmnop", "abcdefghijklmnopq",
                           "page_2", "pa ge"};
    const char *features[] = {"view_wait", "Bad", "9lives", "has-dash", "ok_2", "", "a\\b", NULL,
                              "abcdefghijklmnopqrstuvwxyz012345", "abcdefghijklmnopqrstuvwxyz0123456"};
    dev_caps_t caps = {.view_min = 1, .view_max = 2, .pages = pages, .n_pages = 12, .features = features, .n_features = 10};
    dev_checkin_t c = {.caps = &caps, .fw = "x"};
    char out[DEV_CHECKIN_BODY_MAX];
    cJSON *j = body_json(&c, NULL, out, sizeof out);
    const cJSON *cj = cJSON_GetObjectItemCaseSensitive(j, "caps");
    const char *const want_pages[] = {"bot", "future-page", "abcdefghijklmnop", "page_2"};
    const char *const want_features[] = {"view_wait", "ok_2", "abcdefghijklmnopqrstuvwxyz012345"};
    CHECK(ids_are(cJSON_GetObjectItemCaseSensitive(cj, "pages"), want_pages, 4),
          "page ids outside ^[a-z][a-z0-9_-]{0,15}$ left out: %s", out);
    CHECK(ids_are(cJSON_GetObjectItemCaseSensitive(cj, "features"), want_features, 3),
          "tokens outside ^[a-z][a-z0-9_]{0,31}$ left out: %s", out);
    CHECK(!cJSON_GetObjectItemCaseSensitive(cj, "limits"), "no limits when both 0");
    CHECK(!cJSON_GetObjectItemCaseSensitive(cj, "rotations"), "no rotations, no key");
    CHECK(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(cj, "view"), 1)->valueint == 2, "view max");
    cJSON_Delete(j);

    const int rots[] = {0, 45, 90, -180, 180, 270, 360};
    caps.rotations = rots;
    caps.n_rotations = 7;
    j = body_json(&c, NULL, out, sizeof out);
    CHECK(strstr(out, "\"rotations\":[0,90,180,270]") != NULL, "rotations outside 0/90/180/270 left out: %s", out);
    cJSON_Delete(j);
    const int dup[] = {180, 0, 180, 0};
    caps.rotations = dup;
    caps.n_rotations = 4;
    j = body_json(&c, NULL, out, sizeof out);
    CHECK(strstr(out, "\"rotations\":[180,0]") != NULL, "repeats left out: %s", out);
    cJSON_Delete(j);
    const int no_zero[] = {180, 270};
    caps.rotations = no_zero;
    caps.n_rotations = 2;
    j = body_json(&c, NULL, out, sizeof out);
    CHECK(!strstr(out, "rotations"), "no 0: key left out (Ember would drop all caps): %s", out);
    cJSON_Delete(j);
    caps.rotations = NULL;
    caps.n_rotations = 0;

    caps.view_bytes = 100;
    j = body_json(&c, NULL, out, sizeof out);
    const cJSON *lim = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "caps"), "limits");
    CHECK(cJSON_GetArraySize(lim) == 1 && cJSON_GetObjectItemCaseSensitive(lim, "view_bytes"), "one limit: %s", out);
    cJSON_Delete(j);
    caps.view_bytes = 0;
    caps.config_bytes = 7;
    j = body_json(&c, NULL, out, sizeof out);
    lim = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "caps"), "limits");
    CHECK(cJSON_GetArraySize(lim) == 1 && cJSON_GetObjectItemCaseSensitive(lim, "config_bytes"), "other limit: %s", out);
    cJSON_Delete(j);
}

static void fill(char *s, size_t n, char ch)
{
    memset(s, ch, n);
    s[n] = 0;
}

static void test_worst_case_body(void)
{
    dev_caps_t caps;
    const char *ids[PAGES_N];
    knob_caps(&caps, ids);
    dev_diag_t d = {.reset_reason = "pwr_glitch", .boots = UINT32_MAX, .heap_internal_min = UINT32_MAX,
                    .heap_largest_min = UINT32_MAX, .n_tasks = DEV_DIAG_MAX_TASKS, .has_crash = true,
                    .crash_reason = "lvgl_stall", .crash_pc = UINT32_MAX, .has_crash_id = true,
                    .crash_id = UINT32_MAX, .crash_size = UINT32_MAX};
    for (int i = 0; i < DEV_DIAG_MAX_TASKS; i++) {
        fill(d.tasks[i].name, DEV_TASK_NAME_MAX, (char)('a' + i));
        d.tasks[i].stack_free = UINT32_MAX;
    }
    fill(d.crash_task, DEV_TASK_NAME_MAX, 't');
    fill(d.crash_elf, OTA_BUILD_HEX, 'f');
    char build[OTA_BUILD_HEX + 1];
    fill(build, OTA_BUILD_HEX, 'e');
    ota_last_t last = {.attempt = UINT32_MAX, .result = OTA_RES_ROLLED_BACK};
    fill(last.error, sizeof last.error - 1, 'x');
    fill(last.version, sizeof last.version - 1, '9');
    ota_report_t ota = {.rollback = true, .slot = 1, .image = "pending_verify", .phase = "downloading", .last = &last};
    dev_wifi_t w = {.channel = 13, .has_bssid = true, .disconnects = UINT32_MAX, .last_reason = 9999,
                    .has_rssi_min = true, .rssi_min = -127};
    dev_stats_t st = {.period_ms = UINT32_MAX, .n_cpu = DEV_STATS_MAX_CPU, .has_heap_min = true,
                      .heap_internal_min = UINT32_MAX, .has_psram = true, .psram_free = UINT32_MAX,
                      .psram_min = UINT32_MAX, .psram_largest = UINT32_MAX, .has_temp = true, .temp_c = -40.5f,
                      .reset_reason = "cpu_lockup", .has_req = true, .req_ok = UINT32_MAX, .req_fail = UINT32_MAX,
                      .req_ms_avg = 99999.9f, .req_ms_max = UINT32_MAX, .has_frames = true, .fps = 999.9f,
                      .frame_ms_avg = 99999.9f, .frame_ms_max = UINT32_MAX};
    for (int i = 0; i < DEV_STATS_MAX_CPU; i++) st.cpu_pct[i] = 100;
    char fw[33];
    fill(fw, 32, '1');
    dev_checkin_t c = {.caps = &caps, .fw = fw, .fw_build = build, .ota = &ota, .ip = "255.255.255.255", .rssi = -127,
                       .heap_internal_free = UINT32_MAX, .heap_internal_largest = UINT32_MAX, .uptime_s = INT64_MAX,
                       .config_version = UINT32_MAX, .link_mhz = 80, .link_fallback = true, .wifi = &w, .diag = &d};
    char out[DEV_CHECKIN_BODY_MAX];
    size_t n = dev_checkin_body(&c, &st, out, sizeof out);
    CHECK(n > 0, "worst-case body with caps fits DEV_CHECKIN_BODY_MAX");
    printf("caps: worst-case checkin body %zu of %d B\n", n, DEV_CHECKIN_BODY_MAX);
    CHECK(n + 256 < DEV_CHECKIN_BODY_MAX, "256 B headroom: %zu", n);
    cJSON *j = cJSON_Parse(out);
    CHECK(cJSON_IsObject(j), "worst case parses");
    cJSON_Delete(j);
}

int main(void)
{
    test_firmware_caps();
    test_caps_filtering();
    test_worst_case_body();
    if (failures) {
        printf("caps: %d failure(s)\n", failures);
        return 1;
    }
    printf("caps: all tests passed\n");
    return 0;
}
