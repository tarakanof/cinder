#include "pomo_legacy.h"

#include "cJSON.h"

bool pomo_legacy_parse(const char *body, pomo_state_t *out)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) return false;
    const cJSON *phase = cJSON_GetObjectItemCaseSensitive(root, "phase");
    bool ok = cJSON_IsString(phase);
    if (ok) {
        const cJSON *rem = cJSON_GetObjectItemCaseSensitive(root, "remaining_sec");
        const cJSON *plan = cJSON_GetObjectItemCaseSensitive(root, "planned_sec");
        const cJSON *round = cJSON_GetObjectItemCaseSensitive(root, "round");
        *out = (pomo_state_t){
            .phase = pomo_phase_from_wire(phase->valuestring),
            .running = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "running")),
            .paused = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "paused")),
            .remaining_sec = cJSON_IsNumber(rem) ? rem->valueint : 0,
            .planned_sec = cJSON_IsNumber(plan) ? plan->valueint : 0,
            .round = cJSON_IsNumber(round) ? round->valueint : 0,
        };
    }
    cJSON_Delete(root);
    return ok;
}
