#pragma once

#include <stdbool.h>
#include <stddef.h>

#define HWT_PREFIX "HWT1 "
#define HWT_LINE_MAX 512
#define HWT_UNSET (-1000)

typedef enum {
    HWT_INFO,
    HWT_PINS,
    HWT_PROBE,
    HWT_LEVELS,
    HWT_SCAN,
    HWT_ROOM,
    HWT_WATCH,
    HWT_TONE,
    HWT_RADAR,
    HWT_STOP,
} hwt_op_t;

typedef enum { HWT_ROOM_GET, HWT_ROOM_START, HWT_ROOM_STOP } hwt_room_t;

typedef enum { HWT_PIN_SDA, HWT_PIN_SCL, HWT_PIN_BCLK, HWT_PIN_LRC, HWT_PIN_DIN, HWT_PIN_RADAR_RX, HWT_PIN_N } hwt_pin_t;

/* Fields the request did not carry are HWT_UNSET. */
typedef struct {
    int id;
    hwt_op_t op;
    int gpio;
    int level;
    int ms;
    int hz;
    int vol;
    int s;
    int pin[HWT_PIN_N];
    int radar_bit;
    int pcf;
    hwt_room_t room;
} hwt_req_t;

/* line is one input line without the newline; NULL error on success. */
bool hwt_parse(const char *line, hwt_req_t *out, const char **error);
const char *hwt_pin_name(hwt_pin_t p);
