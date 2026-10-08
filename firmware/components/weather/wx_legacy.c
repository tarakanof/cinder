#include "wx_legacy.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static char *array_end(char *p)
{
    int depth = 1;
    bool in_str = false;
    for (; *p; p++) {
        if (in_str) {
            if (*p == '\\') {
                if (!p[1]) return NULL;
                p++;
            } else if (*p == '"') {
                in_str = false;
            }
        } else if (*p == '"') {
            in_str = true;
        } else if (*p == '[') {
            depth++;
        } else if (*p == ']' && --depth == 0) {
            return p;
        }
    }
    return NULL;
}

void wx_legacy_drop_hourly(char *body)
{
    static const char key[] = "\"hourly\":[";
    bool in_str = false;
    for (char *p = body; *p; p++) {
        if (in_str) {
            if (*p == '\\') {
                if (!p[1]) return;
                p++;
            } else if (*p == '"') {
                in_str = false;
            }
        } else if (*p == '"') {
            if (strncmp(p, key, sizeof key - 1) != 0) {
                in_str = true;
                continue;
            }
            char *open = p + sizeof key - 1, *close = array_end(open);
            if (!close) return;
            memmove(open, close, strlen(close) + 1);
            p = open;
        }
    }
}

static void copy_str(char *dst, size_t cap, const cJSON *item)
{
    dst[0] = 0;
    if (cJSON_IsString(item) && item->valuestring) snprintf(dst, cap, "%s", item->valuestring);
}

static int minute_of(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? wx_minute_of_day(v->valuestring) : -1;
}

bool wx_legacy_parse(const char *body, wx_obs_t *o)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) return false;
    memset(o, 0, sizeof *o);
    o->enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "enabled"));
    copy_str(o->provider, sizeof o->provider, cJSON_GetObjectItemCaseSensitive(root, "provider"));
    o->now_min = minute_of(root, "generated_at");
    o->rise_min = o->set_min = -1;
    const cJSON *sun = cJSON_GetObjectItemCaseSensitive(root, "sun");
    if (cJSON_IsObject(sun)) {
        o->rise_min = minute_of(sun, "sunrise");
        o->set_min = minute_of(sun, "sunset");
    }
    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (cJSON_IsObject(cur)) {
        o->valid = true;
        o->stale = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cur, "stale"));
        o->severe = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cur, "severe"));
        copy_str(o->condition, sizeof o->condition, cJSON_GetObjectItemCaseSensitive(cur, "condition"));
        copy_str(o->code, sizeof o->code, cJSON_GetObjectItemCaseSensitive(cur, "condition_code"));
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(cur, "temp_c");
        if (cJSON_IsNumber(t)) {
            o->has_temp = true;
            o->temp_c = (float)t->valuedouble;
        }
    }
    cJSON_Delete(root);
    return true;
}
