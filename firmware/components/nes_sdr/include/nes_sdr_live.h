#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nes_sdr_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*nes_sdr_capture_fn)(void *context,
                                   const uint8_t **frame,
                                   size_t *length);

typedef int (*nes_sdr_refresh_fn)(void *context,
                                  const uint8_t *graph,
                                  size_t length);

typedef struct {
    nes_sdr_capture_fn capture;
    void *capture_context;
    nes_sdr_refresh_fn refresh;
    void *refresh_context;
} nes_sdr_live_ops_t;

typedef enum {
    NES_SDR_LIVE_OK = 0,
    NES_SDR_LIVE_BAD_ARGUMENT,
    NES_SDR_LIVE_CAPTURE_FAILED,
    NES_SDR_LIVE_FRAME_INVALID,
    NES_SDR_LIVE_REFRESH_FAILED,
} nes_sdr_live_result_t;

typedef struct {
    uint32_t attempts;
    uint32_t frames_ok;
    uint32_t capture_failures;
    uint32_t invalid_frames;
    uint32_t refresh_failures;
} nes_sdr_live_stats_t;

/*
 * Perform one complete spectrum refresh:
 * capture one local SPC1 frame -> render 3072 graph bytes -> refresh CHR.
 *
 * The graph buffer is caller-owned so the implementation performs no heap
 * allocation and is suitable for an ESP-IDF task with a fixed workspace.
 */
nes_sdr_live_result_t nes_sdr_live_step(const nes_sdr_live_ops_t *ops,
                                        nes_sdr_live_stats_t *stats,
                                        uint8_t graph[NES_SDR_GRAPH_BYTES]);

#ifdef __cplusplus
}
#endif
