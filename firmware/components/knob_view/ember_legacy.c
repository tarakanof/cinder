#include "ember_legacy.h"

#include "cJSON.h"
#include "knob_view.h"

int ember_legacy_parse(const char *body, ember_host_session_t *sess, int max, ember_host_info_t *host)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) return -1;
    int mood = -1;
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "render");
    if (cJSON_IsObject(r)) {
        const cJSON *w = cJSON_GetObjectItemCaseSensitive(r, "waiting");
        const cJSON *e = cJSON_GetObjectItemCaseSensitive(r, "errors");
        const cJSON *ru = cJSON_GetObjectItemCaseSensitive(r, "running");
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(r, "done");
        int waiting = cJSON_IsNumber(w) ? w->valueint : 0, errors = cJSON_IsNumber(e) ? e->valueint : 0;
        int running = cJSON_IsNumber(ru) ? ru->valueint : 0, done = cJSON_IsNumber(d) ? d->valueint : 0;
        mood = knob_view_mood_from(waiting, errors, running, done);

        const cJSON *src = cJSON_GetObjectItemCaseSensitive(r, "source");
        int n = 0;
        if (!cJSON_IsString(src)) {
            const cJSON *it;
            cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "sessions"))
            {
                if (n == max) break;
                sess[n++] = (ember_host_session_t){
                    .source = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "source")),
                    .state = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "state")),
                    .updated_at = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "updated_at")),
                };
            }
        }
        *host = (ember_host_info_t){.color = -1};
        ember_host_label(cJSON_IsString(src) ? src->valuestring : NULL, cJSON_IsString(src), sess, n, host->text,
                         sizeof host->text);
    }
    cJSON_Delete(root);
    return mood;
}
