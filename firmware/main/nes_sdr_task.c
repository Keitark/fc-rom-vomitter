#include "nes_sdr_task.h"

#include <inttypes.h>

#include "controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nes_sdr_live.h"
#include "s3_local_spec.h"
#include "sdkconfig.h"

#if CONFIG_NESCART_NES_SDR_SYNTHETIC
#include <string.h>
#include "esp_rom_crc.h"
typedef struct {
    uint8_t frame[28u + 256u + 4u];
    uint32_t number;
} synthetic_state_t;
#endif

static const char *TAG = "nes_sdr_live";
static nes_sdr_live_stats_t s_stats;

static bool capture_frame(void *context, const uint8_t **frame, size_t *length)
{
#if CONFIG_NESCART_NES_SDR_SYNTHETIC
    synthetic_state_t *state = context;
    uint8_t *data = state->frame;
    memset(data, 0, sizeof(state->frame));
    memcpy(data, "SPC1", 4);
    uint32_t number = ++state->number;
    for (unsigned i = 0; i < 4; ++i) data[4 + i] = (uint8_t)(number >> (8u * i));
    data[16] = 0x00;
    data[17] = 0x10; /* 4096 pairs */
    data[20] = 8;  /* eight FFTs */
    data[26] = 8;  /* 256 bins */
    data[27] = 2;
    switch (number % 5u) {
    case 1: data[28 + 128] = 255; break; /* left after fftshift */
    case 2: data[28] = 255; break;       /* center/DC */
    case 3: data[28 + 127] = 255; break; /* right after fftshift */
    case 4: memset(data + 28, 255, 256); break;
    default: break;                      /* flat */
    }
    uint32_t crc = esp_rom_crc32_le(0, data, sizeof(state->frame) - 4u);
    for (unsigned i = 0; i < 4; ++i) {
        data[sizeof(state->frame) - 4u + i] = (uint8_t)(crc >> (8u * i));
    }
    *frame = data;
    *length = sizeof(state->frame);
    return true;
#else
    (void)context;
    return s3_capture_local_spec(75, 6, 256, frame, length);
#endif
}

static int refresh_graph(void *context, const uint8_t *graph, size_t length)
{
    (void)context;
    return controller_refresh_chr(graph, length) == ESP_OK ? 0 : -1;
}

static void live_task(void *context)
{
    (void)context;
    uint8_t graph[NES_SDR_GRAPH_BYTES];
#if CONFIG_NESCART_NES_SDR_SYNTHETIC
    synthetic_state_t synthetic = {0};
#endif
    const nes_sdr_live_ops_t ops = {
        .capture = capture_frame,
#if CONFIG_NESCART_NES_SDR_SYNTHETIC
        .capture_context = &synthetic,
#endif
        .refresh = refresh_graph,
    };
    bool was_armed = false;
    for (;;) {
        controller_status_t status;
        controller_get_status(&status);
        if (!status.live_armed) {
            if (was_armed) ESP_LOGW(TAG, "live refresh disarmed");
            was_armed = false;
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (!was_armed) ESP_LOGI(TAG, "live refresh armed");
        was_armed = true;

        TickType_t cycle_start = xTaskGetTickCount();
        if (controller_live_begin() == ESP_OK) {
            nes_sdr_live_result_t result = nes_sdr_live_step(&ops, &s_stats, graph);
            controller_live_end();
            if (result == NES_SDR_LIVE_OK) {
                ESP_LOGI(TAG, "frame %" PRIu32 " verified", s_stats.frames_ok);
            } else {
                ESP_LOGW(TAG, "cycle failed: %u (capture=%" PRIu32
                         ", frame=%" PRIu32 ", refresh=%" PRIu32 ")",
                         (unsigned)result, s_stats.capture_failures,
                         s_stats.invalid_frames, s_stats.refresh_failures);
            }
        }
        vTaskDelayUntil(&cycle_start, pdMS_TO_TICKS(1000));
    }
}

esp_err_t nes_sdr_task_start(void)
{
#if !CONFIG_NESCART_NES_SDR_SYNTHETIC
    if (!s3_local_spec_init()) {
        return ESP_FAIL;
    }
#else
    ESP_LOGI(TAG, "synthetic CHR cycling selected; RF capture disabled");
#endif
    if (xTaskCreate(live_task, "nes_sdr_live", 12288, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
