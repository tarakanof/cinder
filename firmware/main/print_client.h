/* The print block and K2's baked pictures; the picture pair goes to the LVGL task, which owns and frees it. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "http_conn.h"
#include "print.h"

typedef struct {
    bool fed;              /* a view was applied since boot */
    bool has;              /* the last view carried a print block */
    print_block_t b;
    bool have_srv, has_utc_offset;
    double srv_offset;     /* server Unix s = pomo_client_now() + srv_offset */
    int utc_offset;        /* s, Ember's zone: the day block's, else the weather block's */
} print_snapshot_t;

/* ghost and solid: PRINT_PIC_W x (bottom - top) RGB565, rows top..bottom of the 466 px picture. */
typedef struct {
    char version[PRINT_THUMBV_MAX + 1];
    int top, bottom;
    uint16_t *ghost, *solid;
} print_pics_t;

#define PRINT_JPEG_MAX (96 * 1024)

/* Once, before the ember task starts. */
void print_client_init(void);
/* Ember task, every applied view: b NULL when the view has no print block. */
void print_client_feed(const print_block_t *b, bool has_utc_offset, int utc_offset);
/* Any task. */
void print_client_get(print_snapshot_t *out);
/* LVGL task: pictures are fetched only while the page shows. */
void print_client_set_visible(bool on);
/* Ember task: a picture pair is due now. */
bool print_client_pending(void);
/* Ember task. base: Ember's URL. */
void print_client_service(http_conn_t *conn, const char *base);
/* LVGL task: the newest finished pair, if any; the caller frees it with print_pics_free. */
bool print_client_take_pics(print_pics_t *out);
void print_pics_free(print_pics_t *p);
/* LVGL task: the view freed its pair, so the same version is fetched again when the page shows. */
void print_client_pics_dropped(void);
