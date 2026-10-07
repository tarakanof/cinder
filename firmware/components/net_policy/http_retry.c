#include "http_retry.h"

bool http_should_retry(bool reused, bool idempotent, http_fail_t why)
{
    if (!reused) return false;
    if (idempotent) return true;
    return why == HTTP_FAIL_OPEN || why == HTTP_FAIL_CLOSED;
}

bool http_should_retry_wait(bool reused, bool idempotent, bool waited, http_fail_t why)
{
    if (!waited) return http_should_retry(reused, idempotent, why);
    return reused && (why == HTTP_FAIL_OPEN || why == HTTP_FAIL_SEND || why == HTTP_FAIL_CLOSED);
}

const char *http_fail_name(http_fail_t why)
{
    static const char *const N[] = {"connect", "send", "closed", "timeout", "no status", "body"};
    return (unsigned)why < sizeof N / sizeof N[0] ? N[why] : "?";
}
