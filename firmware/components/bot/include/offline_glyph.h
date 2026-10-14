#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OFFLINE_GLYPH_W 62
#define OFFLINE_GLYPH_H 52

/* a8: OFFLINE_GLYPH_W x OFFLINE_GLYPH_H coverage, stride OFFLINE_GLYPH_W. */
void offline_glyph_raster(uint8_t *a8);

#ifdef __cplusplus
}
#endif
