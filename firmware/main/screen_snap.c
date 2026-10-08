#include "screen_snap.h"

#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

/* WANTED -> READY only by CAS: a snapshot finished after the requester timed out is destroyed. RELEASE -> IDLE on the LVGL task. */
enum { ST_IDLE, ST_WANTED, ST_READY, ST_FAILED, ST_RELEASE };
static atomic_int s_state;
static lv_draw_buf_t *s_buf;

void screen_snap_reap(void)
{
    if (atomic_load(&s_state) != ST_RELEASE) return;
    lv_draw_buf_destroy(s_buf);
    s_buf = NULL;
    atomic_store(&s_state, ST_IDLE);
}

void screen_snap_frame(void)
{
    if (atomic_load(&s_state) != ST_WANTED) return;
    lv_draw_buf_t *b = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    s_buf = b;
    int want = ST_WANTED;
    if (!atomic_compare_exchange_strong(&s_state, &want, b ? ST_READY : ST_FAILED)) {
        s_buf = NULL;
        if (b) lv_draw_buf_destroy(b);
    }
}

snap_result_t screen_snap_take(uint32_t timeout_ms, const uint8_t **px, int *w, int *h, int *stride)
{
    uint32_t t = 0;
    for (int idle = ST_IDLE; !atomic_compare_exchange_strong(&s_state, &idle, ST_WANTED); idle = ST_IDLE) {
        if (idle != ST_RELEASE || t >= timeout_ms) return SNAP_BUSY;
        vTaskDelay(pdMS_TO_TICKS(10));
        t += 10;
    }
    for (; t < timeout_ms && atomic_load(&s_state) == ST_WANTED; t += 10) vTaskDelay(pdMS_TO_TICKS(10));
    int st = ST_WANTED;
    if (atomic_compare_exchange_strong(&s_state, &st, ST_IDLE)) return SNAP_FAILED;
    if (st != ST_READY) {
        atomic_store(&s_state, ST_IDLE);
        return SNAP_FAILED;
    }
    *px = s_buf->data;
    *w = s_buf->header.w;
    *h = s_buf->header.h;
    *stride = s_buf->header.stride;
    return SNAP_OK;
}

void screen_snap_release(void)
{
    int ready = ST_READY;
    atomic_compare_exchange_strong(&s_state, &ready, ST_RELEASE);
}
