#pragma once
#include <stdint.h>
typedef enum { LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST } lv_event_code_t;
typedef struct { lv_event_code_t code; } lv_event_t;
typedef struct { int32_t x, y; } lv_point_t;
typedef struct lv_indev lv_indev_t;
lv_event_code_t lv_event_get_code(lv_event_t *e);
lv_indev_t *lv_indev_active(void);
void lv_indev_get_point(const lv_indev_t *indev, lv_point_t *point);
