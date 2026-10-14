#include "join_label.h"

#include <stdio.h>

join_label_t join_label_pick(const join_in_t *in)
{
    if (in->setup) return JOIN_NONE;
    if (!in->online && in->join_failures >= JOIN_FAILS_SHOWN) return JOIN_CANT_JOIN;
    if (in->unauthorized || (in->has_url && !in->has_device)) return JOIN_NOT_PAIRED;
    if (in->update_knob) return JOIN_UPDATE_KNOB;
    if (in->update_ember) return JOIN_UPDATE_EMBER;
    return JOIN_NONE;
}

void join_label_text(join_label_t l, const char *ssid, char *out, size_t n)
{
    switch (l) {
    case JOIN_CANT_JOIN: snprintf(out, n, "Can't join\n%s", ssid); break;
    case JOIN_NOT_PAIRED: snprintf(out, n, "Not paired"); break;
    case JOIN_UPDATE_KNOB: snprintf(out, n, "Update knob"); break;
    case JOIN_UPDATE_EMBER: snprintf(out, n, "Update Ember"); break;
    default: snprintf(out, n, "%s", ""); break;
    }
}

bool join_glyph_shown(bool offline, join_label_t l) { return offline && l != JOIN_CANT_JOIN; }
