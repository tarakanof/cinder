#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JOIN_FAILS_SHOWN 3

typedef enum { JOIN_NONE, JOIN_CANT_JOIN, JOIN_NOT_PAIRED, JOIN_UPDATE_KNOB, JOIN_UPDATE_EMBER } join_label_t;

typedef struct {
    bool setup;
    bool online;
    int join_failures;
    bool unauthorized;
    bool has_url;
    bool has_device;
    bool update_knob;
    bool update_ember;
} join_in_t;

join_label_t join_label_pick(const join_in_t *in);
/* Writes the label's text into out (empty for JOIN_NONE). detail: the SSID for JOIN_CANT_JOIN, a 6-digit pairing code for JOIN_NOT_PAIRED (anything else shows no code). */
void join_label_text(join_label_t l, const char *detail, char *out, size_t n);
bool join_glyph_shown(bool offline, join_label_t l);

#ifdef __cplusplus
}
#endif
