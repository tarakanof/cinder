#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
/* Screen px; degrees: 0 = 3 o'clock, clockwise (y down). */
    float x, y;
    float rot_deg;
} arc_item_t;

void arc_text_layout(float cx, float cy, float r, const float *w, int n, arc_item_t *out);

/* One byte per row, bit 7 = left column; tool is ember_tool_t, 0 returned for no icon. */
int arc_tool_icon(int tool, uint8_t body[8], uint8_t feature[8]);

#ifdef __cplusplus
}
#endif
