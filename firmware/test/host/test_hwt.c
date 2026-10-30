#include <stdio.h>
#include <string.h>

#include "hwt_line.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_ops(void)
{
    hwt_req_t q;
    const char *e;
    CHECK(hwt_parse("HWT1 {\"id\":7,\"op\":\"info\"}", &q, &e) && q.id == 7 && q.op == HWT_INFO && !e, "info");
    CHECK(hwt_parse("HWT1 {\"op\":\"probe\",\"gpio\":39,\"level\":0}", &q, &e) && q.op == HWT_PROBE && q.gpio == 39 && q.level == 0, "probe");
    CHECK(hwt_parse("HWT1 {\"op\":\"probe\",\"gpio\":-1}", &q, &e) && q.gpio == -1, "probe off");
    CHECK(hwt_parse("HWT1 {\"op\":\"tone\",\"hz\":880,\"ms\":400,\"vol\":20}", &q, &e) && q.op == HWT_TONE && q.hz == 880 && q.ms == 400 &&
              q.vol == 20, "tone");
    CHECK(hwt_parse("HWT1 {\"op\":\"pins\",\"bclk\":39,\"lrc\":40,\"din\":38,\"radar_bit\":3,\"pcf\":57}", &q, &e) && q.pin[HWT_PIN_BCLK] == 39 &&
              q.pin[HWT_PIN_LRC] == 40 && q.pin[HWT_PIN_DIN] == 38 && q.pin[HWT_PIN_SDA] == HWT_UNSET && q.radar_bit == 3 && q.pcf == 57,
          "pins");
    CHECK(hwt_parse("HWT1 {\"op\":\"room\",\"do\":\"start\"}", &q, &e) && q.op == HWT_ROOM && q.room == HWT_ROOM_START, "room start");
    CHECK(hwt_parse("HWT1 {\"op\":\"room\"}", &q, &e) && q.room == HWT_ROOM_GET, "room get by default");
    CHECK(hwt_parse("HWT1 {\"op\":\"watch\",\"s\":60}", &q, &e) && q.op == HWT_WATCH && q.s == 60, "watch");
    CHECK(hwt_parse("HWT1 {\"op\":\"stop\"}", &q, &e) && q.op == HWT_STOP && q.gpio == HWT_UNSET && q.ms == HWT_UNSET, "unset fields");
}

static void test_rejects(void)
{
    hwt_req_t q;
    const char *e;
    CHECK(!hwt_parse("CINDER1 {\"op\":\"info\"}", &q, &e) && !strcmp(e, "prefix"), "other prefix");
    CHECK(!hwt_parse("HWT1 not json", &q, &e) && !strcmp(e, "json"), "bad JSON");
    CHECK(!hwt_parse("HWT1 [1]", &q, &e) && !strcmp(e, "json"), "not an object");
    CHECK(!hwt_parse("HWT1 {\"op\":\"flash\"}", &q, &e) && !strcmp(e, "op"), "unknown op");
    CHECK(!hwt_parse("HWT1 {\"op\":5}", &q, &e) && !strcmp(e, "op"), "op not a string");
    CHECK(!hwt_parse("HWT1 {\"op\":\"probe\",\"gpio\":49}", &q, &e) && !strcmp(e, "gpio"), "gpio range");
    CHECK(!hwt_parse("HWT1 {\"op\":\"probe\",\"gpio\":39.5}", &q, &e) && !strcmp(e, "gpio"), "gpio integer");
    CHECK(!hwt_parse("HWT1 {\"op\":\"tone\",\"hz\":1e12}", &q, &e) && !strcmp(e, "hz"), "huge number");
    CHECK(!hwt_parse("HWT1 {\"op\":\"tone\",\"vol\":101}", &q, &e) && !strcmp(e, "vol"), "volume range");
    CHECK(!hwt_parse("HWT1 {\"op\":\"pins\",\"sda\":\"44\"}", &q, &e) && !strcmp(e, "sda"), "pin as a string");
    CHECK(!hwt_parse("HWT1 {\"op\":\"room\",\"do\":\"reboot\"}", &q, &e) && !strcmp(e, "do"), "room action");
    CHECK(!hwt_parse("HWT1 {\"op\":\"info\",\"id\":-1}", &q, &e) && !strcmp(e, "id"), "negative id");
    char big[HWT_LINE_MAX + 32];
    memset(big, ' ', sizeof big);
    const char *head = "HWT1 {\"op\":\"info\"";
    memcpy(big, head, strlen(head));
    big[sizeof big - 2] = '}';
    big[sizeof big - 1] = 0;
    CHECK(!hwt_parse(big, &q, &e) && !strcmp(e, "too_long"), "line over HWT_LINE_MAX");
    CHECK(hwt_parse("HWT1 {\"op\":\"info\",\"id\":3}", &q, &e) && q.id == 3, "a good line after rejects");
    CHECK(!hwt_parse("HWT1 {\"id\":9,\"op\":\"nope\"}", &q, &e) && q.id == 9, "the id survives a bad op, for the error reply");
}

int main(void)
{
    test_ops();
    test_rejects();
    if (failures) {
        printf("test_hwt: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_hwt: ok (ops, ranges, rejects)\n");
    return 0;
}
