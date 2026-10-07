#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "cloud_sync.h"
#include "controller.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "nes_sdr_backend.h"
#include "nes_sdr_live.h"
#include "nes_sdr_platform.h"
#include "web_server.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

enum {
    DELAY, STOP_HTTP, STOP_WIFI, MODE_NULL, CREATE_RF,
    START_WIFI, SET_PS, ENABLE_PROMISCUOUS, SET_CHANNEL, DELETE_RF, RECOVER,
};
static int events[32];
static unsigned event_count;
static int failed_operation;
static unsigned injected_failures;
static unsigned radio_calls;
static unsigned capture_calls;
static unsigned refresh_calls;
static unsigned live_calls;
static unsigned cycle_begin_calls;
static unsigned cycle_end_calls;
static unsigned cycle_delay_calls;
static unsigned recovery_calls;
static bool fatal_after_cycle;
static bool http_stopped;
static bool wifi_stopped;
static bool mode_null;
static bool inside_rf_task;
static bool task_deleted;
static TaskFunction_t rf_entry;
static jmp_buf rf_done;

static void event(int value)
{
    CHECK(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count++] = value;
}

static esp_err_t radio_operation(int operation)
{
    CHECK(inside_rf_task && !task_deleted);
    ++radio_calls;
    event(operation);
    if (operation == failed_operation && injected_failures == 0) {
        ++injected_failures;
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool nes_sdr_rf_backend_available(void) { return true; }
bool cloud_sync_enabled(void) { return false; }
bool controller_prg_matches(size_t offset, const void *expected, size_t length)
{
    CHECK(offset == 0x7ff0 && length == 8 && memcmp(expected, "NES-SDR1", 8) == 0);
    return true;
}
void controller_get_status(controller_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->console_power = true;
}
esp_err_t web_server_stop_for_sdr(void)
{
    CHECK(!inside_rf_task);
    event(STOP_HTTP); http_stopped = true; return ESP_OK;
}
esp_err_t esp_wifi_stop(void)
{
    CHECK(!inside_rf_task && http_stopped);
    event(STOP_WIFI); wifi_stopped = true; return ESP_OK;
}
esp_err_t esp_wifi_set_mode(wifi_mode_t mode)
{
    CHECK(!inside_rf_task && mode == WIFI_MODE_NULL && wifi_stopped);
    event(MODE_NULL); mode_null = true; return ESP_OK;
}
esp_err_t web_server_resume_after_sdr_failure(void)
{
    CHECK(!inside_rf_task && task_deleted);
    CHECK(http_stopped && wifi_stopped && mode_null);
    CHECK(strcmp(nes_sdr_platform_rf_state_name(), "error") == 0);
    CHECK(nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);
    ++recovery_calls;
    event(RECOVER);
    http_stopped = wifi_stopped = mode_null = false;
    return ESP_OK;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
                                  uint32_t stack, void *context, unsigned priority,
                                  TaskHandle_t *handle, BaseType_t core)
{
    CHECK(!inside_rf_task && http_stopped && wifi_stopped && mode_null);
    CHECK(entry != NULL && strcmp(name, "nes_sdr_rf") == 0 && stack == 8192);
    CHECK(context == NULL && priority == 5 && core == 0);
    event(CREATE_RF);
    rf_entry = entry;
    *handle = (void *)1;
    return pdPASS;
}
void vTaskDelay(TickType_t delay)
{
    CHECK(!inside_rf_task && delay == 500);
    event(DELAY);
}
void vTaskDelete(TaskHandle_t task)
{
    CHECK(inside_rf_task && task == NULL && recovery_calls == 0);
    CHECK(strcmp(nes_sdr_platform_rf_state_name(), "error") == 0);
    event(DELETE_RF);
    task_deleted = true;
    inside_rf_task = false;
    longjmp(rf_done, 1); /* FreeRTOS never returns after deleting the current task. */
}
size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 32768; }
size_t heap_caps_get_largest_free_block(uint32_t caps)
{ (void)caps; return http_stopped ? 16384 : 4096; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test-error"; }
int64_t esp_timer_get_time(void) { return 100; }
uint32_t nescart_crc32(const void *data, size_t length)
{ (void)data; (void)length; CHECK(false); return 0; }
TickType_t xTaskGetTickCount(void)
{ CHECK(fatal_after_cycle && inside_rf_task); return 0; }
void vTaskDelayUntil(TickType_t *wake, TickType_t period)
{
    CHECK(fatal_after_cycle && inside_rf_task && wake != NULL && period == 200);
    CHECK(live_calls == 1 && cycle_end_calls == 1);
    ++cycle_delay_calls;
}
esp_err_t esp_wifi_start(void) { return radio_operation(START_WIFI); }
esp_err_t esp_wifi_set_ps(int mode)
{ CHECK(mode == WIFI_PS_NONE); return radio_operation(SET_PS); }
esp_err_t esp_wifi_set_promiscuous(bool enabled)
{ CHECK(enabled); return radio_operation(ENABLE_PROMISCUOUS); }
esp_err_t esp_wifi_set_channel(int primary, int secondary)
{
    CHECK(primary == 1 && secondary == WIFI_SECOND_CHAN_NONE);
    return radio_operation(SET_CHANNEL);
}
bool nes_sdr_rf_backend_capture(uint32_t frequency, uint32_t duration,
                                const uint8_t **frame, size_t *length)
{
    (void)frequency; (void)duration; (void)frame; (void)length;
    ++capture_calls; return false;
}
esp_err_t controller_refresh_chr_if_prg_matches(const uint8_t *data, size_t length,
                                               size_t offset, const void *expected,
                                               size_t expected_length)
{
    (void)data; (void)length; (void)offset; (void)expected; (void)expected_length;
    ++refresh_calls; return ESP_OK;
}
esp_err_t controller_live_cycle_begin(size_t offset, const void *expected, size_t length)
{
    CHECK(offset == 0x7ff0 && length == 8 && memcmp(expected, "NES-SDR1", 8) == 0);
    ++cycle_begin_calls;
    return fatal_after_cycle && cycle_begin_calls == 2 ? ESP_ERR_INVALID_STATE : ESP_OK;
}
void controller_live_cycle_end(void)
{
    CHECK(fatal_after_cycle && inside_rf_task && cycle_begin_calls == 1);
    ++cycle_end_calls;
}
nes_sdr_live_result_t nes_sdr_live_step(const nes_sdr_live_ops_t *ops,
                                      nes_sdr_live_stats_t *stats,
                                      uint8_t graph[NES_SDR_GRAPH_BYTES])
{
    (void)ops; (void)stats; (void)graph;
    CHECK(fatal_after_cycle && inside_rf_task && cycle_begin_calls == 1);
    ++live_calls; return NES_SDR_LIVE_CAPTURE_FAILED;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    static const char *const faults[] = {
        "start", "power_save", "promiscuous", "channel", "capture_gate",
    };
    unsigned fault;
    for (fault = 0; fault < sizeof(faults) / sizeof(faults[0]); ++fault) {
        if (strcmp(argv[1], faults[fault]) == 0) break;
    }
    CHECK(fault < sizeof(faults) / sizeof(faults[0]));
    fatal_after_cycle = fault == 4;
    failed_operation = fatal_after_cycle ? -1 : START_WIFI + (int)fault;

    CHECK(nes_sdr_platform_start_rf() == ESP_OK);
    CHECK(event_count == 0 && strcmp(nes_sdr_platform_rf_state_name(), "starting") == 0);
    nes_sdr_platform_service_rf_start();
    static const int teardown[] = { DELAY, STOP_HTTP, STOP_WIFI, MODE_NULL, CREATE_RF };
    CHECK(event_count == 5 && memcmp(events, teardown, sizeof(teardown)) == 0);
    CHECK(rf_entry != NULL && nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);

    if (setjmp(rf_done) == 0) {
        inside_rf_task = true;
        rf_entry(NULL);
        CHECK(false); /* A failed RF task must delete itself rather than return. */
    }
    CHECK(task_deleted && !inside_rf_task);
    const unsigned expected_radio_calls = fatal_after_cycle ? 4 : fault + 1;
    CHECK(injected_failures == (fatal_after_cycle ? 0u : 1u));
    CHECK(radio_calls == expected_radio_calls && event_count == 6 + expected_radio_calls);
    for (unsigned i = 0; i < expected_radio_calls; ++i) {
        CHECK(events[5 + i] == START_WIFI + (int)i);
    }
    CHECK(events[event_count - 1] == DELETE_RF && recovery_calls == 0);
    CHECK(capture_calls == 0 && refresh_calls == 0);
    CHECK(strcmp(nes_sdr_platform_rf_state_name(), "error") == 0);
    nes_sdr_rf_status_t status;
    nes_sdr_platform_get_rf_status(&status);
    if (fatal_after_cycle) {
        CHECK(live_calls == 1 && cycle_begin_calls == 2 && cycle_end_calls == 1 && cycle_delay_calls == 1);
        CHECK(status.attempts == 1 && status.frames_ok == 0);
        CHECK(status.last_result == NES_SDR_LIVE_CAPTURE_FAILED);
        CHECK(status.period_us == 0 && status.graph_crc32 == 0);
        CHECK(nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);
        const unsigned after_deletion = event_count;
        nes_sdr_platform_service_rf_start();
        nes_sdr_platform_service_rf_start();
        CHECK(recovery_calls == 0 && event_count == after_deletion);
        CHECK(http_stopped && wifi_stopped && mode_null);
        CHECK(strcmp(nes_sdr_platform_rf_state_name(), "error") == 0);
        CHECK(nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);
        puts("RF post-capture fatal gate: task deletion, AP kept offline and reset-required retry gate pass");
        return 0;
    }
    CHECK(live_calls == 0 && cycle_begin_calls == 0 && cycle_end_calls == 0 && cycle_delay_calls == 0);
    CHECK(status.attempts == 0 && status.frames_ok == 0 && status.last_result == -1);
    CHECK(status.period_us == 0 && status.graph_crc32 == 0);
    CHECK(nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);

    nes_sdr_platform_service_rf_start();
    CHECK(recovery_calls == 1 && events[event_count - 1] == RECOVER);
    CHECK(!http_stopped && !wifi_stopped && !mode_null);
    CHECK(strcmp(nes_sdr_platform_rf_state_name(), "error") == 0);
    const unsigned after_recovery = event_count;
    nes_sdr_platform_service_rf_start();
    CHECK(recovery_calls == 1 && event_count == after_recovery);
    CHECK(nes_sdr_platform_start_rf() == ESP_OK);
    CHECK(strcmp(nes_sdr_platform_rf_state_name(), "starting") == 0);
    nes_sdr_platform_service_rf_start();
    CHECK(event_count == after_recovery + 5);
    CHECK(memcmp(events + after_recovery, teardown, sizeof(teardown)) == 0);
    CHECK(injected_failures == 1 && recovery_calls == 1);
    printf("RF startup %s failure: task deletion, deferred setup recovery and retry pass\n", argv[1]);
    return 0;
}
