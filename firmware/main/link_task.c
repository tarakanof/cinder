#include "link_task.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "bsp_knob_15_md50et.h"
#include "config_store.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_client.h"
#include "panel_check.h"

static const char *TAG = "cinder";

#define LINK_FALLBACK 1
#define LINK_CHECK_WAIT_MS 1000

static void link_reboot(const char *why)
{
    ESP_LOGE(TAG, "display link: %s", why);
    vTaskDelay(pdMS_TO_TICKS(200));
}

static bool link_confirm_fail(int seed)
{
    int fails = 0;
    for (int k = 0; k < 3; k++) {
        uint8_t raw[32];
        int bad = panel_check_run(seed + 1000 + k, raw, LINK_CHECK_WAIT_MS);
        ESP_LOGW(TAG, "link recheck %d: %d (level %02X wrote %02X read %02X)", k, bad, raw[0], raw[1], raw[2]);
        fails += bad > 0;
    }
    return fails >= 2;
}

static ota_reboot_gate_t s_reboot_gate;

static void link_reboot_run(ota_reboot_kind_t k)
{
    if (k == OTA_REBOOT_FALLBACK) bsp_knob_15_md50et_qspi_fallback_reboot();
    else if (k == OTA_REBOOT_RESTART) esp_restart();
}

static void link_reboot_request(ota_reboot_kind_t k, const char *why)
{
    link_reboot(why);
    ota_reboot_kind_t now = ota_reboot_request(&s_reboot_gate, k, ota_client_verifying());
    if (now == OTA_REBOOT_NONE) ESP_LOGW(TAG, "display link: reboot held until the new firmware is marked valid");
    link_reboot_run(now);
}

static void link_task(void *arg)
{
    (void)arg;
    static EXT_RAM_BSS_ATTR knob_settings_t ks;
    vTaskDelay(pdMS_TO_TICKS(3000));
    int fast = bsp_knob_15_md50et_qspi_hz() > 40 * 1000 * 1000;
    if (bsp_knob_15_md50et_qspi_fallback_active()) ESP_LOGW(TAG, "display link: 40 MHz (fallback after a failed check)");
    for (int i = 0; i < 5; i++) {
        uint8_t raw[32];
        int bad = panel_check_run(i, raw, LINK_CHECK_WAIT_MS);
        ESP_LOGW(TAG, "link check %d at %d MHz: %d bad | %02X %02X %02X %02X %02X %02X %02X %02X", i,
                 bsp_knob_15_md50et_qspi_hz() / 1000000, bad, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                 raw[6], raw[7]);
        if (bad < 0) continue;
        bool failed = bad > 0 && link_confirm_fail(i);
        ota_client_note_link(ota_link_result(failed, fast));
        if (failed && fast && LINK_FALLBACK) {
            link_reboot_request(OTA_REBOOT_FALLBACK, "check failed at boot: rebooting at 40 MHz");
        }
    }
    uint32_t runs = 0, fails = 0, errs = 0;
    int streak = 0;
    for (int seed = 100;; seed++) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        link_reboot_run(ota_reboot_release(&s_reboot_gate, ota_client_verifying()));
        config_store_settings(&ks);
        bool want = ks.fast_link && !bsp_knob_15_md50et_qspi_fallback_active();
        if (want != (bool)fast && s_reboot_gate.held == OTA_REBOOT_NONE)
            link_reboot_request(OTA_REBOOT_RESTART,
                                want ? "fast link turned on: rebooting at 80 MHz" : "fast link turned off: rebooting at 40 MHz");
        int bad = panel_check_run(seed, NULL, LINK_CHECK_WAIT_MS);
        runs++;
        if (bad < 0) errs++;
        else if (bad > 0) fails++;
        if (bad > 0 && !link_confirm_fail(seed)) bad = 0;
        streak = bad > 0 ? streak + 1 : 0;
        if (bad >= 0) ota_client_note_link(ota_link_result(streak >= 2, fast));
        if (streak >= 2 && fast && LINK_FALLBACK) {
            link_reboot_request(OTA_REBOOT_FALLBACK, "two checks failed: rebooting at 40 MHz");
            streak = 0;
        }
        if (bad != 0 || runs % 12 == 0)
            ESP_LOGW(TAG, "link soak: %" PRIu32 " runs, %" PRIu32 " mismatches, %" PRIu32 " errors (%d MHz)", runs, fails,
                     errs, bsp_knob_15_md50et_qspi_hz() / 1000000);
    }
}

void link_task_start(void)
{
    if (xTaskCreatePinnedToCoreWithCaps(link_task, "link", 4096, NULL, 1, NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS)
        ESP_LOGE(TAG, "link task not created");
}
