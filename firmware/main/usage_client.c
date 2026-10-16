#include "usage_client.h"

#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pomo_client.h"

static EXT_RAM_BSS_ATTR struct {
    SemaphoreHandle_t lock;
    usage_block_t blk;
    usage_clock_t clk;
    unsigned gen;
} S;

void usage_client_init(void)
{
    if (!S.lock) S.lock = xSemaphoreCreateMutex();
}

void usage_client_feed(const usage_block_t *b, bool has_utc_offset, int utc_offset)
{
    if (!S.lock) return;
    int n = b ? b->n : 0;
    usage_clock_t clk = {.has_utc_offset = has_utc_offset, .utc_offset = has_utc_offset ? utc_offset : 0};
    clk.have_srv = pomo_client_srv_offset(&clk.srv_offset);
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool moved = S.blk.n != n || (n && memcmp(S.blk.a, b->a, sizeof b->a[0] * (size_t)n) != 0) ||
                 S.clk.has_utc_offset != clk.has_utc_offset || S.clk.utc_offset != clk.utc_offset || S.clk.have_srv != clk.have_srv;
    if (moved) {
        if (n) S.blk = *b;
        else memset(&S.blk, 0, sizeof S.blk);
        S.gen++;
    }
    S.clk = clk;
    xSemaphoreGive(S.lock);
}

bool usage_client_take(unsigned *gen, usage_block_t *out, usage_clock_t *clk)
{
    if (!S.lock) return false;
    xSemaphoreTake(S.lock, portMAX_DELAY);
    bool moved = *gen != S.gen;
    if (moved) *out = S.blk;
    *clk = S.clk;
    *gen = S.gen;
    xSemaphoreGive(S.lock);
    return moved;
}
