#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coredump_up.h"
#include "device_api.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define MIN_MS (60 * 1000)

static void test_crc(void)
{
    CHECK(cd_crc32(0, "123456789", 9) == 0xcbf43926u, "zlib check value");
    CHECK(cd_crc32(0, "", 0) == 0, "empty");
    static uint8_t img[65536];
    uint32_t x = 12345;
    for (size_t i = 0; i < sizeof img; i++) img[i] = (uint8_t)((x = x * 1103515245u + 12345u) >> 16);
    uint32_t whole = cd_crc32(0, img, sizeof img);
    uint32_t c = 0;
    for (size_t off = 0; off < sizeof img; off += CD_CHUNK) c = cd_crc32(c, img + off, CD_CHUNK);
    CHECK(c == whole, "4 KB chunks == whole: %08x %08x", c, whole);
    c = 0;
    for (size_t off = 0; off < sizeof img; off += 1000) c = cd_crc32(c, img + off, sizeof img - off < 1000 ? sizeof img - off : 1000);
    CHECK(c == whole, "odd chunks == whole");
}

static uint8_t *make_image(uint32_t size, uint32_t *id)
{
    uint8_t *b = malloc(size);
    for (uint32_t i = 0; i < size - 4; i++) b[i] = (uint8_t)(i * 7 + 3);
    memcpy(b, &size, 4);
    *id = cd_crc32(0, b, size - 4);
    for (int i = 0; i < 4; i++) b[size - 4 + i] = (uint8_t)(*id >> (8 * i));
    return b;
}

static void test_check(void)
{
    uint32_t size = 9 * 1024 + 20, id;
    uint8_t *b = make_image(size, &id);
    cd_check_t c;
    cd_check_init(&c, id, size);
    for (uint32_t off = 0; off < size; off += CD_CHUNK) cd_check_feed(&c, b + off, size - off < CD_CHUNK ? size - off : CD_CHUNK);
    CHECK(cd_check_ok(&c), "chunked image passes");

    cd_check_init(&c, id, size);
    cd_check_feed(&c, b, size - 2);
    cd_check_feed(&c, b + size - 2, 2);
    CHECK(cd_check_ok(&c), "split inside the trailing CRC");

    cd_check_init(&c, id, size);
    cd_check_feed(&c, b, size - 1);
    CHECK(!cd_check_ok(&c), "short body fails");

    cd_check_init(&c, id ^ 1, size);
    cd_check_feed(&c, b, size);
    CHECK(!cd_check_ok(&c), "wrong id fails");

    b[100] ^= 0x40;
    cd_check_init(&c, id, size);
    cd_check_feed(&c, b, size);
    CHECK(!cd_check_ok(&c), "flipped bit fails");
    b[100] ^= 0x40;

    b[size - 1] ^= 1;
    cd_check_init(&c, id, size);
    cd_check_feed(&c, b, size);
    CHECK(!cd_check_ok(&c), "bad trailing CRC fails");
    b[size - 1] ^= 1;

    cd_check_init(&c, id, size);
    cd_check_feed(&c, b, size);
    cd_check_feed(&c, b, 1);
    CHECK(!cd_check_ok(&c), "extra byte fails");

    cd_check_init(&c, 0, 3);
    cd_check_feed(&c, b, 3);
    CHECK(!cd_check_ok(&c), "below 4 bytes fails");
    free(b);
}

static void test_id(void)
{
    char s[CD_ID_LEN + 1];
    cd_id_format(0x1a2b3c4d, s);
    CHECK(strcmp(s, "1a2b3c4d") == 0, "format %s", s);
    cd_id_format(0xabc, s);
    CHECK(strcmp(s, "00000abc") == 0, "zero padded %s", s);
    uint32_t v = 0;
    CHECK(cd_id_parse("1a2b3c4d", &v) && v == 0x1a2b3c4d, "parse");
    CHECK(cd_id_parse("ffffffff", &v) && v == 0xffffffffu, "parse max");
    CHECK(!cd_id_parse("1A2B3C4D", &v), "upper case rejected");
    CHECK(!cd_id_parse("1a2b3c4", &v) && !cd_id_parse("1a2b3c4d0", &v) && !cd_id_parse("", &v) &&
              !cd_id_parse(NULL, &v) && !cd_id_parse("1a2b3c4g", &v) && !cd_id_parse("0x2b3c4d", &v),
          "malformed rejected");
}

static void test_reply_parse(void)
{
    dev_checkin_result_t r;
    dev_checkin_parse("{\"config_version\":1,\"coredump_wanted\":\"1a2b3c4d\"}", &r);
    CHECK(r.has_coredump_wanted && r.coredump_wanted == 0x1a2b3c4d && !r.has_coredump_ack, "wanted");
    dev_checkin_result_free(&r);
    dev_checkin_parse("{\"config_version\":1,\"coredump_ack\":\"00000abc\"}", &r);
    CHECK(r.has_coredump_ack && r.coredump_ack == 0xabc && !r.has_coredump_wanted, "ack");
    dev_checkin_result_free(&r);
    dev_checkin_parse("{\"config_version\":1}", &r);
    CHECK(!r.has_coredump_ack && !r.has_coredump_wanted, "old server: neither");
    dev_checkin_result_free(&r);
    dev_checkin_parse("{\"config_version\":1,\"coredump_ack\":12345678,\"coredump_wanted\":\"XYZ\"}", &r);
    CHECK(!r.has_coredump_ack && !r.has_coredump_wanted, "malformed: neither");
    dev_checkin_result_free(&r);
}

static void test_body(void)
{
    char out[1024];
    dev_diag_t d = {.boots = 3, .has_crash = true, .crash_reason = "panic", .crash_pc = 0x4201a2b3,
                    .has_crash_id = true, .crash_id = 0x00c0ffee, .crash_size = 20516};
    strcpy(d.crash_task, "prof");
    dev_checkin_t c = {.fw = "0.9.14", .diag = &d};
    dev_checkin_body(&c, NULL, out, sizeof out);
    CHECK(strstr(out, "\"crash\":{\"id\":\"00c0ffee\",\"pc\":\"0x4201a2b3\",\"reason\":\"panic\",\"size\":20516,"
                      "\"task\":\"prof\"}") != NULL,
          "crash id and size, sorted keys: %s", out);
    d.has_crash_id = false;
    dev_checkin_body(&c, NULL, out, sizeof out);
    CHECK(strstr(out, "\"crash\":{\"pc\":\"0x4201a2b3\",\"reason\":\"panic\",\"task\":\"prof\"}") != NULL,
          "no id: as before: %s", out);
}

static void test_state(void)
{
    const uint32_t ID = 0x1a2b3c4d;
    cd_state_t s;
    cd_init(&s, true, ID);
    CHECK(!cd_upload_due(&s, 0, false), "nothing asked yet");
    CHECK(!cd_reply(&s, false, 0, false, 0) && !cd_upload_due(&s, 0, false), "old server: no upload, no erase");

    CHECK(!cd_reply(&s, true, ID + 1, false, 0) && !cd_upload_due(&s, 0, false), "wanted for another id: ignored");
    CHECK(!cd_reply(&s, false, 0, true, ID + 1), "ack for another id: no erase");
    CHECK(s.has_dump, "dump kept after a mismatching ack");

    CHECK(!cd_reply(&s, true, ID, false, 0), "wanted: no erase");
    CHECK(cd_upload_due(&s, 0, false), "wanted -> upload due");
    CHECK(!cd_upload_due(&s, 0, true), "never during a running Pomodoro");

    cd_upload_done(&s, false, 1000);
    CHECK(s.has_dump && !cd_upload_due(&s, 1000 + MIN_MS - 1, false) && cd_upload_due(&s, 1000 + MIN_MS, false),
          "fail 1: 1 min");
    cd_upload_done(&s, false, 2000);
    CHECK(!cd_upload_due(&s, 2000 + 5 * MIN_MS - 1, false) && cd_upload_due(&s, 2000 + 5 * MIN_MS, false), "fail 2: 5 min");
    cd_upload_done(&s, false, 3000);
    CHECK(!cd_upload_due(&s, 3000 + 30 * MIN_MS - 1, false) && cd_upload_due(&s, 3000 + 30 * MIN_MS, false),
          "fail 3: 30 min");
    cd_upload_done(&s, false, 4000);
    CHECK(cd_upload_due(&s, 4000 + 30 * MIN_MS, false) && !cd_upload_due(&s, 4000 + 30 * MIN_MS - 1, false),
          "fail 4: stays 30 min");
    CHECK(cd_backoff_ms(0) == 0 && cd_backoff_ms(1) == MIN_MS && cd_backoff_ms(2) == 5 * MIN_MS &&
              cd_backoff_ms(9) == 30 * MIN_MS,
          "backoff table");

    cd_init(&s, true, ID);
    cd_reply(&s, true, ID, false, 0);
    cd_upload_done(&s, true, 0);
    CHECK(!cd_upload_due(&s, 100 * MIN_MS, false), "uploaded: wait for the next reply");
    cd_reply(&s, true, ID, false, 0);
    CHECK(!cd_upload_due(&s, MIN_MS - 1, false) && cd_upload_due(&s, MIN_MS, false),
          "server still wants it after a 204: again after 1 min");
    CHECK(!cd_reply(&s, false, 0, false, 0) && !cd_upload_due(&s, 100 * MIN_MS, false), "reply without wanted clears it");

    CHECK(!cd_erase_due(&s, 100 * MIN_MS, false), "no erase before an ack");
    CHECK(cd_reply(&s, true, ID, true, ID), "matching ack, even with wanted");
    CHECK(!cd_upload_due(&s, 100 * MIN_MS, false), "no upload once acked");
    CHECK(cd_erase_due(&s, 0, false), "acked -> erase due at once");
    CHECK(!cd_erase_due(&s, 0, true), "erase deferred during a running Pomodoro");
    CHECK(!cd_reply(&s, false, 0, false, 0) && cd_erase_due(&s, 0, false), "ack sticks for this id");
    CHECK(!cd_reply(&s, true, ID, false, 0) && !cd_upload_due(&s, 100 * MIN_MS, false), "wanted after the ack: no second upload");
    cd_erased(&s);
    CHECK(!s.has_dump && !cd_reply(&s, true, ID, true, ID) && !cd_upload_due(&s, 100 * MIN_MS, false),
          "after erase: nothing");

    CHECK(!cd_erase_due(&s, 100 * MIN_MS, false), "after erase: no erase");

    cd_init(&s, false, 0);
    CHECK(!cd_reply(&s, true, 0, true, 0) && !cd_upload_due(&s, 0, false) && !cd_erase_due(&s, 0, false),
          "no dump: id 0 never matches");
}

static void test_erase_backoff(void)
{
    const uint32_t ID = 0x1a2b3c4d;
    cd_state_t s;
    cd_init(&s, true, ID);
    cd_reply(&s, false, 0, true, ID + 1);
    CHECK(!cd_erase_due(&s, 100 * MIN_MS, false), "mismatching ack: no erase");
    cd_reply(&s, false, 0, true, ID);
    CHECK(cd_erase_failed(&s, 1000), "first failure starts a streak (log it)");
    CHECK(s.has_dump && !cd_erase_due(&s, 1000 + MIN_MS - 1, false) && cd_erase_due(&s, 1000 + MIN_MS, false),
          "erase fail 1: 1 min");
    cd_reply(&s, false, 0, true, ID);
    CHECK(!cd_erase_due(&s, 1000 + MIN_MS - 1, false), "a repeated ack does not skip the backoff");
    CHECK(!cd_erase_failed(&s, 2000), "second failure: same streak, no log");
    CHECK(!cd_erase_due(&s, 2000 + 5 * MIN_MS - 1, false) && cd_erase_due(&s, 2000 + 5 * MIN_MS, false),
          "erase fail 2: 5 min");
    cd_erase_failed(&s, 3000);
    CHECK(!cd_erase_due(&s, 3000 + 30 * MIN_MS - 1, false) && cd_erase_due(&s, 3000 + 30 * MIN_MS, false),
          "erase fail 3: 30 min");
    cd_erase_failed(&s, 4000);
    CHECK(cd_erase_due(&s, 4000 + 30 * MIN_MS, false) && !cd_erase_due(&s, 4000 + 30 * MIN_MS - 1, false),
          "erase fail 4: stays 30 min");
    CHECK(!cd_erase_due(&s, 4000 + 30 * MIN_MS, true), "retry still waits for the Pomodoro");
    cd_erased(&s);
    CHECK(!s.has_dump && !cd_erase_due(&s, 100 * MIN_MS, false), "erased: done");

    cd_init(&s, true, ID);
    cd_reply(&s, true, ID, false, 0);
    cd_upload_done(&s, false, 0);
    cd_reply(&s, false, 0, true, ID);
    CHECK(cd_erase_due(&s, 0, false), "upload backoff does not delay the erase");
}

int main(void)
{
    test_crc();
    test_check();
    test_id();
    test_reply_parse();
    test_body();
    test_state();
    test_erase_backoff();
    if (failures) {
        printf("coredump: %d failure(s)\n", failures);
        return 1;
    }
    printf("coredump: all tests passed\n");
    return 0;
}
