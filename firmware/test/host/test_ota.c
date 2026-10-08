#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "device_api.h"
#include "ota_policy.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define SLOT 0x400000u
#define MIN_MS (60 * 1000)
#define SHA_A "9f2c000000000000000000000000000000000000000000000000000000000001"
#define SHA_B "9f2c000000000000000000000000000000000000000000000000000000000002"

static const char *vectors_dir = "test/vectors";

#define OFFER_JSON(attempt, retry, sha, size, url, ver)                                                              \
    "{\"attempt\":" attempt ",\"auto\":false,\"build\":\"a1b2c3d4\",\"retry\":" retry ",\"sha256\":\"" sha        \
    "\",\"size\":" size ",\"url\":\"" url "\",\"version\":\"" ver "\"}"

static bool parse_offer(const char *json, ota_offer_t *o)
{
    cJSON *root = cJSON_Parse(json);
    bool ok = ota_offer_parse(root, SLOT, o);
    cJSON_Delete(root);
    return ok;
}

static ota_offer_t offer(uint32_t attempt, const char *sha, const char *ver)
{
    ota_offer_t o;
    memset(&o, 0, sizeof o);
    o.attempt = attempt;
    strcpy(o.build, "a1b2c3d4");
    strcpy(o.sha256, sha);
    o.size = 1625408;
    strcpy(o.version, ver);
    return o;
}

static void test_semver(void)
{
    const char *good[] = {"0.9.14", "1.0.0", "10.20.30", "1.0.0-rc.1", "1.0.0-alpha-1", "0.0.0-0a"};
    const char *bad[] = {"", "1.0", "01.0.0", "1.0.0.0", "1.0.0-", "1.0.0-01", "1.0.0+b", "v1.0.0", "1..0",
                         "1.0.0-a..b", "1.0.0-a b", "0.0.0-aaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for (size_t i = 0; i < sizeof good / sizeof *good; i++) CHECK(ota_semver_valid(good[i]), "good %s", good[i]);
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(!ota_semver_valid(bad[i]), "bad %s", bad[i]);
    CHECK(!ota_semver_valid(NULL), "null");
}

static void test_offer(void)
{
    ota_offer_t o;
    CHECK(parse_offer(OFFER_JSON("3", "false", SHA_A, "1625408", "/v1/devices/self/firmware/0.9.14", "0.9.14"), &o),
          "Ember's offer");
    CHECK(o.attempt == 3 && !o.is_auto && !o.retry && o.size == 1625408 && strcmp(o.build, "a1b2c3d4") == 0 &&
              strcmp(o.sha256, SHA_A) == 0 && strcmp(o.version, "0.9.14") == 0,
          "fields");
    CHECK(parse_offer("{\"attempt\":4,\"auto\":true,\"build\":\"a1b2c3d4\",\"extra\":[1],\"retry\":true,\"sha256\":\"" SHA_A
                      "\",\"size\":288,\"url\":\"/v1/devices/self/firmware/1.0.0-rc.1\",\"version\":\"1.0.0-rc.1\"}",
                      &o) &&
              o.is_auto && o.retry && o.size == 288,
          "auto, retry, unknown key ignored, minimum size");
    CHECK(!parse_offer(OFFER_JSON("3", "false", SHA_A, "1625408", "http://evil:80/v1/devices/self/firmware/0.9.14", "0.9.14"), &o),
          "absolute url refused");
    CHECK(!parse_offer(OFFER_JSON("3", "false", SHA_A, "1625408", "/v1/devices/self/firmware/0.9.13", "0.9.14"), &o),
          "url for another version");
    CHECK(!parse_offer(OFFER_JSON("3", "false", SHA_A, "1625408", "/v1/devices/self/firmware/../x", "0.9.14"), &o), "path");
    CHECK(!parse_offer(OFFER_JSON("0", "false", SHA_A, "1625408", "/v1/devices/self/firmware/0.9.14", "0.9.14"), &o),
          "attempt 0");
    CHECK(!parse_offer(OFFER_JSON("1.5", "false", SHA_A, "1625408", "/v1/devices/self/firmware/0.9.14", "0.9.14"), &o),
          "attempt not integral");
    CHECK(!parse_offer(OFFER_JSON("3", "false", "9F2C", "1625408", "/v1/devices/self/firmware/0.9.14", "0.9.14"), &o),
          "short sha");
    CHECK(!parse_offer(OFFER_JSON("3", "false", "9F2C000000000000000000000000000000000000000000000000000000000001",
                                  "1625408", "/v1/devices/self/firmware/0.9.14", "0.9.14"),
                       &o),
          "upper-case sha");
    CHECK(!parse_offer(OFFER_JSON("3", "false", SHA_A, "4194305", "/v1/devices/self/firmware/0.9.14", "0.9.14"), &o),
          "bigger than the slot");
    CHECK(parse_offer(OFFER_JSON("3", "false", SHA_A, "4194304", "/v1/devices/self/firmware/0.9.14", "0.9.14"), &o),
          "exactly the slot");
    CHECK(!parse_offer(OFFER_JSON("3", "false", SHA_A, "100", "/v1/devices/self/firmware/0.9.14", "0.9.14"), &o),
          "smaller than the header");
    CHECK(!parse_offer(OFFER_JSON("3", "false", SHA_A, "1625408", "/v1/devices/self/firmware/v0.9.14", "v0.9.14"), &o),
          "not semver");
    CHECK(!parse_offer("{\"attempt\":3,\"sha256\":\"" SHA_A "\",\"size\":1625408,\"url\":\"/v1/devices/self/firmware/0.9.14\","
                       "\"version\":\"0.9.14\"}",
                       &o),
          "no build");
    CHECK(!parse_offer("[]", &o) && !parse_offer("null", &o), "not an object");
}

static void test_reply(void)
{
    dev_checkin_result_t r;
    dev_checkin_parse("{\"config_version\":1,\"ota\":" OFFER_JSON("7", "true", SHA_A, "1625408",
                                                                  "/v1/devices/self/firmware/0.9.16", "0.9.16") "}",
                      &r);
    CHECK(r.ok && r.has_ota && r.ota.attempt == 7 && r.ota.retry && strcmp(r.ota.version, "0.9.16") == 0, "reply ota");
    dev_checkin_result_free(&r);
    dev_checkin_parse("{\"config_version\":1}", &r);
    CHECK(r.ok && !r.has_ota, "no offer");
    dev_checkin_parse("{\"config_version\":1,\"ota\":{\"attempt\":1}}", &r);
    CHECK(r.ok && !r.has_ota, "malformed offer ignored");
}

static void test_records(void)
{
    ota_rec_t r;
    memset(&r, 0, sizeof r);
    r.last.result = OTA_RES_ROLLED_BACK;
    r.last.attempt = 2;
    ota_offer_t o = offer(3, SHA_A, "0.9.16");
    ota_rec_start(&r, &o);
    CHECK(r.att_state == OTA_ATT_DL && r.att_attempt == 3 && strcmp(r.att_sha, SHA_A) == 0 &&
              strcmp(r.att_ver, "0.9.16") == 0 && strcmp(r.att_build, "a1b2c3d4") == 0,
          "start stores the attempt");
    CHECK(r.last.result == OTA_RES_NONE, "a new attempt clears last");
    ota_rec_fail(&r, "sha256", true);
    CHECK(r.att_state == OTA_ATT_NONE && r.att_attempt == 0 && r.last.result == OTA_RES_FAILED && r.last.attempt == 3 &&
              strcmp(r.last.error, "sha256") == 0 && strcmp(r.last.version, "0.9.16") == 0,
          "fail");
    CHECK(strcmp(r.bad, SHA_A) == 0, "sha256 failure blocks the image");

    ota_offer_t next = offer(4, SHA_A, "0.9.16");
    CHECK(ota_rec_refuse(&r, &next), "refuse records");
    CHECK(r.last.attempt == 4 && r.last.result == OTA_RES_FAILED && strcmp(r.last.error, "refused") == 0, "refused");
    CHECK(!ota_rec_refuse(&r, &next), "refused once per attempt");
    CHECK(!ota_rec_retry(&r, &next), "no retry flag: guard stays");
    next.retry = true;
    CHECK(ota_rec_retry(&r, &next) && r.bad[0] == 0, "retry clears the guard");
    CHECK(!ota_rec_retry(&r, &next), "nothing left to clear");

    ota_offer_t c = offer(5, SHA_B, "0.9.17");
    ota_rec_start(&r, &c);
    ota_rec_fail(&r, "net", false);
    CHECK(r.bad[0] == 0, "network failure does not block");
    ota_rec_start(&r, &c);
    ota_rec_rebooting(&r);
    CHECK(r.att_state == OTA_ATT_BOOT, "rebooting");
    ota_rec_valid(&r);
    CHECK(r.att_state == OTA_ATT_NONE && r.last.result == OTA_RES_OK && r.last.attempt == 5 && r.last.error[0] == 0 &&
              strcmp(r.last.version, "0.9.17") == 0,
          "valid");
    ota_rec_valid(&r);
    CHECK(r.last.result == OTA_RES_OK && r.last.attempt == 5, "valid without an attempt keeps last");
}

static void test_boot(void)
{
    ota_rec_t r;
    ota_offer_t o = offer(9, SHA_A, "0.9.16");
    ota_boot_in_t old = {.running_build = "77aa01ff", .running_ver = "0.9.15"};
    ota_boot_in_t fresh = {.pending_verify = true, .running_build = "a1b2c3d4", .running_ver = "0.9.16"};

    memset(&r, 0, sizeof r);
    CHECK(ota_rec_boot(&r, &old) == OTA_BOOT_NOTHING, "nothing");
    CHECK(ota_rec_boot(&r, &fresh) == OTA_BOOT_VERIFY, "pending without a record still verifies");

    ota_rec_start(&r, &o);
    CHECK(ota_rec_boot(&r, &old) == OTA_BOOT_INTERRUPTED, "reset mid-download");
    CHECK(r.last.result == OTA_RES_FAILED && strcmp(r.last.error, "interrupted") == 0 && r.last.attempt == 9 &&
              r.att_state == OTA_ATT_NONE && r.bad[0] == 0,
          "interrupted");

    memset(&r, 0, sizeof r);
    ota_rec_start(&r, &o);
    ota_rec_rebooting(&r);
    CHECK(ota_rec_boot(&r, &fresh) == OTA_BOOT_VERIFY && r.att_state == OTA_ATT_BOOT, "first boot of the new image");
    CHECK(ota_rec_boot(&r, &fresh) == OTA_BOOT_VERIFY, "a reboot while pending: still verifying");
    CHECK(ota_rec_rollback(&r, "no_checkin"), "rollback record changed");
    CHECK(!ota_rec_rollback(&r, "no_checkin"), "a repeated rollback writes nothing");
    CHECK(r.last.result == OTA_RES_ROLLED_BACK && strcmp(r.last.error, "no_checkin") == 0 && r.att_state == OTA_ATT_BOOT &&
              strcmp(r.bad, SHA_A) == 0,
          "no_checkin recorded before the reboot");
    ota_boot_in_t back = old;
    back.has_invalid = true;
    back.invalid_build = "a1b2c3d4";
    back.invalid_ver = "0.9.16";
    CHECK(ota_rec_boot(&r, &back) == OTA_BOOT_ROLLED_BACK, "old image after the rollback");
    CHECK(r.last.result == OTA_RES_ROLLED_BACK && strcmp(r.last.error, "no_checkin") == 0 && r.last.attempt == 9 &&
              r.att_state == OTA_ATT_NONE,
          "no_checkin kept");

    memset(&r, 0, sizeof r);
    ota_rec_start(&r, &o);
    ota_rec_rebooting(&r);
    CHECK(ota_rec_boot(&r, &back) == OTA_BOOT_ROLLED_BACK, "bootloader rolled back (crash in pending verify)");
    CHECK(r.last.result == OTA_RES_ROLLED_BACK && strcmp(r.last.error, "boot") == 0 && strcmp(r.bad, SHA_A) == 0,
          "rolled_back/boot, image blocked");

    memset(&r, 0, sizeof r);
    ota_rec_start(&r, &o);
    ota_rec_rebooting(&r);
    ota_boot_in_t valid = fresh;
    valid.pending_verify = false;
    CHECK(ota_rec_boot(&r, &valid) == OTA_BOOT_OK && r.last.result == OTA_RES_OK, "new image already valid");

    memset(&r, 0, sizeof r);
    ota_rec_start(&r, &o);
    ota_rec_rebooting(&r);
    CHECK(ota_rec_boot(&r, &old) == OTA_BOOT_INTERRUPTED, "something else was flashed (USB)");

    memset(&r, 0, sizeof r);
    ota_rec_start(&r, &o);
    ota_rec_rebooting(&r);
    ota_boot_in_t other_inv = old;
    other_inv.has_invalid = true;
    other_inv.invalid_build = "0badf00d";
    other_inv.invalid_ver = "0.9.12";
    CHECK(ota_rec_boot(&r, &other_inv) == OTA_BOOT_INTERRUPTED && strcmp(r.last.error, "interrupted") == 0 &&
              r.bad[0] == 0,
          "the invalid slot holds another image: not this attempt's rollback");

    memset(&r, 0, sizeof r);
    ota_rec_start(&r, &o);
    ota_rec_ready(&r);
    CHECK(r.att_state == OTA_ATT_READY, "verified, waiting for the Pomodoro");
    CHECK(ota_rec_boot(&r, &old) == OTA_BOOT_INTERRUPTED && strcmp(r.last.error, "reset_waiting") == 0 &&
              r.last.result == OTA_RES_FAILED && r.bad[0] == 0 && r.att_state == OTA_ATT_NONE,
          "reset while waiting: own label, image not blocked");

    memset(&r, 0, sizeof r);
    ota_rec_start(&r, &o);
    ota_rec_ready(&r);
    ota_rec_rebooting(&r);
    CHECK(ota_rec_boot(&r, &fresh) == OTA_BOOT_VERIFY, "power cut after the boot slot was set: new image verifies");
    ota_rec_valid(&r);
    CHECK(r.last.result == OTA_RES_OK && r.last.attempt == 9, "and reports ok once confirmed");

    memset(&r, 0, sizeof r);
    ota_rec_start(&r, &o);
    ota_rec_ready(&r);
    ota_rec_rebooting(&r);
    CHECK(ota_rec_boot(&r, &old) == OTA_BOOT_INTERRUPTED, "power cut before the slot switch completed: old image");

    memset(&r, 0, sizeof r);
    ota_rec_start(&r, &o);
    ota_rec_rebooting(&r);
    r.att_build[0] = 0;
    CHECK(ota_rec_boot(&r, &fresh) == OTA_BOOT_VERIFY, "record without a build matches by version");
}

static void test_gate(void)
{
    ota_gate_t g;
    memset(&g, 0, sizeof g);
    ota_rec_t rec;
    memset(&rec, 0, sizeof rec);
    ota_offer_t o = offer(3, SHA_A, "0.9.16");
    ota_env_t e = {.rollback = true, .image_valid = true, .online = true, .paired = true, .now_ms = 0, .last_input_ms = -1};

    CHECK(ota_start_decide(&g, &rec, &o, &e) == OTA_GO, "go");
    ota_env_t x = e;
    x.rollback = false;
    CHECK(ota_start_decide(&g, &rec, &o, &x) == OTA_WAIT_NOT_READY, "no rollback bootloader");
    x = e;
    x.image_valid = false;
    CHECK(ota_start_decide(&g, &rec, &o, &x) == OTA_WAIT_NOT_READY, "pending image");
    x = e;
    x.pomo_active = true;
    CHECK(ota_start_decide(&g, &rec, &o, &x) == OTA_WAIT_POMODORO, "pomodoro");

    ota_offer_t a = o;
    a.is_auto = true;
    x = e;
    x.now_ms = 20 * MIN_MS;
    x.last_input_ms = 11 * MIN_MS;
    CHECK(ota_start_decide(&g, &rec, &a, &x) == OTA_WAIT_INPUT, "auto: input 9 min ago");
    x.last_input_ms = 10 * MIN_MS;
    CHECK(ota_start_decide(&g, &rec, &a, &x) == OTA_GO, "auto: 10 min idle");
    x.last_input_ms = -1;
    CHECK(ota_start_decide(&g, &rec, &a, &x) == OTA_GO, "auto: no input since boot");
    x.last_input_ms = 19 * MIN_MS;
    CHECK(ota_start_decide(&g, &rec, &o, &x) == OTA_GO, "manual ignores input");

    ota_gate_started(&g);
    ota_gate_failed(&g, 0);
    x = e;
    x.now_ms = MIN_MS - 1;
    CHECK(ota_start_decide(&g, &rec, &o, &x) == OTA_WAIT_BACKOFF, "1 min backoff");
    x.now_ms = MIN_MS;
    CHECK(ota_start_decide(&g, &rec, &o, &x) == OTA_GO, "after 1 min");
    ota_gate_started(&g);
    ota_gate_failed(&g, MIN_MS);
    x.now_ms = 6 * MIN_MS - 1;
    CHECK(ota_start_decide(&g, &rec, &o, &x) == OTA_WAIT_BACKOFF, "5 min backoff");
    CHECK(ota_backoff_ms(3) == 30 * MIN_MS && ota_backoff_ms(9) == 30 * MIN_MS && ota_backoff_ms(0) == 0, "30 min cap");

    ota_offer_t r = o;
    r.attempt = 4;
    r.retry = true;
    CHECK(ota_start_decide(&g, &rec, &r, &x) == OTA_GO && g.started == 0, "retry resets the backoff and the count");
    for (int i = 0; i < OTA_ATTEMPTS_PER_BOOT; i++) {
        CHECK(ota_start_decide(&g, &rec, &r, &e) == OTA_GO, "start %d of attempt 4", i + 1);
        ota_gate_started(&g);
    }
    CHECK(g.started == 3, "three started");
    CHECK(ota_start_decide(&g, &rec, &r, &e) == OTA_REFUSE_CAP, "per-boot cap for this attempt");
    ota_offer_t retry2 = o;
    retry2.attempt = 6;
    retry2.retry = true;
    CHECK(ota_start_decide(&g, &rec, &retry2, &e) == OTA_GO && g.started == 0, "Ember's Retry (new attempt) goes at once");
    for (int i = 0; i < OTA_ATTEMPTS_PER_BOOT; i++) ota_gate_started(&g);
    ota_offer_t same = retry2;
    same.retry = false;
    CHECK(ota_start_decide(&g, &rec, &same, &e) == OTA_REFUSE_CAP, "the same attempt stays capped");
    ota_offer_t other = offer(7, SHA_B, "0.9.17");
    CHECK(ota_start_decide(&g, &rec, &other, &e) == OTA_GO && g.started == 0, "another image: new count");

    memset(&g, 0, sizeof g);
    strcpy(rec.bad, SHA_A);
    CHECK(ota_start_decide(&g, &rec, &o, &e) == OTA_REFUSE_BAD, "bad image");
    x = e;
    x.pomo_active = true;
    CHECK(ota_start_decide(&g, &rec, &o, &x) == OTA_REFUSE_BAD, "a lasting refusal wins over a pomodoro");
    CHECK(strcmp(ota_decision_name(OTA_REFUSE_CAP), "attempt_cap") == 0, "names");
}

static void test_resume(void)
{
    char err[OTA_ERROR_MAX + 1];
    CHECK(ota_resume_delay_ms(0) == 5000 && ota_resume_delay_ms(1) == 15000 && ota_resume_delay_ms(2) == 30000 &&
              ota_resume_delay_ms(3) == -1,
          "5, 15, 30 s, then none");
    CHECK(ota_resumable(-1) && ota_resumable(500) && ota_resumable(503) && !ota_resumable(409) && !ota_resumable(200) &&
              !ota_resumable(404),
          "resumable");
    CHECK(ota_resp_check(200, 0, false, 0, 1000, 1000, err) == OTA_RESP_CONTINUE, "fresh 200");
    CHECK(ota_resp_check(200, 0, false, 0, 999, 1000, err) == OTA_RESP_FAIL && strcmp(err, "size") == 0,
          "Content-Length != size");
    CHECK(ota_resp_check(200, 0, false, 0, -1, 1000, err) == OTA_RESP_FAIL, "no Content-Length");
    CHECK(ota_resp_check(206, 400, true, 400, 600, 1000, err) == OTA_RESP_CONTINUE, "206 resume");
    CHECK(ota_resp_check(206, 400, true, 300, 700, 1000, err) == OTA_RESP_FAIL, "206 from another offset");
    CHECK(ota_resp_check(206, 400, false, 0, 600, 1000, err) == OTA_RESP_FAIL, "206 without Content-Range");
    CHECK(ota_resp_check(206, 400, true, 400, 500, 1000, err) == OTA_RESP_FAIL, "206 short");
    CHECK(ota_resp_check(206, 0, true, 0, 1000, 1000, err) == OTA_RESP_FAIL, "206 without a Range request");
    CHECK(ota_resp_check(200, 400, false, 0, 1000, 1000, err) == OTA_RESP_RESTART, "If-Range mismatch: whole image");
    CHECK(ota_resp_check(409, 0, false, 0, 30, 1000, err) == OTA_RESP_FAIL && strcmp(err, "http_409") == 0, "409");
    ota_http_error(-1, err);
    CHECK(strcmp(err, "http_0") == 0, "no status");
    uint32_t s = 0;
    CHECK(ota_content_range_start("bytes 400-999/1000", &s) && s == 400, "Content-Range");
    CHECK(!ota_content_range_start("bytes */1000", &s) && !ota_content_range_start("items 1-2/3", &s) &&
              !ota_content_range_start("bytes 99999999999-1/2", &s) && !ota_content_range_start(NULL, &s),
          "bad Content-Range");
}

static void test_header(void)
{
    char path[512];
    snprintf(path, sizeof path, "%s/ota_header_0.9.16.bin", vectors_dir);
    FILE *f = fopen(path, "rb");
    CHECK(f, "fixture %s", path);
    if (!f) return;
    uint8_t h[OTA_HEADER_LEN];
    size_t n = fread(h, 1, sizeof h, f);
    fclose(f);
    CHECK(n == OTA_HEADER_LEN, "288 bytes");
    CHECK(ota_header_check(h, n, "0.9.16", NULL, "v5.5.5") == NULL, "real header passes");
    CHECK(ota_header_check(h, n, "0.9.16", NULL, NULL) == NULL, "unknown bootloader IDF: no IDF check");
    CHECK(ota_header_check(h, n, "0.9.16", "62d23b87", "v5.5.5") == NULL, "offered build matches");
    CHECK(ota_header_check(h, n, "0.9.16", "62d23b88", "v5.5.5") != NULL, "another build under the offered version");
    CHECK(ota_header_check(h, n, "0.9.17", NULL, "v5.5.5") != NULL, "offered another version");
    CHECK(ota_header_check(h, n, "0.9.16", NULL, "v6.0") != NULL, "IDF major mismatch");
    CHECK(ota_header_check(h, n, "0.9.16", NULL, "v5.4.1") == NULL, "IDF minor differs: fine");
    CHECK(ota_header_check(h, n - 1, "0.9.16", NULL, "v5.5.5") != NULL, "short");
    uint8_t b[OTA_HEADER_LEN];
    memcpy(b, h, n);
    b[0] = 0xE8;
    CHECK(ota_header_check(b, n, "0.9.16", NULL, "v5.5.5") != NULL, "image magic");
    memcpy(b, h, n);
    b[12] = 0;
    CHECK(ota_header_check(b, n, "0.9.16", NULL, "v5.5.5") != NULL, "chip id");
    memcpy(b, h, n);
    b[33] ^= 1;
    CHECK(ota_header_check(b, n, "0.9.16", NULL, "v5.5.5") != NULL, "app desc magic");
    memcpy(b, h, n);
    memcpy(b + 80, "ember\0", 6);
    CHECK(ota_header_check(b, n, "0.9.16", NULL, "v5.5.5") != NULL, "project");
    CHECK(strcmp(ota_header_check(b, n, "0.9.16", NULL, "v5.5.5"), "desc") == 0, "error name");
    CHECK(ota_idf_major("v5.5.5") == 5 && ota_idf_major("5.1") == 5 && ota_idf_major("v12.0") == 12 &&
              ota_idf_major("vx") == -1 && ota_idf_major(NULL) == -1,
          "idf major");
    char build[9];
    ota_build_hex(h + 176, build);
    CHECK(strcmp(build, "62d23b87") == 0, "build = first 4 bytes of app_elf_sha256: %s", build);
}

static void test_progress_and_timers(void)
{
    CHECK(ota_pct(0, 1000) == 0 && ota_pct(999, 1000) == 99 && ota_pct(1000, 1000) == 100 && ota_pct(5, 0) == 0 &&
              ota_pct(2000, 1000) == 100 && ota_pct(4194304, 4194304) == 100,
          "pct");
    int changes = 0, last = -1;
    for (uint32_t w = 0; w <= 1625408; w += 4096) {
        int p = ota_pct(w, 1625408);
        if (p != last) changes++;
        last = p;
    }
    CHECK(changes <= 101, "face redraws only on whole percents: %d", changes);

    ota_valid_in_t v = {.uptime_ms = 60000, .checkin_ok = true, .frame = true, .view_ok = true, .health = OTA_HEALTH_PASS};
    CHECK(ota_valid_ready(&v), "valid at 60 s");
    v.health = OTA_HEALTH_PENDING;
    CHECK(!ota_valid_ready(&v), "needs health");
    v.health = OTA_HEALTH_FAIL;
    CHECK(!ota_valid_ready(&v), "not with failed health");
    v.health = OTA_HEALTH_PASS;
    v.uptime_ms = 59999;
    CHECK(!ota_valid_ready(&v), "not before 60 s");
    v.uptime_ms = 120000;
    v.frame = false;
    CHECK(!ota_valid_ready(&v), "needs a frame");
    v.frame = true;
    v.view_ok = false;
    CHECK(!ota_valid_ready(&v), "needs a view poll");
    v.view_ok = true;
    v.checkin_ok = false;
    CHECK(!ota_valid_ready(&v), "needs a good checkin");

    CHECK(!ota_rollback_due(30 * MIN_MS - 1, false, OTA_ROLLBACK_MS), "not before 30 min");
    CHECK(ota_rollback_due(30 * MIN_MS, false, OTA_ROLLBACK_MS), "30 min");
    CHECK(!ota_rollback_due(30 * MIN_MS, true, OTA_ROLLBACK_MS), "deferred during a pomodoro");
    CHECK(!ota_rollback_due(90 * MIN_MS - 1, true, OTA_ROLLBACK_MS), "up to 60 min more");
    CHECK(ota_rollback_due(90 * MIN_MS, true, OTA_ROLLBACK_MS), "then anyway");
    CHECK(ota_rollback_due(2 * MIN_MS, false, 2 * MIN_MS), "test build: 2 min");
}

static void test_report(void)
{
    char out[320];
    ota_last_t l = {.result = OTA_RES_ROLLED_BACK, .attempt = 3};
    strcpy(l.error, "no_checkin");
    strcpy(l.version, "0.9.15");
    ota_report_t r = {.rollback = true, .slot = 1, .image = "valid", .phase = "idle", .last = &l};
    CHECK(ota_report_json(&r, out, sizeof out) > 0, "fits");
    CHECK(strcmp(out, "{\"image\":\"valid\",\"last\":{\"attempt\":3,\"error\":\"no_checkin\",\"result\":\"rolled_back\","
                      "\"version\":\"0.9.15\"},\"phase\":\"idle\",\"rollback\":true,\"slot\":1}") == 0,
          "design example: %s", out);
    cJSON *j = cJSON_Parse(out);
    CHECK(j != NULL, "valid JSON");
    cJSON_Delete(j);
    ota_last_t ok = {.result = OTA_RES_OK, .attempt = 4};
    strcpy(ok.version, "0.9.16");
    r = (ota_report_t){.rollback = true, .slot = 0, .image = "pending_verify", .phase = "rebooting", .last = &ok};
    ota_report_json(&r, out, sizeof out);
    CHECK(strcmp(out, "{\"image\":\"pending_verify\",\"last\":{\"attempt\":4,\"result\":\"ok\",\"version\":\"0.9.16\"},"
                      "\"phase\":\"rebooting\",\"rollback\":true,\"slot\":0}") == 0,
          "ok: %s", out);
    ota_last_t none = {0};
    r = (ota_report_t){.rollback = false, .slot = -1, .image = "undefined", .phase = "idle", .last = &none};
    ota_report_json(&r, out, sizeof out);
    CHECK(strcmp(out, "{\"image\":\"undefined\",\"phase\":\"idle\",\"rollback\":false}") == 0, "no last, no slot: %s", out);
    ota_last_t badf = {.result = OTA_RES_FAILED, .attempt = 0};
    strcpy(badf.error, "Bad\"x");
    strcpy(badf.version, "x");
    r.last = &badf;
    ota_report_json(&r, out, sizeof out);
    CHECK(strstr(out, "\"last\":{\"result\":\"failed\"}") != NULL, "invalid fields dropped: %s", out);
    CHECK(ota_report_json(&r, out, 20) == 0, "too small");
    CHECK(strcmp(ota_image_name(true, 0), "new") == 0 && strcmp(ota_image_name(true, 1), "pending_verify") == 0 &&
              strcmp(ota_image_name(true, 2), "valid") == 0 && strcmp(ota_image_name(true, 4), "undefined") == 0 &&
              strcmp(ota_image_name(true, 0xFFFFFFFFu), "undefined") == 0 &&
              strcmp(ota_image_name(false, 2), "undefined") == 0,
          "image names");
}

static void test_checkin_body(void)
{
    char out[1536];
    ota_last_t l = {.result = OTA_RES_FAILED, .attempt = 2};
    strcpy(l.error, "refused");
    strcpy(l.version, "0.9.16");
    ota_report_t rep = {.rollback = true, .slot = 0, .image = "valid", .phase = "idle", .last = &l};
    dev_diag_t d;
    memset(&d, 0, sizeof d);
    d.boots = 1;
    d.has_crash = true;
    d.crash_reason = "panic";
    d.crash_pc = 0x4201a2b3;
    strcpy(d.crash_elf, "77aa01ff");
    dev_checkin_t c = {.fw = "0.9.16", .fw_build = "a1b2c3d4", .ota = &rep, .ip = "192.168.0.39", .rssi = -70,
                       .uptime_s = 5, .diag = &d};
    CHECK(dev_checkin_body(&c, NULL, out, sizeof out) > 0, "body");
    const char *fw = strstr(out, "\"fw\":"), *fb = strstr(out, "\"fw_build\":\"a1b2c3d4\""),
               *hf = strstr(out, "\"heap_internal_free\""), *ip = strstr(out, "\"ip\""), *ota = strstr(out, "\"ota\":{"),
               *rs = strstr(out, "\"rssi\"");
    CHECK(fw && fb && hf && ip && ota && rs && fw < fb && fb < hf && ip < ota && ota < rs, "sorted keys: %s", out);
    CHECK(strstr(out, "\"crash\":{\"elf\":\"77aa01ff\",\"pc\":\"0x4201a2b3\",\"reason\":\"panic\"}") != NULL,
          "crash elf: %s", out);
    CHECK(strstr(out, "\"last\":{\"attempt\":2,\"error\":\"refused\",\"result\":\"failed\",\"version\":\"0.9.16\"}"),
          "refused report");
    cJSON *j = cJSON_Parse(out);
    CHECK(j != NULL, "valid JSON");
    cJSON_Delete(j);
    c.fw_build = "A1B2C3D4";
    c.ota = NULL;
    strcpy(d.crash_elf, "xyz");
    dev_checkin_body(&c, NULL, out, sizeof out);
    CHECK(!strstr(out, "fw_build") && !strstr(out, "\"ota\"") && !strstr(out, "\"elf\""), "invalid build, no ota: %s", out);
}

static void test_capable(void)
{
    CHECK(ota_rollback_capable(true, 2) && ota_rollback_capable(true, 3), "app and bootloader can roll back");
    CHECK(!ota_rollback_capable(true, 1), "old bootloader");
    CHECK(!ota_rollback_capable(false, 2), "app built without rollback");
}

static void test_backoff_retry(void)
{
    ota_backoff_t b = {0};
    CHECK(ota_backoff_due(&b, 0), "first try at once");
    ota_backoff_failed(&b, 1000);
    CHECK(!ota_backoff_due(&b, 1000 + MIN_MS - 1) && ota_backoff_due(&b, 1000 + MIN_MS), "1 min");
    ota_backoff_failed(&b, 0);
    CHECK(!ota_backoff_due(&b, 5 * MIN_MS - 1) && ota_backoff_due(&b, 5 * MIN_MS), "5 min");
    ota_backoff_failed(&b, 0);
    ota_backoff_failed(&b, 0);
    CHECK(!ota_backoff_due(&b, 30 * MIN_MS - 1) && ota_backoff_due(&b, 30 * MIN_MS), "30 min cap");
}

static void test_reboot_gate(void)
{
    ota_reboot_gate_t g = {0};
    CHECK(ota_reboot_request(&g, OTA_REBOOT_RESTART, false) == OTA_REBOOT_RESTART, "valid image: reboot now");
    CHECK(ota_reboot_release(&g, false) == OTA_REBOOT_NONE, "nothing held");
    CHECK(ota_reboot_request(&g, OTA_REBOOT_RESTART, true) == OTA_REBOOT_NONE, "pending verify: held");
    CHECK(ota_reboot_request(&g, OTA_REBOOT_FALLBACK, true) == OTA_REBOOT_NONE, "still held");
    CHECK(ota_reboot_request(&g, OTA_REBOOT_RESTART, true) == OTA_REBOOT_NONE, "a restart never downgrades a fallback");
    CHECK(ota_reboot_release(&g, true) == OTA_REBOOT_NONE, "not before the image is valid");
    CHECK(ota_reboot_release(&g, false) == OTA_REBOOT_FALLBACK, "the fallback runs once valid");
    CHECK(ota_reboot_release(&g, false) == OTA_REBOOT_NONE, "once");
    CHECK(ota_reboot_request(&g, OTA_REBOOT_NONE, true) == OTA_REBOOT_NONE && g.held == OTA_REBOOT_NONE, "none");
}

static ota_health_in_t healthy(void)
{
    ota_health_in_t h = {
        .link = OTA_LINK_OK,
        .frames = 750,
        .loop_age_ms = 16,
        .touch_ok = 1,
        .heap_internal_min = 84 * 1024,
        .heap_largest_min = 31 * 1024,
        .n_tasks = 3,
        .tasks = {{"ember", 1800}, {"lvgl", 2484}, {"link", 900}},
    };
    return h;
}

static bool reason_ok(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n < 1 || n > 24) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) return false;
    return true;
}

static bool is(ota_health_t r, ota_health_state_t st, const char *why)
{
    if (r.state != st) return false;
    if (!why) return r.reason == NULL;
    return r.reason && strcmp(r.reason, why) == 0;
}

static void test_health(void)
{
    ota_health_in_t h = healthy();
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "healthy passes");

    h = healthy();
    h.link = OTA_LINK_PENDING;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_display"), "no link check yet: pending");
    h.link = OTA_LINK_FAIL;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_FAIL, "health_display"), "link fail at the fallback clock: hard fail");

    h = healthy();
    h.frames = OTA_HEALTH_MIN_FRAMES - 1;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_render"), "19 frames: render");
    h.frames = OTA_HEALTH_MIN_FRAMES;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "20 frames pass");
    h.loop_age_ms = OTA_HEALTH_LOOP_MS;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "loop tick 30 s ago passes");
    h.loop_age_ms = OTA_HEALTH_LOOP_MS + 1;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_render"), "loop stalled > 30 s: render");
    h.loop_age_ms = -1;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_render"), "loop never ran: render");

    h = healthy();
    h.touch_ok = 0;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_input"), "touch controller never answered: input");
    h.input_seen = true;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "a real turn or press passes input");
    h.input_seen = false;
    h.touch_ok = OTA_HEALTH_MIN_TOUCH_OK;
    h.loop_age_ms = 0;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "one good controller read passes");

    h = healthy();
    h.heap_internal_min = OTA_HEALTH_HEAP_MIN - 1;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_heap"), "heap below 40 KB");
    h.heap_internal_min = OTA_HEALTH_HEAP_MIN;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "heap at 40 KB passes");
    h.heap_largest_min = OTA_HEALTH_LARGEST_MIN - 1;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_heap"), "largest block below 15 KB");
    h.heap_largest_min = OTA_HEALTH_LARGEST_MIN;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "largest block at 15 KB passes");

    h = healthy();
    h.tasks[1].stack_free = OTA_HEALTH_STACK_LVGL - 1;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_stack"), "lvgl below 1024 B");
    h.tasks[1].stack_free = OTA_HEALTH_STACK_LVGL;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "lvgl at 1024 B passes");
    h.tasks[2].stack_free = OTA_HEALTH_STACK_LVGL - 1;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "other tasks need only 512 B");
    h.tasks[2].stack_free = OTA_HEALTH_STACK_MIN - 1;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_stack"), "other task below 512 B");
    h.tasks[2].stack_free = OTA_HEALTH_STACK_MIN;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "other task at 512 B passes");
    h.n_tasks = 0;
    h.tasks[0].stack_free = 0;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PASS, NULL), "only listed tasks count");

    h = healthy();
    h.link = OTA_LINK_PENDING;
    h.frames = 0;
    h.input_seen = false;
    h.touch_ok = 0;
    h.heap_internal_min = 0;
    h.tasks[0].stack_free = 0;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_display"), "order: display first");
    h.link = OTA_LINK_OK;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_render"), "order: then render");
    h.frames = 750;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_input"), "order: then input");
    h.touch_ok = 1;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_heap"), "order: then heap");
    h.heap_internal_min = 84 * 1024;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_PENDING, "health_stack"), "order: then stack");
    h.link = OTA_LINK_FAIL;
    CHECK(is(ota_health_check(&h), OTA_HEALTH_FAIL, "health_display"), "a link fail outranks soft checks");

    const char *reasons[] = {"health_display", "health_render", "health_input", "health_heap", "health_stack",
                             "no_checkin"};
    for (size_t i = 0; i < sizeof reasons / sizeof *reasons; i++)
        CHECK(reason_ok(reasons[i]), "reason charset %s", reasons[i]);
    CHECK(!reason_ok("Health") && !reason_ok("") && !reason_ok("health-display") && !reason_ok("a234567890123456789012345"),
          "charset check rejects");
    ota_rec_t r = {.att_state = OTA_ATT_BOOT, .att_attempt = 4};
    strcpy(r.att_ver, "0.9.30");
    ota_rec_rollback(&r, "health_render");
    char out[320];
    ota_report_t rep = {.rollback = true, .slot = 1, .image = "valid", .last = &r.last};
    ota_report_json(&rep, out, sizeof out);
    CHECK(strstr(out, "\"error\":\"health_render\"") != NULL, "the reason reaches the ota JSON: %s", out);
}

static void test_verify_rollback(void)
{
    ota_health_t pass = {OTA_HEALTH_PASS, NULL};
    ota_health_t render = {OTA_HEALTH_PENDING, "health_render"};
    ota_health_t stack = {OTA_HEALTH_PENDING, "health_stack"};
    ota_health_t link = {OTA_HEALTH_FAIL, "health_display"};
    const char *s;

    CHECK(ota_verify_rollback(false, true, &pass) == NULL, "before the deadline: wait");
    CHECK(ota_verify_rollback(false, true, &render) == NULL, "soft fail waits for the deadline");
    CHECK(ota_verify_rollback(false, false, &render) == NULL, "soft fail without checkin waits too");
    s = ota_verify_rollback(false, false, &link);
    CHECK(s && strcmp(s, "health_display") == 0, "hard fail rolls back at once, even before a checkin");
    s = ota_verify_rollback(true, true, &render);
    CHECK(s && strcmp(s, "health_render") == 0, "deadline, checkins fine: the health reason");
    s = ota_verify_rollback(true, true, &stack);
    CHECK(s && strcmp(s, "health_stack") == 0, "deadline: the first failing reason as given");
    s = ota_verify_rollback(true, false, &render);
    CHECK(s && strcmp(s, "no_checkin") == 0, "deadline without any checkin: no_checkin");
    s = ota_verify_rollback(true, true, &pass);
    CHECK(s && strcmp(s, "no_checkin") == 0, "deadline, healthy but never valid: no_checkin");
    s = ota_verify_rollback(true, false, &pass);
    CHECK(s && strcmp(s, "no_checkin") == 0, "deadline, healthy, no checkin: no_checkin");

    CHECK(!ota_rollback_due(29 * MIN_MS, false, OTA_ROLLBACK_MS) &&
              ota_verify_rollback(ota_rollback_due(29 * MIN_MS, false, OTA_ROLLBACK_MS), true, &render) == NULL,
          "29 min: wait");
    s = ota_verify_rollback(ota_rollback_due(30 * MIN_MS, true, OTA_ROLLBACK_MS), true, &render);
    CHECK(s == NULL, "pomodoro defers the health rollback");
    s = ota_verify_rollback(ota_rollback_due(90 * MIN_MS, true, OTA_ROLLBACK_MS), true, &render);
    CHECK(s && strcmp(s, "health_render") == 0, "after the pomodoro extension: health reason");
    s = ota_verify_rollback(ota_rollback_due(2 * MIN_MS, false, 2 * MIN_MS), true, &render);
    CHECK(s && strcmp(s, "health_render") == 0, "test build: health_render at 2 min");
}

static void test_link_mapping(void)
{
    CHECK(ota_link_result(false, true) == OTA_LINK_OK, "80 MHz pass: ok");
    CHECK(ota_link_result(true, true) == OTA_LINK_OK, "80 MHz confirmed fail: ok (the held fallback fixes it)");
    CHECK(ota_link_result(false, false) == OTA_LINK_OK, "40 MHz pass: ok");
    CHECK(ota_link_result(true, false) == OTA_LINK_FAIL, "40 MHz confirmed fail: hard fail");
    CHECK(!ota_verify_needs_health(false, OTA_LINK_PENDING) && !ota_verify_needs_health(false, OTA_LINK_OK),
          "no health read before the deadline without a hard fail");
    CHECK(ota_verify_needs_health(false, OTA_LINK_FAIL), "a hard link fail is checked at once");
    CHECK(ota_verify_needs_health(true, OTA_LINK_OK) && ota_verify_needs_health(true, OTA_LINK_PENDING), "deadline");
    ota_health_in_t h = healthy();
    h.link = ota_link_result(true, false);
    ota_health_t r = ota_health_check(&h);
    const char *s = ota_verify_rollback(ota_rollback_due(5 * 1000, true, OTA_ROLLBACK_MS), false, &r);
    CHECK(s && strcmp(s, "health_display") == 0, "a hard fail ignores the pomodoro deferral and the 60 s rule");
}

static void test_override(void)
{
    ota_valid_in_t v = {.uptime_ms = 60000, .checkin_ok = true, .frame = true, .view_ok = true,
                        .health = OTA_HEALTH_PENDING};
    CHECK(ota_override_check(true, &v) == OTA_OVERRIDE_OK, "pending + checkin: accepted, health skipped");
    v.health = OTA_HEALTH_FAIL;
    CHECK(ota_override_check(true, &v) == OTA_OVERRIDE_OK, "health ignored");
    CHECK(ota_override_check(false, &v) == OTA_OVERRIDE_NOT_PENDING, "not pending: refused");
    v.checkin_ok = false;
    CHECK(ota_override_check(true, &v) == OTA_OVERRIDE_NO_CHECKIN, "no checkin yet: refused");
    CHECK(ota_override_check(false, &v) == OTA_OVERRIDE_NOT_PENDING, "not pending outranks no checkin");
    v.checkin_ok = true;
    v.uptime_ms = 59999;
    CHECK(ota_override_check(true, &v) == OTA_OVERRIDE_NOT_READY, "the 60 s rule still applies");
    v.uptime_ms = 60000;
    v.frame = false;
    CHECK(ota_override_check(true, &v) == OTA_OVERRIDE_NOT_READY, "the frame rule still applies");
    v.frame = true;
    v.view_ok = false;
    CHECK(ota_override_check(true, &v) == OTA_OVERRIDE_NOT_READY, "the view rule still applies");
}

int main(int argc, char **argv)
{
    if (argc > 1) vectors_dir = argv[1];
    test_semver();
    test_offer();
    test_reply();
    test_records();
    test_boot();
    test_gate();
    test_resume();
    test_header();
    test_progress_and_timers();
    test_report();
    test_checkin_body();
    test_capable();
    test_backoff_retry();
    test_reboot_gate();
    test_health();
    test_verify_rollback();
    test_override();
    test_link_mapping();
    if (failures) {
        printf("ota: %d failure(s)\n", failures);
        return 1;
    }
    printf("ota: all passed\n");
    return 0;
}
