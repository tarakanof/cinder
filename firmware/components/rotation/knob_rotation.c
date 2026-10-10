#include "knob_rotation.h"

const int KR_SUPPORTED[KR_N_SUPPORTED] = {0, 180};

#define VIS_X1 (KR_FRAME_W - 1)
#define VIS_Y1 (KR_FRAME_H - 1)

bool kr_valid(int deg) { return deg == 0 || deg == 90 || deg == 180 || deg == 270; }

int kr_effective(int deg)
{
    for (int i = 0; i < KR_N_SUPPORTED; i++)
        if (KR_SUPPORTED[i] == deg) return deg;
    return 0;
}

kr_panel_t kr_panel(int deg)
{
    if (kr_effective(deg) != 180) return (kr_panel_t){0};
    return (kr_panel_t){
        .mirror_x = true,
        .mirror_y = true,
        .gap_x = KR_GRAM - 1 - (KR_VIS_X0 + VIS_X1),
        .gap_y = KR_GRAM - 1 - VIS_Y1,
    };
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void kr_touch(int deg, int *x, int *y)
{
    if (kr_effective(deg) == 180) {
        *x = KR_VIS_X0 + VIS_X1 - *x;
        *y = VIS_Y1 - *y;
    }
    *x = clampi(*x, 0, KR_FRAME_W - 1);
    *y = clampi(*y, 0, KR_FRAME_H - 1);
}
