#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HTTP_FAIL_OPEN,
    HTTP_FAIL_SEND,
    HTTP_FAIL_CLOSED,
    HTTP_FAIL_TIMEOUT,
    HTTP_FAIL_NO_STATUS,
    HTTP_FAIL_BODY,
} http_fail_t;

bool http_should_retry(bool reused, bool idempotent, http_fail_t why);
bool http_should_retry_wait(bool reused, bool idempotent, bool waited, http_fail_t why);

const char *http_fail_name(http_fail_t why);

#ifdef __cplusplus
}
#endif
