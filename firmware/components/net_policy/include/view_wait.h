#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIEW_WAIT_HEADER "X-Ember-View-Wait"
#define VIEW_WAIT_MAX_S 25
#define VIEW_WAIT_MIN_S 2
#define VIEW_WAIT_SLACK_MS 10000
#define VIEW_WAIT_FAST_MS 1000
#define VIEW_REARM_MIN_MS 200

int view_wait_cap(const char *hdr);

int view_wait_s(int cap_s, bool have_etag, int64_t until_checkin_ms, bool failing);

bool view_wait_clock_sample(int waited_s, int64_t elapsed_ms);

int view_wait_budget_ms(int wait_s);

int view_rearm_ms(int status, int waited_s, int64_t elapsed_ms, int poll_ms);

#ifdef __cplusplus
}
#endif
