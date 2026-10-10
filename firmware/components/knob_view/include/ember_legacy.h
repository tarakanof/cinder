#pragma once

#include <stdbool.h>

#include "ember_host.h"
#include "link_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy /state: bot_mood_t, or -1 (host untouched) without a "render" object; sess is scratch, dangling after return. */
int ember_legacy_parse(const char *body, ember_host_session_t *sess, int max, ember_host_info_t *host);

/* GET /state answer (status as http_conn returns it): the mood of a whole 200, else -1; a truncated 200 is a link skip. */
int ember_state_answer(link_level_t level, int status, bool truncated, const char *body, ember_host_session_t *sess,
                       int max, ember_host_info_t *host, link_outcome_t *outcome);

#ifdef __cplusplus
}
#endif
