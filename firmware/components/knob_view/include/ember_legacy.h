#pragma once

#include "ember_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy /state: bot_mood_t, or -1 (host untouched) without a "render" object; sess is scratch, dangling after return. */
int ember_legacy_parse(const char *body, ember_host_session_t *sess, int max, ember_host_info_t *host);

#ifdef __cplusplus
}
#endif
