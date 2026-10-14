#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ember_client.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nowplaying_client.h"
#include "pomo_client.h"
#include "rom/tjpgd.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef enum { ANS_GOOD, ANS_OVER, ANS_UNDECODABLE, ANS_500 } answer_t;

static int64_t s_us = 1000000;
static answer_t s_cover = ANS_GOOD;
static int s_cover_gets, s_artist_gets;
static UINT s_px;
static JRESULT s_jd;

int64_t esp_timer_get_time(void) { return s_us; }
void esp_log_host(esp_log_level_t level, const char *tag, const char *fmt, ...) { (void)level, (void)tag, (void)fmt; }
uint32_t esp_random(void) { return 4; }
void ember_client_wake(void) {}
ember_link_t ember_client_link(void) { return (ember_link_t)0; }
bool config_store_has_device(void) { return true; }
double pomo_client_now(void) { return s_us / 1e6; }
bool pomo_client_srv_offset(double *offset) { (void)offset; return false; }

int http_conn_req_opts(http_conn_t *c, const char *url, const http_req_opts_t *o, char *buf, int cap)
{
    (void)c, (void)buf;
    bool cover = strstr(url, "kind=cover") != NULL;
    const char *sz = strstr(url, "size=");
    s_px = sz ? (UINT)atoi(sz + 5) : 0;
    s_jd = JDR_OK;
    answer_t a = ANS_GOOD;
    if (cover) {
        s_cover_gets++;
        a = s_cover;
    } else {
        s_artist_gets++;
    }
    if (a == ANS_500) return 500;
    if (o->on_header) o->on_header("Cache-Control", "public, max-age=86400, immutable", o->hdr_ctx);
    *o->body_len = a == ANS_OVER ? cap + 1000 : 2000;
    if (a == ANS_UNDECODABLE) s_jd = JDR_FMT1;
    return 200;
}

JRESULT jd_prepare(JDEC *jd, UINT (*in)(JDEC *, BYTE *, UINT), void *work, UINT n, void *dev)
{
    (void)in, (void)work, (void)n;
    jd->width = jd->height = s_px;
    jd->device = dev;
    return s_jd;
}

JRESULT jd_decomp(JDEC *jd, UINT (*out)(JDEC *, void *, JRECT *), BYTE scale)
{
    (void)jd, (void)out, (void)scale;
    return JDR_OK;
}

static np_info_t block(const char *cover_version)
{
    np_info_t np = {.state = NP_PLAYING, .volume = -1, .duration_ms = 200000, .artist_art = true};
    strcpy(np.title, cover_version);
    strcpy(np.art_version, "a1b2c3d4e5f6");
    strcpy(np.cover_version, cover_version);
    return np;
}

static http_conn_t s_conn;

static void service_at(int64_t ms)
{
    s_us += ms * 1000;
    np_client_service(&s_conn, "http://ember");
}

static bool take(np_art_set_t *set)
{
    memset(set, 0, sizeof *set);
    return np_client_take_art(set);
}

static void test_give_up(void)
{
    np_art_set_t set;
    np_info_t a = block("bad0-r1");
    np_client_feed(&a);
    np_client_set_visible(true);
    s_cover = ANS_OVER;
    service_at(0);
    CHECK(s_cover_gets == 1 && !take(&set), "first over-cap cover: nothing published");
    service_at(1000);
    CHECK(s_cover_gets == 1, "backoff holds the retry");
    service_at(5000);
    CHECK(s_cover_gets == 2 && !take(&set), "second try after 5 s");
    s_cover = ANS_UNDECODABLE;
    service_at(10000);
    CHECK(s_cover_gets == 3, "third try after 10 s more: %d", s_cover_gets);
    CHECK(take(&set) && set.no_cover && set.is_cover && !set.cover && set.avatar && strcmp(set.version, "bad0-r1") == 0,
          "third bad answer: a no_cover set with the avatar");
    np_art_set_free(&set);
    service_at(1000);
    CHECK(s_cover_gets == 3 && !take(&set), "given up and shown: nothing more");

    np_info_t none = {.state = NP_NONE, .volume = -1};
    np_client_feed(&none);
    service_at(1000);
    if (take(&set)) np_art_set_free(&set);
    np_client_feed(&a);
    np_client_forget_art();
    int artist = s_artist_gets;
    service_at(10);
    CHECK(s_cover_gets == 3 && s_artist_gets == artist + 1, "back from idle: no cover download (%d)", s_cover_gets);
    CHECK(take(&set) && set.no_cover && !set.cover && set.avatar, "and the no_cover set at once, with no wait");
    np_art_set_free(&set);
    service_at(120000);
    CHECK(s_cover_gets == 3 && !np_client_pending(), "given up: no more downloads");

    s_cover = ANS_GOOD;
    np_info_t b = block("good-r1");
    np_client_feed(&b);
    service_at(10);
    CHECK(take(&set) && set.cover && !set.no_cover, "a new cover_version fetches again");
    np_art_set_free(&set);
}

static void test_success_resets(void)
{
    np_art_set_t set;
    np_info_t c = block("flaky-r1");
    np_client_feed(&c);
    s_cover = ANS_OVER;
    service_at(10);
    service_at(5000);
    int gets = s_cover_gets;
    s_cover = ANS_GOOD;
    service_at(10000);
    CHECK(s_cover_gets == gets + 1 && take(&set) && set.cover && !set.no_cover, "two bad, then a good answer: shown");
    np_art_set_free(&set);
    np_client_forget_art();
    s_cover = ANS_OVER;
    service_at(10);
    service_at(5000);
    CHECK(!take(&set), "a success of the version clears its count: two more bad answers do not give up");
    service_at(10000);
    CHECK(take(&set) && set.no_cover, "the third does");
    np_art_set_free(&set);

    np_info_t d = block("busy-r1");
    np_client_feed(&d);
    s_cover = ANS_500;
    for (int i = 0; i < 8; i++) {
        service_at(60000);
        CHECK(!take(&set), "server errors never give up (%d)", i);
    }
    s_cover = ANS_GOOD;
    service_at(60000);
    CHECK(take(&set) && set.cover, "and the cover comes when the server does");
    np_art_set_free(&set);
}

int main(void)
{
    np_client_init();
    test_give_up();
    test_success_resets();
    if (failures) {
        printf("np_client: %d failure(s)\n", failures);
        return 1;
    }
    printf("np_client: all tests passed\n");
    return 0;
}
