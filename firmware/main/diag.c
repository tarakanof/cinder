#include "diag.h"

#include <stdatomic.h>
#include <string.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/temperature_sensor.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "diag";

static uint32_t s_boots;
static atomic_uint s_largest_min = UINT32_MAX;
static bool s_has_crash;
static uint32_t s_crash_pc;
static char s_crash_task[DEV_TASK_NAME_MAX + 1];
static const char *s_crash_reason = "unknown";
static bool s_has_crash_id;
static uint32_t s_crash_id, s_crash_size;
static char s_crash_elf[OTA_BUILD_HEX + 1];

#define TWDT_MAGIC 0x54574454u
typedef struct {
    uint32_t magic;
    dev_twdt_capture_t cap;
} twdt_note_t;
static RTC_NOINIT_ATTR twdt_note_t s_twdt;
static char s_wdt_culprit[DEV_TASK_NAME_MAX + 1];
static RTC_NOINIT_ATTR dev_stall_note_t s_stall;
static bool s_stall_reset;

static void twdt_msg(void *opaque, const char *msg) { dev_twdt_capture_msg(opaque, msg); }

void esp_task_wdt_isr_user_handler(void)
{
    if (s_twdt.magic == TWDT_MAGIC) return;
    memset(&s_twdt.cap, 0, sizeof s_twdt.cap);
    esp_task_wdt_print_triggered_tasks(twdt_msg, &s_twdt.cap, NULL);
    s_twdt.magic = TWDT_MAGIC;
}

#define DIAG_TASKS(X) X("ember") X("eye") X("link") X("lvgl") X("pomo") X("prov") X("rim") X("stats") X("weather")
#define DIAG_TASK_ENTRY(name) name,
#define DIAG_TASK_FITS(name) \
    _Static_assert(sizeof name <= sizeof TASKS[0], "task name " name " fits TASKS[] with its NUL");
static const char TASKS[][8] = {DIAG_TASKS(DIAG_TASK_ENTRY)};
DIAG_TASKS(DIAG_TASK_FITS)
_Static_assert(sizeof TASKS / sizeof TASKS[0] * (sizeof ", " - 1 + sizeof TASKS[0] - 1 + 1 + 10) + 1 <= DIAG_STACKS_LINE_MAX,
               "worst-case stack line fits");
_Static_assert(sizeof TASKS / sizeof TASKS[0] <= DEV_DIAG_MAX_TASKS, "task list fits");
_Static_assert(sizeof TASKS[0] <= DEV_TASK_NAME_MAX + 1, "task name fits dev_diag_t");
_Static_assert(sizeof TASKS / sizeof TASKS[0] <= OTA_HEALTH_TASKS_MAX, "task list fits the health gate");
#define DIAG_IDF_TASKS(X) X("esp_timer") X("ipc0") X("ipc1") X("sys_evt") X("tiT") X("Tmr Svc") X("wifi")
#define DIAG_IDF_TASK_FITS(name) \
    _Static_assert(sizeof name <= sizeof IDF_TASKS[0], "task name " name " fits IDF_TASKS[] with its NUL");
static const char IDF_TASKS[][10] = {DIAG_IDF_TASKS(DIAG_TASK_ENTRY)};
DIAG_IDF_TASKS(DIAG_IDF_TASK_FITS)
_Static_assert(sizeof TASKS / sizeof TASKS[0] * (sizeof ", " - 1 + sizeof TASKS[0] - 1 + 1 + 10) + sizeof " | idf " - 1 +
                       sizeof IDF_TASKS / sizeof IDF_TASKS[0] * (sizeof ", " - 1 + sizeof IDF_TASKS[0] - 1 + 1 + 10) + 1 <=
                   DIAG_STACKS_LINE_MAX,
               "worst-case stack line with IDF tasks fits");

_Static_assert(ESP_RST_UNKNOWN == 0 && ESP_RST_POWERON == 1 && ESP_RST_EXT == 2 && ESP_RST_SW == DEV_RR_SW &&
                   ESP_RST_PANIC == DEV_RR_PANIC && ESP_RST_INT_WDT == DEV_RR_INT_WDT && ESP_RST_TASK_WDT == DEV_RR_TASK_WDT &&
                   ESP_RST_WDT == DEV_RR_WDT && ESP_RST_DEEPSLEEP == 8 && ESP_RST_BROWNOUT == 9 && ESP_RST_SDIO == 10 &&
                   ESP_RST_USB == 11 && ESP_RST_JTAG == 12 && ESP_RST_EFUSE == 13 && ESP_RST_PWR_GLITCH == 14 &&
                   ESP_RST_CPU_LOCKUP == DEV_RR_COUNT - 1 && DEV_RR_LVGL_STALL >= DEV_RR_COUNT,
               "dev_reset_reason_name and DEV_RR_* follow esp_reset_reason_t");

static atomic_int s_level;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_frames, s_req_ok, s_req_fail, s_req_ms_max;
static uint64_t s_frame_us, s_req_ms_total;
static int64_t s_frame_us_max;

static int64_t s_win_start_us;
static bool s_reported;
static uint64_t s_idle_base[portNUM_PROCESSORS];
static temperature_sensor_handle_t s_tsens;
static bool s_tsens_failed;

static void window_start(void)
{
    s_win_start_us = esp_timer_get_time();
    for (int i = 0; i < portNUM_PROCESSORS; i++) s_idle_base[i] = ulTaskGetIdleRunTimeCounterForCore(i);
    taskENTER_CRITICAL(&s_mux);
    s_frames = s_req_ok = s_req_fail = s_req_ms_max = 0;
    s_frame_us = s_req_ms_total = 0;
    s_frame_us_max = 0;
    taskEXIT_CRITICAL(&s_mux);
}

void diag_set_level(ks_diag_t level)
{
    if (atomic_exchange(&s_level, (int)level) != (int)level && level != KS_DIAG_OFF) {
        window_start();
        s_reported = false;
    }
}

bool diag_due(int stats_interval_ms, int period_ms, bool live)
{
    if (atomic_load(&s_level) == KS_DIAG_OFF) return false;
    int64_t window_ms = (esp_timer_get_time() - s_win_start_us) / 1000;
    return dev_stats_due(window_ms, stats_interval_ms, period_ms, live, !s_reported);
}

void diag_note_frame(int64_t us)
{
    if (atomic_load_explicit(&s_level, memory_order_relaxed) != KS_DIAG_FULL) return;
    taskENTER_CRITICAL(&s_mux);
    s_frames++;
    s_frame_us += (uint64_t)us;
    if (us > s_frame_us_max) s_frame_us_max = us;
    taskEXIT_CRITICAL(&s_mux);
}

void diag_note_request(bool ok, uint32_t ms)
{
    if (atomic_load_explicit(&s_level, memory_order_relaxed) != KS_DIAG_FULL) return;
    taskENTER_CRITICAL(&s_mux);
    if (ok) s_req_ok++;
    else s_req_fail++;
    s_req_ms_total += ms;
    if (ms > s_req_ms_max) s_req_ms_max = ms;
    taskEXIT_CRITICAL(&s_mux);
}

static bool read_temp(float *out)
{
    if (s_tsens_failed) return false;
    if (!s_tsens) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK) {
            s_tsens_failed = true;
            s_tsens = NULL;
            return false;
        }
    }
    bool ok = temperature_sensor_enable(s_tsens) == ESP_OK && temperature_sensor_get_celsius(s_tsens, out) == ESP_OK;
    temperature_sensor_disable(s_tsens);
    return ok;
}

bool diag_sample(dev_stats_t *st)
{
    int level = atomic_load(&s_level);
    if (level == KS_DIAG_OFF) return false;
    memset(st, 0, sizeof *st);
    int64_t now = esp_timer_get_time();
    int64_t period_us = now - s_win_start_us;
    if (!dev_window_ok(period_us)) {
        st->no_window = true;
        window_start();
    }
    st->period_ms = (uint32_t)(period_us / 1000);
    st->n_cpu = portNUM_PROCESSORS;
    for (int i = 0; i < portNUM_PROCESSORS; i++)
        st->cpu_pct[i] = dev_cpu_pct((uint64_t)ulTaskGetIdleRunTimeCounterForCore(i) - s_idle_base[i], (uint64_t)period_us);
    st->has_heap_min = true;
    st->heap_internal_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t ps_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (ps_total) {
        st->has_psram = true;
        st->psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        st->psram_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        st->psram_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
    st->has_temp = read_temp(&st->temp_c);
    st->reset_reason = dev_boot_reason_name((int)esp_reset_reason(), s_stall_reset);
    if (level == KS_DIAG_FULL) {
        taskENTER_CRITICAL(&s_mux);
        uint32_t frames = s_frames, ok = s_req_ok, fail = s_req_fail, rmax = s_req_ms_max;
        uint64_t fus = s_frame_us, rms = s_req_ms_total;
        int64_t fmax = s_frame_us_max;
        taskEXIT_CRITICAL(&s_mux);
        st->has_req = true;
        st->req_ok = ok;
        st->req_fail = fail;
        st->req_ms_max = rmax;
        if (ok + fail) st->req_ms_avg = (float)rms / (float)(ok + fail);
        st->has_frames = true;
        st->fps = period_us > 0 ? frames * 1e6f / (float)period_us : 0;
        st->frame_ms_avg = frames ? (float)fus / frames / 1000.0f : 0;
        st->frame_ms_max = (uint32_t)((fmax + 999) / 1000);
    }
    return true;
}

void diag_commit(void)
{
    if (atomic_load(&s_level) == KS_DIAG_OFF) return;
    window_start();
    s_reported = true;
}

static bool dump_crc(const esp_partition_t *part, uint32_t *crc, uint32_t *size_out)
{
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size < 4 || addr != part->address) return false;
    *size_out = (uint32_t)size;
    return esp_partition_read(part, size - 4, crc, sizeof *crc) == ESP_OK;
}

/* Erase a partition that is neither blank nor a valid dump: FLASH_NO_OVERWRITE would block every future dump (docs/features.md, #22). */
static void coredump_boot(nvs_handle_t h, bool nvs_ok, int reset_reason)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    uint32_t first = 0xFFFFFFFF;
    if (!part || esp_partition_read(part, 0, &first, sizeof first) != ESP_OK || first == 0xFFFFFFFF) return;
    if (esp_core_dump_image_check() != ESP_OK) {
        esp_err_t err = esp_core_dump_image_erase();
        ESP_LOGW(TAG, "core dump partition held no valid dump: erased (%s)", esp_err_to_name(err));
        return;
    }
    esp_core_dump_summary_t *sum = heap_caps_calloc(1, sizeof *sum, MALLOC_CAP_SPIRAM);
    if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
        s_has_crash = true;
        s_crash_pc = sum->exc_pc;
        snprintf(s_crash_task, sizeof s_crash_task, "%.*s", DEV_TASK_NAME_MAX, sum->exc_task);
        snprintf(s_crash_elf, sizeof s_crash_elf, "%.8s", (const char *)sum->app_elf_sha256);
        if (!ota_hex_valid(s_crash_elf, OTA_BUILD_HEX)) s_crash_elf[0] = 0;
    }
    free(sum);
    if (!s_has_crash) return;
    uint32_t crc = 0, seen = 0, size = 0;
    uint8_t rr = 0;
    char culprit[DEV_TASK_NAME_MAX + 1];
    snprintf(culprit, sizeof culprit, "%s", s_wdt_culprit);
    bool have_crc = dump_crc(part, &crc, &size);
    if (have_crc) {
        s_has_crash_id = true;
        s_crash_id = crc;
        s_crash_size = size;
    }
    if (nvs_ok && have_crc) {
        if (nvs_get_u32(h, "dump_crc", &seen) == ESP_OK && seen == crc && nvs_get_u8(h, "dump_rr", &rr) == ESP_OK) {
            s_crash_reason = dev_crash_reason_name(rr);
            size_t n = sizeof culprit;
            if (nvs_get_str(h, "dump_wdt", culprit, &n) != ESP_OK) culprit[0] = 0;
        } else {
            int code = s_stall_reset ? DEV_RR_LVGL_STALL : reset_reason;
            s_crash_reason = dev_crash_reason_name(code);
            nvs_set_u32(h, "dump_crc", crc);
            nvs_set_u8(h, "dump_rr", (uint8_t)code);
            nvs_set_str(h, "dump_wdt", culprit);
        }
    }
    const char *task = dev_crash_task(s_crash_reason, culprit, s_crash_task);
    if (task != s_crash_task) snprintf(s_crash_task, sizeof s_crash_task, "%s", task);
    ESP_LOGW(TAG, "core dump in flash: %s in task %s, pc 0x%08" PRIx32 ", id %08" PRIx32 ", %" PRIu32 " B",
             s_crash_reason, s_crash_task, s_crash_pc, s_crash_id, s_crash_size);
}

void diag_boot(void)
{
    int rr = (int)esp_reset_reason();
    if (s_twdt.magic == TWDT_MAGIC && rr == ESP_RST_TASK_WDT)
        snprintf(s_wdt_culprit, sizeof s_wdt_culprit, "%.*s", DEV_TASK_NAME_MAX, s_twdt.cap.name);
    s_twdt.magic = 0;
    if (s_wdt_culprit[0]) ESP_LOGW(TAG, "task watchdog culprit: %s", s_wdt_culprit);
    s_stall_reset = dev_stall_boot(&s_stall, rr);
    if (s_stall_reset)
        ESP_LOGW(TAG, "reset by the LVGL stall watchdog (%" PRIu32 " in a row%s)", s_stall.resets,
                 s_stall.resets >= DEV_STALL_MAX_RESETS ? ", log only this boot" : "");
    nvs_handle_t h = 0;
    bool nvs_ok = nvs_open("diag", NVS_READWRITE, &h) == ESP_OK;
    if (nvs_ok) {
        if (nvs_get_u32(h, "boots", &s_boots) != ESP_OK) s_boots = 0;
        s_boots++;
        nvs_set_u32(h, "boots", s_boots);
    }
    ESP_LOGI(TAG, "reset reason %s, boot %" PRIu32, dev_reset_reason_name(rr), s_boots);
    coredump_boot(h, nvs_ok, rr);
    if (nvs_ok) {
        nvs_commit(h);
        nvs_close(h);
    }
    diag_track();
}

void diag_track(void)
{
    unsigned v = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    unsigned cur = atomic_load(&s_largest_min);
    while (v < cur && !atomic_compare_exchange_weak(&s_largest_min, &cur, v)) {
    }
}

void diag_health(ota_health_in_t *h)
{
    diag_track();
    h->heap_internal_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    h->heap_largest_min = atomic_load(&s_largest_min);
    h->n_tasks = 0;
    for (size_t i = 0; i < sizeof TASKS / sizeof TASKS[0]; i++) {
        TaskHandle_t t = xTaskGetHandle(TASKS[i]);
        if (!t) continue;
        h->tasks[h->n_tasks].name = TASKS[i];
        h->tasks[h->n_tasks].stack_free = (uint32_t)uxTaskGetStackHighWaterMark(t);
        h->n_tasks++;
    }
}

dev_stall_act_t diag_stall_check(bool stalled)
{
    return dev_stall_check(&s_stall, stalled, esp_timer_get_time() / 1000);
}

void diag_stacks_line(char out[DIAG_STACKS_LINE_MAX])
{
    const size_t cap = DIAG_STACKS_LINE_MAX;
    size_t len = 0;
    out[0] = 0;
    for (size_t i = 0; i < sizeof TASKS / sizeof TASKS[0]; i++) {
        TaskHandle_t t = xTaskGetHandle(TASKS[i]);
        if (!t) continue;
        int n = snprintf(out + len, cap - len, "%s%s %u", len ? ", " : "", TASKS[i], (unsigned)uxTaskGetStackHighWaterMark(t));
        if (n < 0 || (size_t)n >= cap - len) return;
        len += (size_t)n;
    }
    const char *sep = " | idf ";
    for (size_t i = 0; i < sizeof IDF_TASKS / sizeof IDF_TASKS[0]; i++) {
        TaskHandle_t t = xTaskGetHandle(IDF_TASKS[i]);
        if (!t) continue;
        int n = snprintf(out + len, cap - len, "%s%s %u", sep, IDF_TASKS[i], (unsigned)uxTaskGetStackHighWaterMark(t));
        if (n < 0 || (size_t)n >= cap - len) return;
        len += (size_t)n;
        sep = ", ";
    }
}

void diag_fill(dev_diag_t *d)
{
    diag_track();
    memset(d, 0, sizeof *d);
    d->reset_reason = dev_boot_reason_name((int)esp_reset_reason(), s_stall_reset);
    d->boots = s_boots;
    d->heap_internal_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    d->heap_largest_min = atomic_load(&s_largest_min);
    for (size_t i = 0; i < sizeof TASKS / sizeof TASKS[0]; i++) {
        TaskHandle_t t = xTaskGetHandle(TASKS[i]);
        if (!t) continue;
        memcpy(d->tasks[d->n_tasks].name, TASKS[i], sizeof TASKS[i]);
        d->tasks[d->n_tasks].stack_free = (uint32_t)uxTaskGetStackHighWaterMark(t);
        d->n_tasks++;
    }
    d->has_crash = s_has_crash;
    d->crash_reason = s_crash_reason;
    d->crash_pc = s_crash_pc;
    memcpy(d->crash_task, s_crash_task, sizeof d->crash_task);
    d->has_crash_id = s_has_crash && s_has_crash_id;
    d->crash_id = s_crash_id;
    d->crash_size = s_crash_size;
    memcpy(d->crash_elf, s_crash_elf, sizeof d->crash_elf);
}

bool diag_crash_id(uint32_t *id, uint32_t *size)
{
    if (!s_has_crash || !s_has_crash_id) return false;
    *id = s_crash_id;
    *size = s_crash_size;
    return true;
}

const uint8_t *diag_crash_map(esp_partition_mmap_handle_t *h)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    const void *p = NULL;
    if (!part || !s_has_crash_id || s_crash_size > part->size) return NULL;
    esp_err_t err = esp_partition_mmap(part, 0, s_crash_size, ESP_PARTITION_MMAP_DATA, &p, h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "core dump map failed: %s", esp_err_to_name(err));
        return NULL;
    }
    return p;
}

esp_err_t diag_crash_erase(void)
{
    esp_err_t err = esp_core_dump_image_erase();
    if (err != ESP_OK) return err;
    nvs_handle_t h;
    if (nvs_open("diag", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "dump_crc");
        nvs_erase_key(h, "dump_rr");
        nvs_erase_key(h, "dump_wdt");
        nvs_commit(h);
        nvs_close(h);
    }
    s_has_crash = s_has_crash_id = false;
    s_crash_id = s_crash_size = 0;
    s_crash_pc = 0;
    s_crash_task[0] = 0;
    s_crash_elf[0] = 0;
    s_crash_reason = "unknown";
    return ESP_OK;
}
