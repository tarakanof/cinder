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

static bool code_ok(const char *s)
{
    if (!s) return false;
    int i = 0;
    for (; s[i]; i++)
        if (i >= 6 || s[i] < '0' || s[i] > '9') return false;
    return i == 6;
}

void join_label_text(join_label_t l, const char *detail, char *out, size_t n)
{
    switch (l) {
    case JOIN_CANT_JOIN: snprintf(out, n, "Can't join\n%s", detail ? detail : ""); break;
    case JOIN_NOT_PAIRED:
        if (code_ok(detail)) snprintf(out, n, "Not paired\nCode %.3s %.3s", detail, detail + 3);
        else snprintf(out, n, "Not paired");
        break;
    case JOIN_UPDATE_KNOB: snprintf(out, n, "Update knob"); break;
    case JOIN_UPDATE_EMBER: snprintf(out, n, "Update Ember"); break;
    default: snprintf(out, n, "%s", ""); break;
    }
}

bool join_glyph_shown(bool offline, join_label_t l) { return offline && l != JOIN_CANT_JOIN; }
