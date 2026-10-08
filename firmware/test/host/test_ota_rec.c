#include <stdio.h>
#include <string.h>

#include "ota_policy.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

_Static_assert(sizeof(ota_rec_t) == 252, "ota_rec_t changed: persist the new field in ota_rec_load/ota_rec_save and test it here");
_Static_assert(sizeof(ota_last_t) == 68, "ota_last_t changed: persist the new field in ota_rec_load/ota_rec_save and test it here");

enum { T_U8 = 1, T_U32, T_STR };
#define KEYS_MAX 32
#define STR_MAX 128
#define LOG_MAX 64

typedef struct {
    char key[16];
    int type;
    uint32_t num;
    char str[STR_MAX];
} entry_t;

typedef struct {
    entry_t e[KEYS_MAX];
    int n;
    const char *fail_key;
    char log[LOG_MAX][24];
    int n_log;
} store_t;

static entry_t *find(store_t *s, const char *key)
{
    for (int i = 0; i < s->n; i++)
        if (strcmp(s->e[i].key, key) == 0) return &s->e[i];
    return NULL;
}

static entry_t *put(store_t *s, const char *key, int type)
{
    entry_t *e = find(s, key);
    if (!e) {
        e = &s->e[s->n++];
        memset(e, 0, sizeof *e);
        snprintf(e->key, sizeof e->key, "%s", key);
    }
    e->type = type;
    return e;
}

static void note(store_t *s, const char *op, const char *key)
{
    if (s->n_log < LOG_MAX) snprintf(s->log[s->n_log++], sizeof s->log[0], "%s %s", op, key);
}

static bool f_get_u8(void *ctx, const char *key, uint8_t *v)
{
    entry_t *e = find(ctx, key);
    if (!e || e->type != T_U8) return false;
    *v = (uint8_t)e->num;
    return true;
}

static bool f_get_u32(void *ctx, const char *key, uint32_t *v)
{
    entry_t *e = find(ctx, key);
    if (!e || e->type != T_U32) return false;
    *v = e->num;
    return true;
}

static bool f_get_str(void *ctx, const char *key, char *out, size_t cap)
{
    entry_t *e = find(ctx, key);
    if (!e || e->type != T_STR || strlen(e->str) + 1 > cap) return false;
    memcpy(out, e->str, strlen(e->str) + 1);
    return true;
}

static int f_set(store_t *s, const char *op, const char *key)
{
    note(s, op, key);
    return s->fail_key && strcmp(s->fail_key, key) == 0 ? 0x1105 : 0;
}

static int f_set_u8(void *ctx, const char *key, uint8_t v)
{
    int err = f_set(ctx, "u8", key);
    if (!err) put(ctx, key, T_U8)->num = v;
    return err;
}

static int f_set_u32(void *ctx, const char *key, uint32_t v)
{
    int err = f_set(ctx, "u32", key);
    if (!err) put(ctx, key, T_U32)->num = v;
    return err;
}

static int f_set_str(void *ctx, const char *key, const char *v)
{
    int err = f_set(ctx, "str", key);
    if (!err) snprintf(put(ctx, key, T_STR)->str, STR_MAX, "%s", v);
    return err;
}

static int f_erase(void *ctx, const char *key)
{
    store_t *s = ctx;
    int err = f_set(s, "erase", key);
    entry_t *e = find(s, key);
    if (!err && e) *e = s->e[--s->n];
    return err;
}

static ota_kv_t kv_of(store_t *s)
{
    return (ota_kv_t){.ctx = s,
                      .get_u8 = f_get_u8,
                      .get_u32 = f_get_u32,
                      .get_str = f_get_str,
                      .set_u8 = f_set_u8,
                      .set_u32 = f_set_u32,
                      .set_str = f_set_str,
                      .erase = f_erase};
}

static void fill(char *s, size_t cap, char c)
{
    memset(s, c, cap - 1);
    s[cap - 1] = 0;
}

static ota_rec_t full_rec(void)
{
    ota_rec_t r;
    memset(&r, 0, sizeof r);
    r.att_state = OTA_ATT_BOOT;
    r.att_attempt = 0xfedcba98u;
    fill(r.att_sha, sizeof r.att_sha, 'a');
    fill(r.att_ver, sizeof r.att_ver, 'v');
    fill(r.att_build, sizeof r.att_build, 'b');
    r.last.result = OTA_RES_ROLLED_BACK;
    r.last.attempt = 0x89abcdefu;
    fill(r.last.error, sizeof r.last.error, 'e');
    fill(r.last.version, sizeof r.last.version, 'w');
    fill(r.bad, sizeof r.bad, 'f');
    return r;
}

static bool round_trip(const ota_rec_t *in, ota_rec_t *out, store_t *s)
{
    ota_kv_t kv = kv_of(s);
    if (ota_rec_save(in, &kv) != 0) return false;
    memset(out, 0x5a, sizeof *out);
    ota_rec_load(out, &kv);
    return true;
}

static void test_full_round_trip(void)
{
    store_t s = {0};
    ota_rec_t in = full_rec(), out;
    CHECK(round_trip(&in, &out, &s), "save ok");
    CHECK(memcmp(&in, &out, sizeof in) == 0, "every field survives a save and load");
    CHECK(out.att_state == in.att_state && out.att_attempt == in.att_attempt && strcmp(out.att_sha, in.att_sha) == 0 &&
              strcmp(out.att_ver, in.att_ver) == 0 && strcmp(out.att_build, in.att_build) == 0 &&
              out.last.result == in.last.result && out.last.attempt == in.last.attempt &&
              strcmp(out.last.error, in.last.error) == 0 && strcmp(out.last.version, in.last.version) == 0 &&
              strcmp(out.bad, in.bad) == 0,
          "field by field");
    CHECK(s.n == 10, "one key per field, got %d", s.n);
}

static void test_keys_and_types(void)
{
    static const struct {
        const char *key;
        int type;
    } want[] = {{"att_state", T_U8}, {"att_attempt", T_U32}, {"att_sha", T_STR}, {"att_ver", T_STR},
                {"att_build", T_STR}, {"last_res", T_U8}, {"last_att", T_U32}, {"last_err", T_STR},
                {"last_ver", T_STR}, {"bad", T_STR}};
    store_t s = {0};
    ota_kv_t kv = kv_of(&s);
    ota_rec_t r = full_rec();
    CHECK(ota_rec_save(&r, &kv) == 0, "save ok");
    CHECK(s.n_log == 10, "ten writes, got %d", s.n_log);
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
        entry_t *e = find(&s, want[i].key);
        CHECK(e && e->type == want[i].type, "key %s type %d", want[i].key, want[i].type);
        static const char *const op[] = {"", "u8", "u32", "str"};
        char w[24];
        snprintf(w, sizeof w, "%s %s", op[want[i].type], want[i].key);
        CHECK((int)i < s.n_log && strcmp(s.log[i], w) == 0, "write %zu is %s", i, w);
    }
    CHECK(find(&s, "att_state")->num == 2 && find(&s, "last_res")->num == 3, "enum values as stored by older images");
}

static void test_each_state(void)
{
    for (int st = OTA_ATT_NONE; st <= OTA_ATT_BOOT; st++) {
        store_t s = {0};
        ota_rec_t in = full_rec(), out;
        in.att_state = (ota_att_state_t)st;
        round_trip(&in, &out, &s);
        CHECK(out.att_state == in.att_state, "att_state %d", st);
    }
    for (int res = OTA_RES_NONE; res <= OTA_RES_ROLLED_BACK; res++) {
        store_t s = {0};
        ota_rec_t in = full_rec(), out;
        in.last.result = (ota_result_t)res;
        round_trip(&in, &out, &s);
        CHECK(out.last.result == in.last.result, "last_res %d", res);
    }
}

static void test_empty_strings_erase(void)
{
    store_t s = {0};
    ota_kv_t kv = kv_of(&s);
    ota_rec_t r = full_rec(), out;
    CHECK(ota_rec_save(&r, &kv) == 0, "full save");
    memset(&r, 0, sizeof r);
    s.n_log = 0;
    CHECK(ota_rec_save(&r, &kv) == 0, "empty save");
    static const char *const strs[] = {"att_sha", "att_ver", "att_build", "last_err", "last_ver", "bad"};
    for (size_t i = 0; i < sizeof strs / sizeof *strs; i++) CHECK(!find(&s, strs[i]), "%s erased", strs[i]);
    CHECK(s.n == 4, "only the numbers stay, got %d", s.n);
    CHECK(strcmp(s.log[2], "erase att_sha") == 0 && strcmp(s.log[9], "erase bad") == 0, "empty string → erase");
    memset(&out, 0x5a, sizeof out);
    ota_rec_load(&out, &kv);
    CHECK(memcmp(&out, &r, sizeof r) == 0, "an erased key loads as an empty string");
    s.n_log = 0;
    CHECK(ota_rec_save(&r, &kv) == 0, "erasing absent keys is not an error");
}

static void test_missing_and_bad_keys(void)
{
    store_t s = {0};
    ota_kv_t kv = kv_of(&s);
    ota_rec_t out, zero;
    memset(&zero, 0, sizeof zero);
    memset(&out, 0x5a, sizeof out);
    ota_rec_load(&out, &kv);
    CHECK(memcmp(&out, &zero, sizeof zero) == 0, "empty namespace → zero record");

    put(&s, "att_state", T_U8)->num = OTA_ATT_READY;
    put(&s, "last_res", T_U8)->num = OTA_RES_ROLLED_BACK + 1;
    put(&s, "att_attempt", T_STR);
    put(&s, "last_att", T_U8)->num = 7;
    put(&s, "att_sha", T_U32)->num = 1;
    fill(put(&s, "bad", T_STR)->str, OTA_SHA_HEX + 2, 'x');
    snprintf(put(&s, "last_ver", T_STR)->str, STR_MAX, "0.9.16");
    memset(&out, 0x5a, sizeof out);
    ota_rec_load(&out, &kv);
    CHECK(out.att_state == OTA_ATT_NONE, "att_state ready is not loaded (unchanged behaviour)");
    CHECK(out.last.result == OTA_RES_NONE, "out-of-range last_res → none");
    CHECK(out.att_attempt == 0 && out.last.attempt == 0, "wrong-type numbers → 0");
    CHECK(out.att_sha[0] == 0, "wrong-type string → empty");
    CHECK(out.bad[0] == 0, "too long string → empty");
    CHECK(strcmp(out.last.version, "0.9.16") == 0, "other keys still load");
}

static void test_old_record(void)
{
    store_t s = {0};
    ota_kv_t kv = kv_of(&s);
    put(&s, "att_state", T_U8)->num = 2;
    put(&s, "att_attempt", T_U32)->num = 41;
    snprintf(put(&s, "att_sha", T_STR)->str, STR_MAX, "9f2c000000000000000000000000000000000000000000000000000000000001");
    snprintf(put(&s, "att_ver", T_STR)->str, STR_MAX, "0.9.16");
    snprintf(put(&s, "att_build", T_STR)->str, STR_MAX, "a1b2c3d4");
    put(&s, "last_res", T_U8)->num = 2;
    put(&s, "last_att", T_U32)->num = 40;
    snprintf(put(&s, "last_err", T_STR)->str, STR_MAX, "refused");
    snprintf(put(&s, "last_ver", T_STR)->str, STR_MAX, "0.9.15");
    ota_rec_t r;
    ota_rec_load(&r, &kv);
    CHECK(r.att_state == OTA_ATT_BOOT && r.att_attempt == 41 &&
              strcmp(r.att_sha, "9f2c000000000000000000000000000000000000000000000000000000000001") == 0 &&
              strcmp(r.att_ver, "0.9.16") == 0 && strcmp(r.att_build, "a1b2c3d4") == 0,
          "attempt fields as an older image wrote them");
    CHECK(r.last.result == OTA_RES_FAILED && r.last.attempt == 40 && strcmp(r.last.error, "refused") == 0 &&
              strcmp(r.last.version, "0.9.15") == 0 && r.bad[0] == 0,
          "last and bad as an older image wrote them");
}

static void test_save_stops_at_error(void)
{
    store_t s = {0};
    s.fail_key = "att_ver";
    ota_kv_t kv = kv_of(&s);
    ota_rec_t r = full_rec();
    CHECK(ota_rec_save(&r, &kv) == 0x1105, "the store's error comes back");
    CHECK(s.n_log == 4 && strcmp(s.log[3], "str att_ver") == 0, "no write after the failing one");
    s.n_log = 0;
    s.fail_key = "bad";
    r.bad[0] = 0;
    CHECK(ota_rec_save(&r, &kv) == 0x1105, "an erase error comes back too");
}

int main(void)
{
    test_full_round_trip();
    test_keys_and_types();
    test_each_state();
    test_empty_strings_erase();
    test_missing_and_bad_keys();
    test_old_record();
    test_save_stops_at_error();
    if (failures) {
        printf("ota_rec: %d failure(s)\n", failures);
        return 1;
    }
    printf("ota_rec: all passed\n");
    return 0;
}
