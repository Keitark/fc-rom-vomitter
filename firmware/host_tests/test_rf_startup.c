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

enum { DELAY, STOP_HTTP, STOP_WIFI, MODE_NULL, CREATE_RF, RECOVER };
static int events[16];
static unsigned event_count;
static bool backend_available = true;
static bool cloud_enabled;
static bool image_supported = true;
static bool console_power = true;
static bool fail_allocation;
static esp_err_t stop_http_result;
static bool http_stopped;
static bool wifi_stopped;
static bool mode_null;
static TaskFunction_t rf_entry;
static jmp_buf rf_done;
static unsigned rf_cycles;
static int64_t now_us = 100; /* Firmware RF start happens after boot, not at t=0. */
static nes_sdr_live_result_t cycle_result = NES_SDR_LIVE_OK;

static void event(int value) { CHECK(event_count < 16); events[event_count++] = value; }
static void reset_fixture(void)
{
    event_count = 0;
    http_stopped = wifi_stopped = mode_null = false;
    fail_allocation = false;
    stop_http_result = ESP_OK;
}

bool nes_sdr_rf_backend_available(void) { return backend_available; }
bool cloud_sync_enabled(void) { return cloud_enabled; }
bool controller_prg_matches(size_t offset, const void *expected, size_t length)
{
    CHECK(offset == 0x7ff0 && length == 8 && memcmp(expected, "NES-SDR1", 8) == 0);
    return image_supported;
}
void controller_get_status(controller_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->console_power = console_power;
}
esp_err_t web_server_stop_for_sdr(void)
{
    event(STOP_HTTP);
    if (stop_http_result == ESP_OK) http_stopped = true;
    return stop_http_result;
}
esp_err_t esp_wifi_stop(void) { event(STOP_WIFI); wifi_stopped = true; return ESP_OK; }
esp_err_t esp_wifi_set_mode(wifi_mode_t mode)
{
    CHECK(mode == WIFI_MODE_NULL && wifi_stopped);
    event(MODE_NULL); mode_null = true; return ESP_OK;
}
esp_err_t web_server_resume_after_sdr_failure(void)
{
    event(RECOVER); http_stopped = wifi_stopped = mode_null = false; return ESP_OK;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
                                  uint32_t stack, void *context, unsigned priority,
                                  TaskHandle_t *handle, BaseType_t core)
{
    CHECK(http_stopped && wifi_stopped && mode_null);
    CHECK(entry != NULL && strcmp(name, "nes_sdr_rf") == 0 && stack == 8192);
    CHECK(context == NULL && priority == 5 && core == 0);
    event(CREATE_RF);
    if (fail_allocation) return 0;
    rf_entry = entry;
    *handle = (void *)1;
    return pdPASS;
}
void vTaskDelay(TickType_t delay) { CHECK(delay == 500); event(DELAY); }
size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 32768; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return http_stopped ? 16384 : 4096; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test-error"; }
int64_t esp_timer_get_time(void) { return now_us; }
uint32_t nescart_crc32(const void *data, size_t length)
{ (void)data; CHECK(length == NES_SDR_GRAPH_BYTES); return 0x12345678; }
TickType_t xTaskGetTickCount(void) { return 0; }
void vTaskDelayUntil(TickType_t *wake, TickType_t period)
{
    (void)wake; CHECK(period == 200);
    nes_sdr_rf_status_t status;
    nes_sdr_platform_get_rf_status(&status);
    ++rf_cycles;
    CHECK(status.attempts == rf_cycles && status.frames_ok == 1);
    CHECK(status.graph_crc32 == 0x12345678);
    CHECK(status.last_result == (int)cycle_result);
    if (rf_cycles == 2) {
        CHECK(status.period_us == 200000);
        longjmp(rf_done, 1);
    }
    now_us += 200000;
    cycle_result = NES_SDR_LIVE_CAPTURE_FAILED;
}
void vTaskDelete(TaskHandle_t task) { (void)task; }
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_set_ps(int mode) { (void)mode; return ESP_OK; }
esp_err_t esp_wifi_set_promiscuous(bool enabled) { (void)enabled; return ESP_OK; }
esp_err_t esp_wifi_set_channel(int primary, int secondary) { (void)primary; (void)secondary; return ESP_OK; }
bool nes_sdr_rf_backend_capture(uint32_t frequency, uint32_t duration,
                                const uint8_t **frame, size_t *length)
{ (void)frequency; (void)duration; (void)frame; (void)length; return false; }
esp_err_t controller_refresh_chr_if_prg_matches(const uint8_t *data, size_t length,
                                               size_t offset, const void *expected,
                                               size_t expected_length)
{ (void)data; (void)length; (void)offset; (void)expected; (void)expected_length; return ESP_OK; }
esp_err_t controller_live_cycle_begin(size_t offset, const void *expected, size_t length)
{ (void)offset; (void)expected; (void)length; return ESP_OK; }
void controller_live_cycle_end(void) {}
nes_sdr_live_result_t nes_sdr_live_step(const nes_sdr_live_ops_t *ops,
                                      nes_sdr_live_stats_t *stats,
                                      uint8_t graph[NES_SDR_GRAPH_BYTES])
{ (void)ops; (void)stats; (void)graph; return cycle_result; }

int main(void)
{
    nes_sdr_rf_status_t status;
    nes_sdr_platform_get_rf_status(&status);
    CHECK(status.attempts == 0 && status.frames_ok == 0 && status.last_result == -1);
    CHECK(nes_sdr_platform_demo_step() == ESP_OK);
    nes_sdr_platform_get_rf_status(&status);
    CHECK(status.attempts == 0 && status.frames_ok == 0); /* Demo is not RF. */
    backend_available = false;
    CHECK(nes_sdr_platform_start_rf() == ESP_ERR_NOT_SUPPORTED);
    backend_available = true; cloud_enabled = true;
    CHECK(nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);
    cloud_enabled = false; image_supported = false;
    CHECK(nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);
    image_supported = true; console_power = false;
    CHECK(nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);
    console_power = true;
    nes_sdr_platform_service_rf_start();
    CHECK(event_count == 0);

    CHECK(nes_sdr_platform_start_rf() == ESP_OK);
    CHECK(event_count == 0); /* HTTP request must not allocate or stop itself. */
    CHECK(nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);
    stop_http_result = ESP_FAIL;
    nes_sdr_platform_service_rf_start();
    CHECK(event_count == 3 && events[0] == DELAY && events[1] == STOP_HTTP && events[2] == RECOVER);
    CHECK(strcmp(nes_sdr_platform_rf_state_name(), "error") == 0);

    reset_fixture(); fail_allocation = true;
    CHECK(nes_sdr_platform_start_rf() == ESP_OK);
    nes_sdr_platform_service_rf_start();
    CHECK(event_count == 6);
    const int expected[] = { DELAY, STOP_HTTP, STOP_WIFI, MODE_NULL, CREATE_RF, RECOVER };
    CHECK(memcmp(events, expected, sizeof(expected)) == 0);
    CHECK(!http_stopped && !wifi_stopped && !mode_null);
    CHECK(strcmp(nes_sdr_platform_rf_state_name(), "error") == 0);

    reset_fixture();
    CHECK(nes_sdr_platform_start_rf() == ESP_OK);
    nes_sdr_platform_service_rf_start();
    CHECK(event_count == 5 && memcmp(events, expected, 5 * sizeof(events[0])) == 0);
    nes_sdr_platform_service_rf_start();
    CHECK(event_count == 5); /* An accepted request is serviced once. */
    CHECK(nes_sdr_platform_start_rf() == ESP_ERR_INVALID_STATE);
    CHECK(rf_entry != NULL);
    if (setjmp(rf_done) == 0) rf_entry(NULL);
    CHECK(rf_cycles == 2 && strcmp(nes_sdr_platform_rf_state_name(), "running") == 0);
    puts("RF startup ordering, gates, allocation failure and recovery: pass");
    return 0;
}
