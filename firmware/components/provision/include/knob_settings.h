#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KS_MAX_PAGES 8

typedef enum { KS_DIAG_OFF, KS_DIAG_BASIC, KS_DIAG_FULL } ks_diag_t;
#define KS_PAGE_ID_MAX 16

typedef struct {
    char id[KS_PAGE_ID_MAX + 1];
    bool on;
} ks_page_t;

typedef struct {
    bool follow_ember;
    uint8_t level;
    uint8_t floor;
    uint8_t startup;
    uint8_t n_pages;
    ks_page_t pages[KS_MAX_PAGES];
    char home[KS_PAGE_ID_MAX + 1];
    int poll_ms;
    int sleepy_after_s;
    int demo_hold_s;
    bool source_label;
    bool working_ring;
    bool fast_link;
    bool swipe_pages;
    uint8_t diagnostics;
    int stats_interval_s;
    int live_interval_s;
} knob_settings_t;

#define KS_STATS_INTERVAL_S_DEFAULT 60
#define KS_LIVE_INTERVAL_S_DEFAULT 5
#define KS_STATS_INTERVAL_S_MIN 30
#define KS_STATS_INTERVAL_S_MAX 300
#define KS_LIVE_INTERVAL_S_MIN 2
#define KS_LIVE_INTERVAL_S_MAX 10

void knob_settings_defaults(knob_settings_t *ks);

bool knob_settings_parse(const char *json, knob_settings_t *ks);

int knob_settings_page_order(const knob_settings_t *ks, const char *const *known, int n_known, int *out, int *home);

#ifdef __cplusplus
}
#endif
