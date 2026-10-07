#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIEW_REPROBE_OLD_MS (10 * 60 * 1000)
#define VIEW_REPROBE_MS 60000
#define VIEW_SERVER_ERRORS 3

typedef enum {
    VIEW_WHY_OK,
    VIEW_WHY_NO_DEVICE,
    VIEW_WHY_OLD_SERVER,
    VIEW_WHY_REJECTED,
    VIEW_WHY_SERVER_ERROR,
} view_why_t;

typedef struct {
    bool has_device;
    bool legacy;
    view_why_t why;
    int64_t probe_ms;
    int server_errors;
} view_policy_t;

void view_policy_init(view_policy_t *p, bool has_device);
bool view_policy_try_view(const view_policy_t *p, int64_t now_ms);
bool view_policy_result(view_policy_t *p, int status, int64_t now_ms);
void view_policy_device_changed(view_policy_t *p, bool has_device, int64_t now_ms);
const char *view_why_name(view_why_t w);

typedef enum {
    VIEW_ANS_NEW,
    VIEW_ANS_SAME,
    VIEW_ANS_REFETCH,
    VIEW_ANS_FAILED,
} view_answer_t;
view_answer_t view_answer(int status, bool have_view);

#define VIEW_ETAG_MAX 72
typedef struct {
    char tag[VIEW_ETAG_MAX];
} view_etag_t;

void view_etag_clear(view_etag_t *e);
bool view_etag_set(view_etag_t *e, const char *hdr);
const char *view_etag_get(const view_etag_t *e);

bool view_parse_now(const char *hdr, long long *out);

#ifdef __cplusplus
}
#endif
