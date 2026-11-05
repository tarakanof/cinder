#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ember_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pomo_client.h"
#include "print_client.h"
#include "rom/tjpgd.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int64_t s_us = 1000000;
static int s_status = 200, s_len = 2000, s_px = 466, s_gets, s_wakes, s_live;
static JRESULT s_jd = JDR_OK;
static char s_urls[8][256];
static const char *s_kind;
static void (*s_during)(int get);

int64_t esp_timer_get_time(void) { return s_us; }
void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...) { (void)level, (void)tag, (void)fmt; }
void ember_client_wake(void) { s_wakes++; }
double pomo_client_now(void) { return s_us / 1e6; }
bool pomo_client_srv_offset(double *offset)
{
    *offset = 1782043200.0 - 1.0;
    return true;
}
void *host_heap_alloced(void *p)
{
    if (p) s_live++;
    return p;
}
void host_heap_freed(void *p)
{
    if (p) s_live--;
}

int http_conn_req_opts(http_conn_t *c, const char *url, const http_req_opts_t *o, char *buf, int cap)
{
    (void)c, (void)buf;
    snprintf(s_urls[s_gets % 8], sizeof s_urls[0], "%s", url);
    s_gets++;
    s_kind = strstr(url, "kind=ghost") ? "ghost" : "solid";
    if (s_during) s_during(s_gets);
    *o->body_len = s_len > cap ? cap + 1000 : s_len;
    return s_status;
}

JRESULT jd_prepare(JDEC *jd, UINT (*in)(JDEC *, BYTE *, UINT), void *work, UINT n, void *dev)
{
    (void)in, (void)work, (void)n;
    jd->width = jd->height = (UINT)s_px;
    jd->device = dev;
    return s_jd;
}

static uint16_t c565(int x, int y, int ghost) { return (uint16_t)((x & 0x1F) << 11 | (y & 0x3F) << 5 | (ghost ? 1 : 2)); }

JRESULT jd_decomp(JDEC *jd, UINT (*out)(JDEC *, void *, JRECT *), BYTE scale)
{
    (void)scale;
    static uint8_t mcu[16 * 16 * 3];
    int ghost = strcmp(s_kind, "ghost") == 0;
    for (int by = 0; by < 466; by += 16)
        for (int bx = 0; bx < 466; bx += 16) {
            JRECT r = {(WORD)bx, (WORD)(bx + 15 < 465 ? bx + 15 : 465), (WORD)by, (WORD)(by + 15 < 465 ? by + 15 : 465)};
            int w = r.right - r.left + 1, k = 0;
            for (int y = r.top; y <= r.bottom; y++)
                for (int x = r.left; x < r.left + w; x++, k += 3) {
                    mcu[k] = (uint8_t)((x & 0x1F) << 3);
                    mcu[k + 1] = (uint8_t)((y & 0x3F) << 2);
                    mcu[k + 2] = (uint8_t)((ghost ? 1 : 2) << 3);
                }
            out(jd, mcu, &r);
        }
    return JDR_OK;
}

static http_conn_t s_conn;
static print_block_t s_b;

static void feed(const char *version, int top)
{
    memset(&s_b, 0, sizeof s_b);
    s_b.phase = PRINT_PRINTING;
    s_b.has_progress = true;
    s_b.progress = 24;
    s_b.ends_at = 1782051245LL;
    strcpy(s_b.thumb_version, version);
    s_b.thumb_top = version[0] ? top : 0;
    s_b.thumb_bottom = version[0] ? 300 : 0;
    print_client_feed(&s_b, true, 3600);
}

static void service_after(int ms)
{
    s_us += (int64_t)ms * 1000;
    print_client_service(&s_conn, "http://ember");
}

static void switch_job(int get)
{
    if (get == 2) feed("bbbbbbbbbbbb-r1", 120);
}

int main(void)
{
    print_snapshot_t snap;
    print_pics_t p;
    print_client_get(&snap);
    CHECK(!snap.fed && !print_client_pending() && !print_client_take_pics(&p), "before init: nothing");
    print_client_init();

    feed("aaaaaaaaaaaa-r1", 110);
    print_client_get(&snap);
    CHECK(snap.fed && snap.has && snap.b.progress == 24 && snap.have_srv && snap.srv_offset == 1782043199.0 &&
              snap.has_utc_offset && snap.utc_offset == 3600,
          "the snapshot carries the block, Ember's clock and the zone");
    unsigned gen = snap.gen;
    feed("aaaaaaaaaaaa-r1", 110);
    print_client_get(&snap);
    CHECK(snap.gen == gen, "the same block again: same gen");
    CHECK(!print_client_pending(), "hidden: no picture is due");
    service_after(0);
    CHECK(s_gets == 0, "hidden: nothing fetched");

    print_client_set_visible(true);
    CHECK(s_wakes == 1 && print_client_pending(), "shown: the link task is woken and the pair is due");
    service_after(0);
    CHECK(s_gets == 2 && strcmp(s_urls[0], "http://ember/v1/print/thumb?kind=ghost&v=aaaaaaaaaaaa-r1") == 0 &&
              strcmp(s_urls[1], "http://ember/v1/print/thumb?kind=solid&v=aaaaaaaaaaaa-r1") == 0,
          "ghost then solid with the version: %s %s", s_urls[0], s_urls[1]);
    CHECK(!print_client_pending(), "handed: not due again");
    CHECK(print_client_take_pics(&p) && strcmp(p.version, "aaaaaaaaaaaa-r1") == 0 && p.top == 110 && p.bottom == 300,
          "the pair with its version and rows");
    bool crop = true;
    for (int y = 110; y < 300 && crop; y++)
        for (int x = 0; x < PRINT_PIC_W && crop; x++) {
            int fx = x + PRINT_PIC_X0;
            crop = p.ghost[(y - 110) * PRINT_PIC_W + x] == c565(fx, y, 1) && p.solid[(y - 110) * PRINT_PIC_W + x] == c565(fx, y, 0);
        }
    CHECK(crop, "each picture is rows 110-299, columns 93-372 of its 466 px frame");
    print_pics_t q;
    CHECK(!print_client_take_pics(&q), "taken once");
    print_pics_free(&p);
    CHECK(s_live == 0, "no buffer left after the free: %d", s_live);

    print_client_pics_dropped();
    CHECK(print_client_pending(), "the view dropped its pair: the same version is due again");
    service_after(0);
    CHECK(s_gets == 4 && print_client_take_pics(&p), "fetched again");
    print_pics_free(&p);

    s_status = 404;
    feed("cccccccccccc-r1", 110);
    service_after(0);
    CHECK(s_gets == 5 && !print_client_take_pics(&p) && !print_client_pending(), "404 on the ghost: no pair, backing off");
    CHECK(s_live == 0, "nothing kept after a 404: %d", s_live);
    service_after(4999);
    CHECK(s_gets == 5 && !print_client_pending(), "5 s backoff holds");
    s_us += 1000;
    CHECK(print_client_pending(), "due after 5 s");
    service_after(0);
    CHECK(s_gets == 6, "retried");
    service_after(9999);
    CHECK(s_gets == 6, "then 10 s");
    service_after(1);
    CHECK(s_gets == 7, "retried after 10 s");
    feed("dddddddddddd-r1", 110);
    CHECK(print_client_pending(), "a new version is due at once");
    s_status = 200;
    service_after(0);
    CHECK(s_gets == 9 && print_client_take_pics(&p) && strcmp(p.version, "dddddddddddd-r1") == 0, "the new version's pair");
    print_pics_free(&p);

    feed("eeeeeeeeeeee-r1", 110);
    s_len = PRINT_JPEG_MAX;
    service_after(0);
    CHECK(s_gets == 10 && !print_client_take_pics(&p), "a body of 96 KB or more is refused");
    s_len = 2000;
    feed("ffffffffffff-r1", 110);
    s_px = 240;
    service_after(0);
    CHECK(s_gets == 11 && !print_client_take_pics(&p), "a picture that is not 466 px is refused, the solid not asked");
    s_px = 466;
    feed("gggggggggggg-r1", 110);
    s_jd = JDR_FMT1;
    service_after(0);
    CHECK(!print_client_take_pics(&p) && !print_client_pending(), "an undecodable JPEG backs off");
    s_jd = JDR_OK;
    CHECK(s_live == 0, "nothing kept after the failures: %d", s_live);

    feed("hhhhhhhhhhhh-r1", 110);
    s_during = switch_job;
    s_gets = 0;
    service_after(0);
    s_during = NULL;
    CHECK(!print_client_take_pics(&p), "the job changed during the fetch: the old pair is dropped");
    CHECK(s_live == 0 && print_client_pending(), "and the new job's pair is due");
    service_after(0);
    CHECK(print_client_take_pics(&p) && strcmp(p.version, "bbbbbbbbbbbb-r1") == 0 && p.top == 120, "the new job's pair");
    print_pics_free(&p);

    feed("iiiiiiiiiiii-r1", 110);
    service_after(0);
    feed("jjjjjjjjjjjj-r1", 110);
    service_after(0);
    CHECK(print_client_take_pics(&p) && strcmp(p.version, "jjjjjjjjjjjj-r1") == 0, "an untaken pair is replaced by the newer");
    print_pics_free(&p);
    CHECK(s_live == 0, "the replaced pair was freed: %d", s_live);

    print_client_feed(NULL, false, 0);
    print_client_get(&snap);
    CHECK(snap.fed && !snap.has && !print_client_pending(), "no block: nothing due");
    feed("kkkkkkkkkkkk-r1", 110);
    print_client_set_visible(false);
    CHECK(!print_client_pending(), "hidden again: nothing due");

    if (failures) {
        printf("print_client: %d failure(s)\n", failures);
        return 1;
    }
    printf("print_client: all tests passed\n");
    return 0;
}
