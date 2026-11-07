#include "print_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "ember_client.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pomo_client.h"
#include "rom/tjpgd.h"

static const char *TAG = "print";

#define RETRY_MIN_MS 5000
#define RETRY_MAX_MS 300000
#define TJPGD_WORK 3100

typedef struct {
    print_snapshot_t snap;
    uint32_t gen;
    bool visible;
    char want[PRINT_THUMBV_MAX + 1];
    int want_top, want_bottom;
    char handed[PRINT_THUMBV_MAX + 1];
    print_pics_t pending;
    bool has_pending;
    char failed[PRINT_THUMBV_MAX + 1];
    int fails;
    int64_t retry_at_ms;
    char url[CFG_URL_MAX + 80];
} print_shared_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static EXT_RAM_BSS_ATTR print_shared_t S;
static bool s_init;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void print_client_init(void)
{
    taskENTER_CRITICAL(&s_lock);
    memset(&S, 0, sizeof S);
    S.gen = 1;
    s_init = true;
    taskEXIT_CRITICAL(&s_lock);
}

void print_pics_free(print_pics_t *p)
{
    heap_caps_free(p->ghost);
    heap_caps_free(p->solid);
    memset(p, 0, sizeof *p);
}

void print_client_feed(const print_block_t *b, bool has_utc_offset, int utc_offset)
{
    if (!s_init) return;
    double srv = 0;
    bool have_srv = pomo_client_srv_offset(&srv);
    taskENTER_CRITICAL(&s_lock);
    print_snapshot_t *s = &S.snap;
    if (b) {
        s->b = *b;
        strcpy(S.want, b->thumb_version);
        S.want_top = b->thumb_top;
        S.want_bottom = b->thumb_bottom;
    } else {
        memset(&s->b, 0, sizeof s->b);
        S.want[0] = 0;
    }
    s->fed = true;
    s->has = b != NULL;
    s->have_srv = have_srv;
    s->srv_offset = srv;
    s->has_utc_offset = has_utc_offset;
    s->utc_offset = has_utc_offset ? utc_offset : 0;
    S.gen++;
    taskEXIT_CRITICAL(&s_lock);
}

bool print_client_get(print_snapshot_t *out, uint32_t *gen)
{
    bool fresh = false;
    taskENTER_CRITICAL(&s_lock);
    if (S.gen != *gen) {
        *out = S.snap;
        *gen = S.gen;
        fresh = true;
    }
    taskEXIT_CRITICAL(&s_lock);
    return fresh;
}

void print_client_set_visible(bool on)
{
    if (!s_init) return;
    taskENTER_CRITICAL(&s_lock);
    bool was = S.visible;
    S.visible = on;
    taskEXIT_CRITICAL(&s_lock);
    if (on && !was) ember_client_wake();
}

static bool due_locked(int64_t now)
{
    if (!S.visible || !S.want[0] || strcmp(S.want, S.handed) == 0) return false;
    return strcmp(S.want, S.failed) != 0 || now >= S.retry_at_ms;
}

bool print_client_pending(void)
{
    if (!s_init) return false;
    int64_t now = now_ms();
    taskENTER_CRITICAL(&s_lock);
    bool due = due_locked(now);
    taskEXIT_CRITICAL(&s_lock);
    return due;
}

void print_client_pics_dropped(void)
{
    if (!s_init) return;
    taskENTER_CRITICAL(&s_lock);
    if (!S.has_pending) S.handed[0] = 0;
    taskEXIT_CRITICAL(&s_lock);
}

bool print_client_take_pics(print_pics_t *out)
{
    if (!s_init) return false;
    taskENTER_CRITICAL(&s_lock);
    bool got = S.has_pending;
    if (got) {
        *out = S.pending;
        memset(&S.pending, 0, sizeof S.pending);
        S.has_pending = false;
    }
    taskEXIT_CRITICAL(&s_lock);
    return got;
}

typedef struct {
    const uint8_t *src;
    size_t len, pos;
    uint16_t *dst;
    int top, bottom;
    bool past;
} jd_io_t;

static UINT jd_in(JDEC *jd, BYTE *buf, UINT n)
{
    jd_io_t *io = jd->device;
    if (n > io->len - io->pos) n = io->len - io->pos;
    if (buf) memcpy(buf, io->src + io->pos, n);
    io->pos += n;
    return n;
}

static UINT jd_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jd_io_t *io = jd->device;
    if (r->top >= io->bottom) {
        io->past = true;
        return 0;
    }
    int w = r->right - r->left + 1;
    int x0 = r->left > PRINT_PIC_X0 ? r->left : PRINT_PIC_X0, x1 = r->right + 1 < PRINT_PIC_X1 ? r->right + 1 : PRINT_PIC_X1;
    if (x0 >= x1) return 1;
    int y0 = r->top > io->top ? r->top : io->top, y1 = r->bottom + 1 < io->bottom ? r->bottom + 1 : io->bottom;
    if (y0 >= y1) return 1;
    const uint8_t *s = (const uint8_t *)bitmap + (size_t)3 * w * (y0 - r->top);
    for (int y = y0; y < y1; y++, s += 3 * w) {
        uint16_t *d = io->dst + (size_t)(y - io->top) * PRINT_PIC_W + (x0 - PRINT_PIC_X0);
        const uint8_t *p = s + 3 * (x0 - r->left);
        for (int x = x0; x < x1; x++, p += 3) *d++ = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
    }
    return 1;
}

/* 466 is not a multiple of the 16 px MCU: TJpgDec clips the last MCU, so no padding. */
static uint16_t *decode(const uint8_t *jpg, int len, int top, int bottom)
{
    void *work = heap_caps_malloc(TJPGD_WORK, MALLOC_CAP_SPIRAM);
    if (!work) work = heap_caps_malloc(TJPGD_WORK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint16_t *out = heap_caps_calloc((size_t)PRINT_PIC_W * (bottom - top), 2, MALLOC_CAP_SPIRAM);
    jd_io_t io = {.src = jpg, .len = (size_t)len, .dst = out, .top = top, .bottom = bottom};
    JDEC jd;
    JRESULT r = work && out ? jd_prepare(&jd, jd_in, work, TJPGD_WORK, &io) : JDR_MEM1;
    if (r == JDR_OK && (jd.width != PRINT_FRAME_PX || jd.height != PRINT_FRAME_PX)) {
        ESP_LOGW(TAG, "picture is %ux%u, want %d", jd.width, jd.height, PRINT_FRAME_PX);
        r = JDR_PAR;
    }
    if (r == JDR_OK) r = jd_decomp(&jd, jd_out, 0);
    if (r == JDR_INTR && io.past) r = JDR_OK;
    heap_caps_free(work);
    if (r != JDR_OK) {
        heap_caps_free(out);
        ESP_LOGW(TAG, "JPEG decode failed (%d)", (int)r);
        return NULL;
    }
    return out;
}

static void fail(const char *version)
{
    taskENTER_CRITICAL(&s_lock);
    if (strcmp(S.failed, version) != 0) S.fails = 0;
    strcpy(S.failed, version);
    S.fails++;
    int64_t wait = RETRY_MIN_MS;
    for (int i = 1; i < S.fails && wait < RETRY_MAX_MS; i++) wait *= 2;
    S.retry_at_ms = now_ms() + (wait > RETRY_MAX_MS ? RETRY_MAX_MS : wait);
    taskEXIT_CRITICAL(&s_lock);
}

void print_client_service(http_conn_t *conn, const char *base)
{
    if (!s_init) return;
    char v[PRINT_THUMBV_MAX + 1];
    int top, bottom;
    taskENTER_CRITICAL(&s_lock);
    bool due = due_locked(now_ms());
    strcpy(v, S.want);
    top = S.want_top;
    bottom = S.want_bottom;
    taskEXIT_CRITICAL(&s_lock);
    if (!due) return;
    int64_t t0 = esp_timer_get_time();
    uint8_t *jpg = heap_caps_malloc(PRINT_JPEG_MAX, MALLOC_CAP_SPIRAM);
    print_pics_t set = {.top = top, .bottom = bottom};
    strcpy(set.version, v);
    static const char *const KIND[2] = {"ghost", "solid"};
    int bytes = 0, status = 0;
    for (int k = 0; k < 2 && jpg; k++) {
        snprintf(S.url, sizeof S.url, "%s/v1/print/thumb?kind=%s&v=%s", base, KIND[k], v);
        int len = 0;
        const http_req_opts_t o = {.idempotent = true, .auth = true, .body_len = &len};
        status = http_conn_req_opts(conn, S.url, &o, (char *)jpg, PRINT_JPEG_MAX);
        if (status != 200 || len >= PRINT_JPEG_MAX) {
            ESP_LOGW(TAG, "picture %s %s: HTTP %d, %d B%s", KIND[k], v, status, len,
                     status == 404 ? " (the job changed)" : "");
            break;
        }
        bytes += len;
        uint16_t *img = decode(jpg, len, top, bottom);
        if (!img) break;
        if (k == 0) set.ghost = img;
        else set.solid = img;
    }
    heap_caps_free(jpg);
    if (!set.ghost || !set.solid) {
        print_pics_free(&set);
        fail(v);
        return;
    }
    ESP_LOGI(TAG, "pictures %s: %d B in %lld ms, rows %d-%d", v, bytes, (long long)((esp_timer_get_time() - t0) / 1000),
             top, bottom);
    print_pics_t old = {0};
    taskENTER_CRITICAL(&s_lock);
    if (strcmp(S.want, v) == 0) {
        if (S.has_pending) old = S.pending;
        S.pending = set;
        S.has_pending = true;
        strcpy(S.handed, v);
        S.fails = 0;
        S.failed[0] = 0;
        memset(&set, 0, sizeof set);
    }
    taskEXIT_CRITICAL(&s_lock);
    print_pics_free(&old);
    print_pics_free(&set);
}
