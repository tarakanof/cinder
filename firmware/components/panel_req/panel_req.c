#include "panel_req.h"

void pr_init(panel_req_t *p)
{
    atomic_init(&p->bright, -1);
    atomic_init(&p->req, 0);
    atomic_init(&p->done, 0);
    atomic_init(&p->result, 0);
    p->posted = 0;
    p->step = 0;
    p->run = 0;
    p->dirty = false;
    p->bad = 0;
}

void pr_brightness(panel_req_t *p, uint8_t level) { atomic_store(&p->bright, level); }

uint32_t pr_check_post(panel_req_t *p, int seed)
{
    p->posted = (p->posted + 1) & 0x7FFFFFFFu;
    if (!p->posted) p->posted = 1;
    atomic_store(&p->req, p->posted << 1 | ((unsigned)seed & 1u));
    return p->posted;
}

bool pr_check_result(panel_req_t *p, uint32_t ticket, int *bad, uint8_t raw[3])
{
    if (atomic_load(&p->done) != ticket) return false;
    unsigned r = atomic_load(&p->result);
    if (atomic_load(&p->done) != ticket) return false;
    *bad = (int)(r & 0xFF) - 1;
    if (raw) {
        raw[0] = (uint8_t)(r >> 8);
        raw[1] = (uint8_t)(r >> 16);
        raw[2] = (uint8_t)(r >> 24);
    }
    return true;
}

static void pr_finish(panel_req_t *p, const pr_io_t *io, int bad)
{
    if (bad < 0 && p->dirty) io->write(io->ctx, p->cur);
    p->dirty = false;
    p->step = 0;
    atomic_store(&p->result, (unsigned)(bad + 1) | (unsigned)p->cur << 8 | (unsigned)p->vals[0] << 16 |
                                 (unsigned)p->got << 24);
    atomic_store(&p->done, p->run);
}

static void pr_start(panel_req_t *p, const pr_io_t *io, unsigned req, int64_t now_ms)
{
    p->run = req >> 1;
    p->bad = 0;
    p->cur = p->got = 0;
    p->vals[0] = p->vals[1] = 0;
    if (io->read(io->ctx, &p->cur) != 0) {
        pr_finish(p, io, -1);
        return;
    }
    p->vals[0] = (uint8_t)(p->cur ^ (1 + (req & 1)));
    p->vals[1] = p->cur;
    p->dirty = true;
    if (io->write(io->ctx, p->vals[0]) != 0) {
        pr_finish(p, io, -1);
        return;
    }
    p->at_ms = now_ms;
    p->step = 1;
}

void pr_frame(panel_req_t *p, const pr_io_t *io, int64_t now_ms)
{
    if (p->step == 0) {
        int b = atomic_exchange(&p->bright, -1);
        if (b >= 0) io->write(io->ctx, (uint8_t)b);
        unsigned req = atomic_load(&p->req);
        if ((req >> 1) != 0 && (req >> 1) != p->run) pr_start(p, io, req, now_ms);
        return;
    }
    if (now_ms - p->at_ms < PR_SETTLE_MS) return;
    int k = p->step - 1;
    if (io->read(io->ctx, &p->got) != 0) {
        pr_finish(p, io, -1);
        return;
    }
    p->bad += p->got != p->vals[k];
    if (k == 1) {
        pr_finish(p, io, p->bad);
        return;
    }
    if (io->write(io->ctx, p->vals[1]) != 0) {
        pr_finish(p, io, -1);
        return;
    }
    p->dirty = false;
    p->at_ms = now_ms;
    p->step = 2;
}
