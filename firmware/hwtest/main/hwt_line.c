#include "hwt_line.h"

#include <string.h>

#include "cJSON.h"

static const char *const OPS[] = {"info", "pins", "probe", "levels", "scan", "room", "watch", "tone", "radar", "stop"};
static const char *const PINS[HWT_PIN_N] = {"sda", "scl", "bclk", "lrc", "din", "radar_rx"};

const char *hwt_pin_name(hwt_pin_t p) { return p < HWT_PIN_N ? PINS[p] : "?"; }

static bool num(const cJSON *o, const char *key, int lo, int hi, int *out, const char **error)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!v) return true;
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= lo && v->valuedouble <= hi) || v->valuedouble != (double)(int)v->valuedouble) {
        *error = key;
        return false;
    }
    *out = v->valueint;
    return true;
}

bool hwt_parse(const char *line, hwt_req_t *out, const char **error)
{
    *error = NULL;
    *out = (hwt_req_t){.id = 0, .gpio = HWT_UNSET, .level = HWT_UNSET, .ms = HWT_UNSET, .hz = HWT_UNSET, .vol = HWT_UNSET, .s = HWT_UNSET,
                       .radar_bit = HWT_UNSET, .pcf = HWT_UNSET, .room = HWT_ROOM_GET};
    for (int i = 0; i < HWT_PIN_N; i++) out->pin[i] = HWT_UNSET;
    size_t pl = strlen(HWT_PREFIX);
    if (strncmp(line, HWT_PREFIX, pl) != 0) return *error = "prefix", false;
    if (strlen(line) > HWT_LINE_MAX) return *error = "too_long", false;
    cJSON *o = cJSON_Parse(line + pl);
    if (!cJSON_IsObject(o)) {
        cJSON_Delete(o);
        return *error = "json", false;
    }
    bool ok = num(o, "id", 0, 1 << 30, &out->id, error);
    const cJSON *op = cJSON_GetObjectItemCaseSensitive(o, "op");
    int found = -1;
    for (int i = 0; ok && cJSON_IsString(op) && i < (int)(sizeof OPS / sizeof OPS[0]); i++)
        if (!strcmp(op->valuestring, OPS[i])) found = i;
    if (ok && found < 0) ok = false, *error = "op";
    out->op = (hwt_op_t)(found < 0 ? 0 : found);
    ok = ok && num(o, "gpio", -1, 48, &out->gpio, error) && num(o, "level", 0, 1, &out->level, error) &&
         num(o, "ms", 0, 600000, &out->ms, error) && num(o, "hz", 20, 8000, &out->hz, error) && num(o, "vol", 0, 100, &out->vol, error) &&
         num(o, "s", 1, 3600, &out->s, error) && num(o, "radar_bit", 0, 7, &out->radar_bit, error) && num(o, "pcf", 0, 0x3F, &out->pcf, error);
    for (int i = 0; ok && i < HWT_PIN_N; i++) ok = num(o, PINS[i], -1, 48, &out->pin[i], error);
    const cJSON *room = cJSON_GetObjectItemCaseSensitive(o, "do");
    if (ok && room) {
        if (cJSON_IsString(room) && !strcmp(room->valuestring, "start"))
            out->room = HWT_ROOM_START;
        else if (cJSON_IsString(room) && !strcmp(room->valuestring, "stop"))
            out->room = HWT_ROOM_STOP;
        else if (!cJSON_IsString(room) || strcmp(room->valuestring, "get"))
            ok = false, *error = "do";
    }
    cJSON_Delete(o);
    return ok;
}
