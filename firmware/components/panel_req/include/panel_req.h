#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Panel requests run by the one panel owner (the LVGL task) between frames: brightness writes and the link check,
   a WRDISBV (0x51) write / RDDISBV (0x52) read loopback. Requesters never block here. One requester task for checks. */

#define PR_SETTLE_MS 60

typedef struct {
    /* 0 on success. */
    int (*read)(void *ctx, uint8_t *level);
    int (*write)(void *ctx, uint8_t level);
    void *ctx;
} pr_io_t;

typedef struct {
    atomic_int bright;
    atomic_uint req;
    atomic_uint done;
    atomic_uint result;
    uint32_t posted;
    int step;
    uint32_t run;
    bool dirty;
    uint8_t cur, vals[2], got;
    int bad;
    int64_t at_ms;
} panel_req_t;

void pr_init(panel_req_t *p);

/* Any task. The newest level wins; it is written at the next frame without a check running, else after the check. */
void pr_brightness(panel_req_t *p, uint8_t level);

/* Requester task. Returns the ticket for pr_check_result; a later post replaces a check that has not started. */
uint32_t pr_check_post(panel_req_t *p, int seed);

/* Requester task. True once the ticket's check is done: bad = mismatching reads (0 good) or -1 on a panel I/O error;
   raw (optional, 3 B): level before, level written, last level read. */
bool pr_check_result(panel_req_t *p, uint32_t ticket, int *bad, uint8_t raw[3]);

/* Owner task, between frames. */
void pr_frame(panel_req_t *p, const pr_io_t *io, int64_t now_ms);

#ifdef __cplusplus
}
#endif
