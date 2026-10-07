#include "improv.h"

#include <string.h>

uint8_t improv_checksum(const uint8_t *b, size_t n)
{
    uint32_t s = 0;
    for (size_t i = 0; i < n; i++) s += b[i];
    return (uint8_t)s;
}

size_t improv_frame(uint8_t type, const uint8_t *data, size_t len, uint8_t *out, size_t cap)
{
    size_t total = IMPROV_PREFIX_LEN + len + 2;
    if (len > 255 || total > cap) return 0;
    memcpy(out, IMPROV_HEADER, IMPROV_HEADER_LEN);
    out[6] = IMPROV_VERSION;
    out[7] = type;
    out[8] = (uint8_t)len;
    if (len) memcpy(out + IMPROV_PREFIX_LEN, data, len);
    out[IMPROV_PREFIX_LEN + len] = improv_checksum(out, IMPROV_PREFIX_LEN + len);
    out[IMPROV_PREFIX_LEN + len + 1] = '\n';
    return total;
}

size_t improv_state(uint8_t state, uint8_t *out, size_t cap) { return improv_frame(IMPROV_TYPE_STATE, &state, 1, out, cap); }
size_t improv_error(uint8_t err, uint8_t *out, size_t cap) { return improv_frame(IMPROV_TYPE_ERROR, &err, 1, out, cap); }

size_t improv_result(uint8_t cmd, const char *const *strs, size_t n, uint8_t *out, size_t cap)
{
    uint8_t data[255];
    size_t p = 2;
    for (size_t i = 0; i < n; i++) {
        size_t l = strlen(strs[i]);
        if (l > 255 || p + 1 + l > sizeof data) return 0;
        data[p++] = (uint8_t)l;
        memcpy(data + p, strs[i], l);
        p += l;
    }
    data[0] = cmd;
    data[1] = (uint8_t)(p - 2);
    return improv_frame(IMPROV_TYPE_RESULT, data, p, out, cap);
}

improv_err_t improv_parse(const uint8_t *f, size_t len, improv_pkt_t *out)
{
    if (len < IMPROV_HEADER_LEN || memcmp(f, IMPROV_HEADER, IMPROV_HEADER_LEN) != 0) return IMPROV_E_HEADER;
    if (len < IMPROV_PREFIX_LEN + 1) return IMPROV_E_SHORT;
    if (f[6] != IMPROV_VERSION) return IMPROV_E_VERSION;
    size_t want = IMPROV_PREFIX_LEN + f[8] + 1;
    if (len < want) return IMPROV_E_SHORT;
    if (len > want) return IMPROV_E_LENGTH;
    if (improv_checksum(f, len - 1) != f[len - 1]) return IMPROV_E_CHECKSUM;
    out->type = f[7];
    out->len = f[8];
    out->data = f + IMPROV_PREFIX_LEN;
    return IMPROV_OK;
}

improv_err_t improv_parse_rpc(const improv_pkt_t *pkt, improv_rpc_t *out)
{
    memset(out, 0, sizeof *out);
    if (pkt->len < 2 || pkt->data[1] != pkt->len - 2) return IMPROV_E_STRINGS;
    out->cmd = pkt->data[0];
    const uint8_t *p = pkt->data + 2, *end = pkt->data + pkt->len;
    while (p < end) {
        uint8_t l = *p++;
        if (l > end - p) return IMPROV_E_STRINGS;
        if (out->n < IMPROV_RPC_MAX_STRINGS) {
            out->str[out->n] = p;
            out->len[out->n] = l;
            out->n++;
        }
        p += l;
    }
    return IMPROV_OK;
}

bool improv_rpc_string(const improv_rpc_t *rpc, int i, char *dst, size_t cap)
{
    if (i < 0 || i >= rpc->n || rpc->len[i] >= cap) return false;
    if (memchr(rpc->str[i], 0, rpc->len[i])) return false;
    memcpy(dst, rpc->str[i], rpc->len[i]);
    dst[rpc->len[i]] = 0;
    return true;
}

enum { M_TEXT, M_FRAME, M_SKIP };

void prov_rx_init(prov_rx_t *rx, uint8_t *buf, size_t cap)
{
    memset(rx, 0, sizeof *rx);
    rx->buf = buf;
    rx->cap = cap;
}

static bool is_cinder(const uint8_t *b, size_t n)
{
    return n >= PROV_LINE_PREFIX_LEN && memcmp(b, PROV_LINE_PREFIX, PROV_LINE_PREFIX_LEN) == 0;
}

prov_rx_ev_t prov_rx_feed(prov_rx_t *rx, uint8_t c, const uint8_t **out, size_t *out_len)
{
    if (rx->mode == M_FRAME) {
        rx->buf[rx->len++] = c;
        if (rx->len >= IMPROV_PREFIX_LEN && rx->len == (size_t)IMPROV_PREFIX_LEN + rx->buf[8] + 1) {
            rx->mode = M_TEXT;
            *out = rx->buf;
            *out_len = rx->len;
            rx->len = 0;
            return PROV_RX_IMPROV;
        }
        return PROV_RX_NONE;
    }

    if (rx->mode == M_TEXT && rx->len < IMPROV_HEADER_LEN && c == (uint8_t)IMPROV_HEADER[rx->len] &&
        memcmp(rx->buf, IMPROV_HEADER, rx->len) == 0 && rx->len + 1 == IMPROV_HEADER_LEN) {
        memcpy(rx->buf, IMPROV_HEADER, IMPROV_HEADER_LEN);
        rx->len = IMPROV_HEADER_LEN;
        rx->mode = M_FRAME;
        return PROV_RX_NONE;
    }
    if (c == '\n') {
        prov_rx_ev_t ev = PROV_RX_NONE;
        if (rx->mode == M_SKIP) {
            if (rx->cinder) ev = PROV_RX_LINE_TOO_LONG;
        } else {
            size_t n = rx->len;
            if (n && rx->buf[n - 1] == '\r') n--;
            if (is_cinder(rx->buf, n)) {
                rx->buf[n] = 0;
                *out = rx->buf + PROV_LINE_PREFIX_LEN;
                *out_len = n - PROV_LINE_PREFIX_LEN;
                ev = PROV_RX_LINE;
            }
        }
        rx->mode = M_TEXT;
        rx->len = 0;
        rx->cinder = false;
        return ev;
    }
    if (rx->mode == M_SKIP) return PROV_RX_NONE;
    if (rx->len + 1 >= rx->cap) {
        rx->cinder = is_cinder(rx->buf, rx->len);
        rx->mode = M_SKIP;
        rx->len = 0;
        return PROV_RX_NONE;
    }
    rx->buf[rx->len++] = c;
    return PROV_RX_NONE;
}

void prov_rx_idle(prov_rx_t *rx)
{
    if (rx->mode != M_FRAME) return;
    rx->mode = M_TEXT;
    rx->len = 0;
}

bool improv_join_failure_reason(int r)
{
    switch (r) {
    case 2:
    case 15:
    case 201:
    case 202:
    case 204:
    case 210:
    case 211:
        return true;
    default:
        return false;
    }
}

void improv_join_reset(improv_join_t *j) { j->strong = j->weak = 0; }

void improv_join_note(improv_join_t *j, int reason, int rssi)
{
    if (!improv_join_failure_reason(reason)) return;
    bool eapol = reason == 2 || reason == 15 || reason == 204;
    if (eapol && rssi != 0 && rssi < IMPROV_WEAK_RSSI) j->weak++;
    else j->strong++;
}

int improv_join_failures(const improv_join_t *j) { return j->strong + j->weak / IMPROV_WEAK_PER_FAIL; }
