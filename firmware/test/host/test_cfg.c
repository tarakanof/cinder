#include <stdio.h>
#include <string.h>

#include "cfg.h"
#include "reset_gesture.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_fields(void)
{
    cfg_t c;
    for (size_t i = 0; i < CFG_FIELD_COUNT; i++) {
        const cfg_field_t *f = &CFG_FIELDS[i];
        CHECK(strlen(f->key) <= 15, "NVS key too long: %s", f->key);
        CHECK(f->offset + f->size <= sizeof c, "field %s outside cfg_t", f->key);
        for (size_t j = 0; j < i; j++) CHECK(strcmp(CFG_FIELDS[j].key, f->key) != 0, "duplicate key %s", f->key);
    }
    CHECK(strlen(CFG_KEY_SETTINGS) <= 15 && strlen(CFG_KEY_SETTINGS_VER) <= 15, "settings keys");
    CHECK(strlen(CFG_NAMESPACE) <= 15, "namespace");
    int secrets = 0;
    for (size_t i = 0; i < CFG_FIELD_COUNT; i++) secrets += CFG_FIELDS[i].secret;
    CHECK(secrets == 2, "password and token are secret: %d", secrets);
}

static void test_url(void)
{
    CHECK(cfg_url_valid("http://192.168.0.2:3627"), "ip:port");
    CHECK(!cfg_url_valid("https://ember.example.com"), "https rejected (no TLS on the knob)");
    CHECK(cfg_url_valid("HTTP://Ember.LAN:3627"), "uppercase scheme/host accepted (normalised by cfg_set)");
    CHECK(!cfg_url_valid("http://[::1]:3627"), "IPv6 literal");
    CHECK(!cfg_url_valid("http://my_host"), "underscore");
    CHECK(!cfg_url_valid("http://caf\xc3\xa9.lan"), "IDN");
    CHECK(cfg_url_valid("http://ember:3627/"), "trailing slash");
    CHECK(!cfg_url_valid("http://ember:3627/v1"), "path");
    CHECK(!cfg_url_valid("ftp://ember"), "scheme");
    CHECK(!cfg_url_valid("http://"), "no host");
    CHECK(!cfg_url_valid("http://:80"), "empty host");
    CHECK(!cfg_url_valid("http://host:0"), "port 0");
    CHECK(!cfg_url_valid("http://host:65536"), "port too big");
    CHECK(!cfg_url_valid("http://host:"), "empty port");
    CHECK(!cfg_url_valid("http://ho st"), "space");
    CHECK(!cfg_url_valid("http://user@host"), "user info");
    CHECK(!cfg_url_valid("http://host?x=1"), "query");
    CHECK(!cfg_url_valid(""), "empty");
    CHECK(!cfg_url_valid(NULL), "null");
    char big[CFG_URL_MAX + 8] = "http://";
    memset(big + 7, 'a', CFG_URL_MAX);
    big[CFG_URL_MAX + 1] = 0;
    CHECK(!cfg_url_valid(big), "too long");
}

static void test_set(void)
{
    cfg_t c = {0};
    CHECK(cfg_set(&c, "ember_url", "http://192.168.0.2:3627/") == CFG_OK, "set url");
    CHECK(strcmp(c.ember_url, "http://192.168.0.2:3627") == 0, "slash dropped: %s", c.ember_url);
    CHECK(cfg_set(&c, "ember_url", "HTTP://Ember.LAN:3627/") == CFG_OK && strcmp(c.ember_url, "http://ember.lan:3627") == 0,
          "normalised: %s", c.ember_url);
    CHECK(cfg_set(&c, "ember_url", "http://192.168.0.2:3627/") == CFG_OK, "set url again");
    CHECK(cfg_set(&c, "ember_url", "http://x/y") == CFG_ERR_BAD_VALUE, "bad url");
    CHECK(strcmp(c.ember_url, "http://192.168.0.2:3627") == 0, "unchanged on error");
    CHECK(cfg_set(&c, "nope", "x") == CFG_ERR_UNKNOWN_KEY, "unknown key");
    CHECK(cfg_set(&c, NULL, "x") == CFG_ERR_UNKNOWN_KEY, "null key");

    CHECK(cfg_set(&c, "wifi_pass", "short") == CFG_ERR_BAD_VALUE, "wpa2 min 8");
    CHECK(cfg_set(&c, "wifi_pass", "") == CFG_OK, "open network");
    CHECK(cfg_set(&c, "wifi_pass", "12345678") == CFG_OK, "8 chars");
    char hex[65];
    memset(hex, 'a', 64);
    hex[64] = 0;
    CHECK(cfg_set(&c, "wifi_pass", hex) == CFG_OK, "64 hex");
    hex[0] = 'z';
    CHECK(cfg_set(&c, "wifi_pass", hex) == CFG_ERR_BAD_VALUE, "64 non-hex");
    CHECK(cfg_set(&c, "wifi_pass", "pass\nword") == CFG_ERR_BAD_VALUE, "control char");

    char ssid[40];
    memset(ssid, 's', 33);
    ssid[33] = 0;
    CHECK(cfg_set(&c, "wifi_ssid", ssid) == CFG_ERR_TOO_LONG, "ssid 33");
    ssid[32] = 0;
    CHECK(cfg_set(&c, "wifi_ssid", ssid) == CFG_OK && strlen(c.wifi_ssid) == 32, "ssid 32");

    CHECK(cfg_set(&c, "dev_tok", "ekd_abc-_123") == CFG_OK, "token");
    CHECK(cfg_set(&c, "dev_tok", "a b") == CFG_ERR_BAD_VALUE, "token space");
    CHECK(cfg_set(&c, "dev_tok", "a\r\nX-Evil: 1") == CFG_ERR_BAD_VALUE, "token header injection");
    CHECK(cfg_set(&c, "dev_id", "knob-a1b2c3") == CFG_OK, "dev id");
    CHECK(cfg_set(&c, "dev_id", "knob/1") == CFG_ERR_BAD_VALUE, "dev id slash");
    CHECK(cfg_set(&c, "name", "Desk knob") == CFG_OK, "name");
    CHECK(cfg_set(&c, "name", "a\tb") == CFG_ERR_BAD_VALUE, "name tab");
    CHECK(cfg_set(&c, "name", NULL) == CFG_OK && c.name[0] == 0, "null clears");
}

static void test_seed(void)
{
    cfg_t c;
    char bad[64];
    CHECK(!cfg_seed(&c, "", "password1", "http://h:1", bad, sizeof bad), "no ssid, no seed");
    CHECK(!cfg_provisioned(&c) && bad[0] == 0, "unprovisioned, nothing rejected");
    CHECK(cfg_seed(&c, "home", "password1", "http://192.168.0.2:3627", bad, sizeof bad), "seed");
    CHECK(cfg_provisioned(&c) && bad[0] == 0, "provisioned");
    CHECK(strcmp(c.wifi_ssid, "home") == 0 && strcmp(c.wifi_pass, "password1") == 0 &&
          strcmp(c.ember_url, "http://192.168.0.2:3627") == 0,
          "seed fields");
    CHECK(c.dev_id[0] == 0 && c.dev_tok[0] == 0 && c.name[0] == 0, "the seed never holds a token");
    CHECK(!cfg_paired(&c), "a seeded knob is not paired");
    CHECK(cfg_seed(&c, "home", "", "", bad, sizeof bad), "seed wifi only");

    CHECK(!cfg_seed(&c, "home", "pw", "http://h:1", bad, sizeof bad), "bad password rejects the seed");
    CHECK(strcmp(bad, "wifi_pass") == 0 && !strstr(bad, "pw,"), "names wifi_pass: %s", bad);
    CHECK(c.wifi_ssid[0] == 0, "zeroed on failure");
    char ssid[40];
    memset(ssid, 's', 33);
    ssid[33] = 0;
    CHECK(!cfg_seed(&c, ssid, "password1", "", bad, sizeof bad) && strcmp(bad, "wifi_ssid") == 0,
          "ssid too long: %s", bad);

    CHECK(cfg_seed(&c, "home", "password1", "ember.local", bad, sizeof bad), "partial seed");
    CHECK(strcmp(bad, "ember_url") == 0, "names the url: %s", bad);
    CHECK(strcmp(c.wifi_ssid, "home") == 0 && c.ember_url[0] == 0, "bad url dropped");
}

static void test_paired(void)
{
    cfg_t c = {0};
    CHECK(!cfg_paired(&c), "empty: unpaired");
    cfg_set(&c, "dev_tok", "legacy-master-token");
    CHECK(!cfg_paired(&c), "a token without a device id is not a pairing");
    cfg_set(&c, "dev_id", "knob-a1b2c3");
    cfg_set(&c, "dev_tok", "");
    CHECK(!cfg_paired(&c), "a device id without a token is not a pairing");
    cfg_set(&c, "dev_tok", "ekd_device_token");
    CHECK(cfg_paired(&c), "device id + token: paired");
    CHECK(!cfg_paired(NULL), "null");

    CHECK(cfg_drop_token(true, CFG_READ_NOT_FOUND), "token and no dev_id key: the legacy master token goes");
    CHECK(!cfg_drop_token(true, CFG_READ_OK), "dev_id read: keep");
    CHECK(!cfg_drop_token(true, CFG_READ_ERROR), "dev_id read error: keep the token");
    CHECK(!cfg_drop_token(false, CFG_READ_NOT_FOUND), "no token: nothing to drop");
}

static void test_should_seed(void)
{
    cfg_t empty = {0}, partial = {0}, full = {0};
    cfg_set(&partial, "wifi_pass", "password1");
    cfg_set(&full, "wifi_ssid", "home");
    CHECK(!cfg_should_seed(false, CFG_NVS_EMPTY, &empty, "home"), "DEV_SEED=n");
    CHECK(!cfg_should_seed(true, CFG_NVS_EMPTY, &empty, ""), "no seed ssid");
    CHECK(!cfg_should_seed(true, CFG_NVS_EMPTY, &empty, NULL), "null seed ssid");
    CHECK(cfg_should_seed(true, CFG_NVS_EMPTY, &empty, "home"), "empty namespace");
    CHECK(cfg_should_seed(true, CFG_NVS_HAS_KEYS, &partial, "home"), "partial -> reseed");
    CHECK(!cfg_should_seed(true, CFG_NVS_HAS_KEYS, &full, "other"), "provisioned kept");
    CHECK(!cfg_should_seed(true, CFG_NVS_ERROR, &empty, "home"), "error -> no seed");
}

static void test_ssid_last(void)
{
    CHECK(strcmp(CFG_FIELDS[CFG_FIELD_COUNT - 1].key, "wifi_ssid") == 0, "wifi_ssid is the last field");
}

static void test_raw_copy(void)
{
    uint8_t ssid[32], pass[64];
    char s32[33], p64[65];
    memset(s32, 'S', 32);
    s32[32] = 0;
    memset(p64, 'a', 64);
    p64[64] = 0;
    memset(ssid, 0, sizeof ssid);
    memset(pass, 0, sizeof pass);
    cfg_copy_raw(ssid, sizeof ssid, s32);
    cfg_copy_raw(pass, sizeof pass, p64);
    CHECK(memcmp(ssid, s32, 32) == 0, "32-byte ssid intact");
    CHECK(memcmp(pass, p64, 64) == 0, "64-hex psk intact");
    uint8_t buf[8] = {0};
    cfg_copy_raw(buf, sizeof buf, "abc");
    CHECK(memcmp(buf, "abc\0\0\0\0\0", 8) == 0, "short value NUL-padded");
    cfg_copy_raw(buf, 3, "abcdef");
    CHECK(memcmp(buf, "abc", 3) == 0, "capped at cap");
}

static void test_ids(void)
{
    const uint8_t mac[6] = {0x02, 0x00, 0x00, 0xab, 0xcd, 0xef};
    char id[13], s[7];
    cfg_hw_id(mac, id);
    cfg_short_id(mac, s);
    CHECK(strcmp(id, "020000abcdef") == 0, "hw id %s", id);
    CHECK(strcmp(s, "ABCDEF") == 0, "short id %s", s);
}

static void test_describe(void)
{
    cfg_t c;
    char bad[64];
    cfg_seed(&c, "home", "supersecret", "http://h:1", bad, sizeof bad);
    cfg_set(&c, "dev_tok", "tok-secret-xyz");
    char line[256];
    cfg_describe(&c, line, sizeof line);
    CHECK(strstr(line, "home") && strstr(line, "http://h:1"), "describe: %s", line);
    CHECK(!strstr(line, "supersecret") && !strstr(line, "tok-secret"), "no secrets: %s", line);
}

static void hold(reset_gesture_t *g, double from, double to)
{
    for (double t = from; t <= to + 1e-9; t += 0.016) rg_tick(g, t);
}

static void test_gesture_confirm(void)
{
    reset_gesture_t g;
    rg_init(&g);
    rg_press(&g, 0);
    hold(&g, 0, 9.9);
    CHECK(g.state == RG_IDLE, "not armed before 10 s");
    CHECK(rg_tick(&g, 10.0) == RG_ARMED, "armed at 10 s");
    for (int i = 0; i < RG_DETENTS - 1; i++) CHECK(rg_turn(&g, +1, 10.1 + i * 0.1), "turn consumed");
    CHECK(g.state == RG_ARMED && g.progress == RG_DETENTS - 1, "23 detents: still armed");
    rg_turn(&g, +1, 12.5);
    CHECK(g.state == RG_CONFIRMED, "24 detents confirm");
    CHECK(rg_release(&g, 13), "release consumed");
    CHECK(g.state == RG_CONFIRMED, "release after confirm keeps it");
}

static void test_gesture_cancel(void)
{
    reset_gesture_t g;
    rg_init(&g);
    rg_press(&g, 0);
    hold(&g, 0, 10.5);
    rg_turn(&g, +1, 10.6);
    CHECK(rg_release(&g, 11), "release consumed");
    CHECK(g.state == RG_IDLE && g.progress == 0, "release cancels");

    rg_press(&g, 20);
    hold(&g, 20, 30.2);
    CHECK(g.state == RG_ARMED, "armed");
    rg_turn(&g, +1, 30.3);
    CHECK(rg_turn(&g, -1, 30.4) && g.state == RG_IDLE, "left cancels");
    CHECK(rg_turn(&g, +1, 30.5) && g.state == RG_IDLE, "swallowed after cancel");
    hold(&g, 30.5, 45);
    CHECK(g.state == RG_IDLE, "no re-arm in the same press");
    CHECK(rg_release(&g, 45), "release consumed");

    rg_press(&g, 100);
    hold(&g, 100, 110.2);
    CHECK(g.state == RG_ARMED, "armed for timeout test");
    rg_turn(&g, +1, 113);
    hold(&g, 113, 117.9);
    CHECK(g.state == RG_ARMED, "detent at 113 restarts the timeout");
    hold(&g, 117.9, 118.1);
    CHECK(g.state == RG_IDLE, "timeout cancels");
    rg_release(&g, 119);
}

static void test_gesture_normal_input(void)
{
    reset_gesture_t g;
    rg_init(&g);
    rg_press(&g, 0);
    hold(&g, 0, 0.2);
    CHECK(!rg_release(&g, 0.2), "push not consumed");
    rg_press(&g, 1);
    hold(&g, 1, 3);
    CHECK(!rg_release(&g, 3), "long push not consumed");
    CHECK(!rg_turn(&g, +1, 4), "plain turn");
    rg_press(&g, 5);
    CHECK(!rg_turn(&g, +1, 6), "page change turn");
    hold(&g, 6, 20);
    CHECK(g.state == RG_IDLE, "turned press never arms");
    CHECK(!rg_turn(&g, -1, 20), "still a page change");
    CHECK(!rg_release(&g, 20), "page change release not consumed");
}

int main(void)
{
    test_fields();
    test_url();
    test_set();
    test_seed();
    test_paired();
    test_should_seed();
    test_ssid_last();
    test_raw_copy();
    test_ids();
    test_describe();
    test_gesture_confirm();
    test_gesture_cancel();
    test_gesture_normal_input();
    if (failures) {
        printf("cfg: %d failure(s)\n", failures);
        return 1;
    }
    printf("cfg: all tests passed\n");
    return 0;
}
