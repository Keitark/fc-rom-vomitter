#include "nes_sdr_platform.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "controller.h"
#include "nes_sdr_frame.h"
#include "nes_sdr_live.h"

enum {
    SYNTH_FFT_BINS = 256,
    SPC1_HEADER_BYTES = 28,
    SPC1_CRC_BYTES = 4,
    SYNTH_FRAME_BYTES = SPC1_HEADER_BYTES + SYNTH_FFT_BINS + SPC1_CRC_BYTES,
};

static uint8_t s_frame[SYNTH_FRAME_BYTES];
static uint8_t s_graph[NES_SDR_GRAPH_BYTES];
static nes_sdr_live_stats_t s_stats;
static uint32_t s_frame_number;
static unsigned s_phase;

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
    (void)context;
    return controller_refresh_chr(graph, length) == ESP_OK ? 0 : -1;
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
