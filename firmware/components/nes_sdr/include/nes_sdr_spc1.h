#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nes_sdr_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * View of the ESP-SDR "SPC1" spectrum frame.
 *
 * ESP-SDR frame layout:
 *   28-byte little-endian header
 *   1 << nfft_log2 bytes of spectrum bins
 *   4-byte CRC
 *
 * The bin code is a logarithmic power value. NES-SDR only needs the bytes;
 * the display renderer reduces them to 24 columns.
 */
typedef struct {
    uint32_t frame_number;
    uint64_t pair_index;
    uint32_t pairs;
    uint16_t ffts;
    uint8_t flags;
    uint8_t gain;
    uint16_t drops;
    uint8_t nfft_log2;
    uint8_t db_step;
    const uint8_t *bins;
    size_t bin_count;
} nes_sdr_spc1_view_t;

bool nes_sdr_spc1_parse(const uint8_t *frame, size_t length,
                        nes_sdr_spc1_view_t *view);

bool nes_sdr_spc1_to_graph(const uint8_t *frame, size_t length,
                           uint8_t graph[NES_SDR_GRAPH_BYTES]);

/* Convenience helper for callers that maintain a complete CHR image. */
bool nes_sdr_spc1_to_chr(const uint8_t *frame, size_t length,
                         uint8_t chr[NES_SDR_CHR_BYTES]);

#ifdef __cplusplus
}
#endif
