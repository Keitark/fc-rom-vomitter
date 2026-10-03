#include "nes_sdr_platform.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cloud_sync.h"
#include "controller.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nes_sdr_backend.h"
#include "nes_sdr_frame.h"
#include "nes_sdr_live.h"
#include "web_server.h"

enum {
    SYNTH_FFT_BINS = 256,
    SPC1_HEADER_BYTES = 28,
    SPC1_CRC_BYTES = 4,
    SYNTH_FRAME_BYTES = SPC1_HEADER_BYTES + SYNTH_FFT_BINS + SPC1_CRC_BYTES,
};

static const uint8_t NES_SDR_SIGNATURE[] = "NES-SDR1";
enum { NES_SDR_SIGNATURE_PRG_OFFSET = 0x7ff0 };

typedef enum {
    RF_STATE_UNAVAILABLE,
    RF_STATE_IDLE,
    RF_STATE_STARTING,
    RF_STATE_RUNNING,
    RF_STATE_ERROR,
} rf_state_t;

static const char *TAG = "nes_sdr";
static uint8_t s_frame[SYNTH_FRAME_BYTES];
static uint8_t s_graph[NES_SDR_GRAPH_BYTES];
static nes_sdr_live_stats_t s_stats;
static uint32_t s_frame_number;
static unsigned s_phase;
static TaskHandle_t s_rf_task;
static volatile rf_state_t s_rf_state = RF_STATE_IDLE;

typedef struct {
    int64_t capture_us;
    int64_t refresh_us;
} rf_cycle_timing_t;

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
}

static void put64(uint8_t *p, uint64_t value)
{
    put32(p, (uint32_t)value);
    put32(p + 4, (uint32_t)(value >> 32));
}

static bool synthetic_capture(void *context, const uint8_t **frame, size_t *length)
{
    (void)context;
    memset(s_frame, 0, sizeof(s_frame));
    memcpy(s_frame, "SPC1", 4);
    put32(s_frame + 4, s_frame_number++);
    put64(s_frame + 8, (uint64_t)s_frame_number * SYNTH_FFT_BINS);
    put32(s_frame + 16, SYNTH_FFT_BINS);
    put16(s_frame + 20, 1);
    s_frame[26] = 8; /* log2(256) */
    s_frame[27] = 2; /* ESP-SDR dB code multiplier */

    uint8_t *bins = s_frame + SPC1_HEADER_BYTES;
    for (unsigned k = 0; k < SYNTH_FFT_BINS; ++k) {
        bins[k] = (uint8_t)(24u + ((k * 13u + s_phase * 7u) & 0x0fu));
    }

    /*
     * SPC1 is natural FFT order: DC is bin 0. Put one positive-frequency
     * peak in the first half and one negative-frequency peak near the end.
     * NES-SDR applies fftshift before reducing to 24 display columns.
     */
    const unsigned positive = 20u + (s_phase % 44u);
    const unsigned negative = 236u - (s_phase % 40u);
    for (int d = -5; d <= 5; ++d) {
        const unsigned width = (unsigned)(6 - (d < 0 ? -d : d));
        const int p = (int)positive + d;
        const int n = (int)negative + d;
        if (p >= 0 && p < SYNTH_FFT_BINS) {
            bins[p] = (uint8_t)(90u + width * 26u);
        }
        if (n >= 0 && n < SYNTH_FFT_BINS) {
            bins[n] = (uint8_t)(75u + width * 22u);
        }
    }

    s_phase = (s_phase + 3u) % 64u;
    *frame = s_frame;
    *length = sizeof(s_frame);
    return true;
}

static int refresh_graph(void *context, const uint8_t *graph, size_t length)
{
    const int64_t start = esp_timer_get_time();
    const esp_err_t err = controller_refresh_chr_if_prg_matches(
               graph, length,
               NES_SDR_SIGNATURE_PRG_OFFSET,
               NES_SDR_SIGNATURE,
               sizeof(NES_SDR_SIGNATURE) - 1u);
    if (context != NULL) {
        ((rf_cycle_timing_t *)context)->refresh_us = esp_timer_get_time() - start;
    }
    return err == ESP_OK ? 0 : -1;
}

static bool rf_capture(void *context, const uint8_t **frame, size_t *length)
{
    const int64_t start = esp_timer_get_time();
    const bool ok = nes_sdr_rf_backend_capture(2442u, 50u, frame, length);
    if (context != NULL) {
        ((rf_cycle_timing_t *)context)->capture_us = esp_timer_get_time() - start;
    }
    return ok;
}

bool nes_sdr_platform_image_supported(void)
{
    return controller_prg_matches(NES_SDR_SIGNATURE_PRG_OFFSET,
                                  NES_SDR_SIGNATURE,
                                  sizeof(NES_SDR_SIGNATURE) - 1u);
}

bool nes_sdr_platform_rf_available(void)
{
    return nes_sdr_rf_backend_available();
}

const char *nes_sdr_platform_rf_state_name(void)
{
    if (!nes_sdr_rf_backend_available()) {
        return "unavailable";
    }
    switch (s_rf_state) {
    case RF_STATE_IDLE: return "idle";
    case RF_STATE_STARTING: return "starting";
    case RF_STATE_RUNNING: return "running";
    case RF_STATE_ERROR: return "error";
    case RF_STATE_UNAVAILABLE:
    default: return "unavailable";
    }
}

esp_err_t nes_sdr_platform_demo_step(void)
{
    const nes_sdr_live_ops_t ops = {
        .capture = synthetic_capture,
        .capture_context = NULL,
        .refresh = refresh_graph,
        .refresh_context = NULL,
    };

    const nes_sdr_live_result_t result =
        nes_sdr_live_step(&ops, &s_stats, s_graph);
    switch (result) {
    case NES_SDR_LIVE_OK:
        return ESP_OK;
    case NES_SDR_LIVE_BAD_ARGUMENT:
        return ESP_ERR_INVALID_ARG;
    case NES_SDR_LIVE_CAPTURE_FAILED:
        return ESP_ERR_INVALID_STATE;
    case NES_SDR_LIVE_FRAME_INVALID:
        return ESP_ERR_INVALID_RESPONSE;
    case NES_SDR_LIVE_REFRESH_FAILED:
    default:
        return ESP_FAIL;
    }
}


static void rf_task(void *context)
{
    (void)context;

    /* Give the HTTP response time to leave before the SoftAP disappears. */
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t err = web_server_stop_for_sdr();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to stop HTTP server: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to stop setup Wi-Fi: %s", esp_err_to_name(err));
        goto fail;
    }
    err = esp_wifi_set_mode(WIFI_MODE_NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to select WIFI_MODE_NULL: %s", esp_err_to_name(err));
        goto fail;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to restart Wi-Fi PHY: %s", esp_err_to_name(err));
        goto fail;
    }
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to disable Wi-Fi power save: %s", esp_err_to_name(err));
        goto fail;
    }
    err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to enable promiscuous RX: %s", esp_err_to_name(err));
        goto fail;
    }
    err = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to seed RX channel: %s", esp_err_to_name(err));
        goto fail;
    }

    s_rf_state = RF_STATE_RUNNING;
    ESP_LOGI(TAG, "exclusive SDR mode active; SoftAP is offline");

    TickType_t wake = xTaskGetTickCount();
    int64_t previous_start = 0;

    for (;;) {
        rf_cycle_timing_t timing = {0};
        const nes_sdr_live_ops_t ops = {
            .capture = rf_capture,
            .capture_context = &timing,
            .refresh = refresh_graph,
            .refresh_context = &timing,
        };
        err = controller_live_cycle_begin(NES_SDR_SIGNATURE_PRG_OFFSET,
                                          NES_SDR_SIGNATURE,
                                          sizeof(NES_SDR_SIGNATURE) - 1u);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RF cycle stopped: signed image or console power lost (%s)",
                     esp_err_to_name(err));
            goto fail;
        }
        const int64_t start = esp_timer_get_time();
        const int64_t period_us = previous_start == 0 ? 0 : start - previous_start;
        previous_start = start;
        const nes_sdr_live_result_t result =
            nes_sdr_live_step(&ops, &s_stats, s_graph);
        const int64_t total_us = esp_timer_get_time() - start;
        controller_live_cycle_end();
        const int64_t render_us = total_us - timing.capture_us - timing.refresh_us;
        ESP_LOGI(TAG, "RF frame %" PRIu32 " result=%d capture=%" PRId64
                 "us render=%" PRId64 "us refresh=%" PRId64
                 "us total=%" PRId64 "us period=%" PRId64
                 "us target=200000us",
                 s_stats.attempts, (int)result, timing.capture_us, render_us,
                 timing.refresh_us, total_us, period_us);
        if (total_us > 200000) {
            ESP_LOGW(TAG, "RF frame exceeded the 200 ms budget");
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(200));
    }

fail:
    s_rf_state = RF_STATE_ERROR;
    s_rf_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t nes_sdr_platform_start_rf(void)
{
    if (!nes_sdr_rf_backend_available()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (cloud_sync_enabled() || !nes_sdr_platform_image_supported()) {
        return ESP_ERR_INVALID_STATE;
    }

    controller_status_t status;
    controller_get_status(&status);
    if (!status.console_power ||
        s_rf_state == RF_STATE_STARTING ||
        s_rf_state == RF_STATE_RUNNING ||
        s_rf_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_rf_state = RF_STATE_STARTING;
    if (xTaskCreatePinnedToCore(rf_task, "nes_sdr_rf", 8192, NULL, 5,
                                &s_rf_task, 0) != pdPASS) {
        s_rf_task = NULL;
        s_rf_state = RF_STATE_ERROR;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
