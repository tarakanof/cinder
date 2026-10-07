#include "coredump_up.h"

#include <stdio.h>
#include <string.h>

uint32_t cd_crc32(uint32_t crc, const void *p, size_t n)
{
    static const uint32_t T[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                   0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                   0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    const uint8_t *b = p;
    crc = ~crc;
    while (n--) {
        crc ^= *b++;
        crc = (crc >> 4) ^ T[crc & 15];
        crc = (crc >> 4) ^ T[crc & 15];
    }
    return ~crc;
}

void cd_id_format(uint32_t id, char out[CD_ID_LEN + 1]) { snprintf(out, CD_ID_LEN + 1, "%08lx", (unsigned long)id); }

bool cd_id_parse(const char *s, uint32_t *out)
{
    if (!s || strlen(s) != CD_ID_LEN) return false;
    uint32_t v = 0;
    for (int i = 0; i < CD_ID_LEN; i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) return false;
        v = v << 4 | (uint32_t)d;
    }
    *out = v;
    return true;
}

void cd_init(cd_state_t *s, bool has_dump, uint32_t id)
{
    memset(s, 0, sizeof *s);
    s->has_dump = has_dump;
    s->id = id;
}

bool cd_reply(cd_state_t *s, bool has_wanted, uint32_t wanted, bool has_ack, uint32_t ack)
{
    if (!s->has_dump) return false;
    if (has_ack && ack == s->id) {
        s->wanted = false;
        s->acked = true;
        return true;
    }
    s->wanted = !s->acked && has_wanted && wanted == s->id;
    return false;
}

bool cd_upload_due(const cd_state_t *s, int64_t now_ms, bool pomo_running)
{
    return s->has_dump && s->wanted && !pomo_running && now_ms >= s->next_ms;
}

bool cd_erase_due(const cd_state_t *s, int64_t now_ms, bool pomo_running)
{
    return s->has_dump && s->acked && !pomo_running && now_ms >= s->erase_next_ms;
}

bool cd_erase_failed(cd_state_t *s, int64_t now_ms)
{
    if (s->erase_attempts < 3) s->erase_attempts++;
    s->erase_next_ms = now_ms + cd_backoff_ms(s->erase_attempts);
    return s->erase_attempts == 1;
}

int cd_backoff_ms(int attempts)
{
    if (attempts <= 0) return 0;
    if (attempts == 1) return 60 * 1000;
    if (attempts == 2) return 5 * 60 * 1000;
    return 30 * 60 * 1000;
}

void cd_upload_done(cd_state_t *s, bool ok, int64_t now_ms)
{
    if (ok) s->wanted = false;
    if (s->attempts < 3) s->attempts++;
    s->next_ms = now_ms + cd_backoff_ms(s->attempts);
}

void cd_erased(cd_state_t *s) { cd_init(s, false, 0); }

void cd_check_init(cd_check_t *c, uint32_t id, uint32_t size)
{
    memset(c, 0, sizeof *c);
    c->id = id;
    c->size = size;
}

void cd_check_feed(cd_check_t *c, const uint8_t *p, size_t n)
{
    uint32_t body = c->size >= 4 ? c->size - 4 : 0;
    if (c->off < body) {
        size_t run = body - c->off < n ? body - c->off : n;
        c->crc = cd_crc32(c->crc, p, run);
        c->off += (uint32_t)run;
        p += run;
        n -= run;
    }
    for (; n && c->off < c->size; n--, c->off++) c->tail |= (uint32_t)*p++ << (8 * (c->off - body));
    if (n) c->off = c->size + 1;
}

bool cd_check_ok(const cd_check_t *c) { return c->size >= 4 && c->off == c->size && c->crc == c->id && c->tail == c->id; }
