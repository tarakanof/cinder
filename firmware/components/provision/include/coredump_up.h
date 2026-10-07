#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CD_CHUNK 4096
#define CD_ID_LEN 8

/* zlib crc32(): pass 0 to start, the previous result to continue. */
uint32_t cd_crc32(uint32_t crc, const void *p, size_t n);

void cd_id_format(uint32_t id, char out[CD_ID_LEN + 1]);
/* Exactly 8 lower-case hex digits. */
bool cd_id_parse(const char *s, uint32_t *out);

/* Times in ms on the caller's monotonic clock. */
typedef struct {
    bool has_dump;
    uint32_t id;
    bool wanted;
    int attempts;
    int64_t next_ms;
    bool acked;
    int erase_attempts;
    int64_t erase_next_ms;
} cd_state_t;

void cd_init(cd_state_t *s, bool has_dump, uint32_t id);
/* One checkin reply; true: this reply acks the current dump (the ack sticks until cd_erased). */
bool cd_reply(cd_state_t *s, bool has_wanted, uint32_t wanted, bool has_ack, uint32_t ack);
bool cd_erase_due(const cd_state_t *s, int64_t now_ms, bool pomo_running);
/* True on the first failure of a streak. */
bool cd_erase_failed(cd_state_t *s, int64_t now_ms);
bool cd_upload_due(const cd_state_t *s, int64_t now_ms, bool pomo_running);
void cd_upload_done(cd_state_t *s, bool ok, int64_t now_ms);
int cd_backoff_ms(int attempts);
void cd_erased(cd_state_t *s);

/* Upload self-check: the body is a flash dump image, its last 4 bytes the IDF CRC-32 (LE) of the rest. */
typedef struct {
    uint32_t id, size, off, crc, tail;
} cd_check_t;
void cd_check_init(cd_check_t *c, uint32_t id, uint32_t size);
void cd_check_feed(cd_check_t *c, const uint8_t *p, size_t n);
bool cd_check_ok(const cd_check_t *c);

#ifdef __cplusplus
}
#endif
