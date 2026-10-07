#include "arc_text.h"

#include <math.h>
#include <string.h>

void arc_text_layout(float cx, float cy, float r, const float *w, int n, arc_item_t *out)
{
    float total = 0;
    for (int i = 0; i < n; i++) total += w[i];
    const float k = (float)(180.0 / M_PI) / r;
    float start = 90.0f + total / 2 * k;
    float s = 0;
    for (int i = 0; i < n; i++) {
        float th = start - (s + w[i] / 2) * k;
        float a = th * (float)(M_PI / 180.0);
        out[i].x = cx + r * cosf(a);
        out[i].y = cy + r * sinf(a);
        out[i].rot_deg = th - 90.0f;
        s += w[i];
    }
}

static void rows(const char *const src[8], uint8_t out[8])
{
    for (int y = 0; y < 8; y++) {
        out[y] = 0;
        for (int x = 0; x < 8; x++)
            if (src[y][x] == 'X') out[y] |= (uint8_t)(0x80 >> x);
    }
}

int arc_tool_icon(int tool, uint8_t body[8], uint8_t feature[8])
{
    static const char *const t3[8] = {"........", "XXX.XXX.", ".X....X.", ".X...XX.",
                                      ".X....X.", ".X..XXX.", "........", "........"};
    static const char *const t3_three[8] = {"........", "....XXX.", "......X.", ".....XX.",
                                            "......X.", "....XXX.", "........", "........"};
    switch (tool) {
    case 3: rows(t3, body); rows(t3_three, feature); return 1;
    default:
        memset(body, 0, 8);
        memset(feature, 0, 8);
        return 0;
    }
}
