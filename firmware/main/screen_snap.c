#include "screen_snap.h"

#include <stdatomic.h>

#include "bsp_knob_15_md50et.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

/* WANTED -> READY only by CAS: a snapshot finished after the requester timed out is destroyed. */
enum { ST_IDLE, ST_WANTED, ST_READY, ST_FAILED };
static atomic_int s_state;
static lv_draw_buf_t *s_buf;

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
    int idle = ST_IDLE;
    if (!atomic_compare_exchange_strong(&s_state, &idle, ST_WANTED)) return SNAP_BUSY;
    for (uint32_t t = 0; t < timeout_ms && atomic_load(&s_state) == ST_WANTED; t += 10) vTaskDelay(pdMS_TO_TICKS(10));
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
    if (atomic_load(&s_state) != ST_READY) return;
    bsp_knob_15_md50et_lock(-1);
    lv_draw_buf_destroy(s_buf);
    bsp_knob_15_md50et_unlock();
    s_buf = NULL;
    atomic_store(&s_state, ST_IDLE);
}
