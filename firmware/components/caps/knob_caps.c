#include "knob_caps.h"

#include "cfg.h"
#include "knob_rotation.h"
#include "knob_view.h"

static const char *const FEATURES[] = {"view_wait", "np_control", "ota_rollback", "coredump", "stats_intervals"};

void knob_caps(dev_caps_t *c, const char *ids[PAGES_N])
{
    *c = (dev_caps_t){
        .view_min = KNOB_VIEW_V_MIN,
        .view_max = KNOB_VIEW_V_MAX,
        .pages = ids,
        .n_pages = pages_ids(ids, PAGES_N),
        .features = FEATURES,
        .n_features = (int)(sizeof FEATURES / sizeof FEATURES[0]),
        .view_bytes = KNOB_VIEW_BUF - 1,
        .config_bytes = CFG_SETTINGS_MAX,
        .rotations = KR_SUPPORTED,
        .n_rotations = KR_N_SUPPORTED,
    };
}
