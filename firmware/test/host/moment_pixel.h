#pragma once

void wxm_test_pixel(const void *layer, int y);
#define WXM_PIXEL(im, y) wxm_test_pixel((im)->a, (y))
