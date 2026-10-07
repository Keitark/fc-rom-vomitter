#include "nes_sdr_frame.h"

#include <string.h>

void nes_sdr_reduce_bins_u8(const uint8_t *bins, size_t bin_count,
                            uint8_t heights[NES_SDR_COLUMNS])
{
    if (bins == NULL || bin_count == 0) {
        memset(heights, 0, NES_SDR_COLUMNS);
        return;
    }

    for (size_t x = 0; x < NES_SDR_COLUMNS; ++x) {
        size_t begin = (x * bin_count) / NES_SDR_COLUMNS;
        size_t end = ((x + 1) * bin_count) / NES_SDR_COLUMNS;
        if (end <= begin) {
            end = begin + 1;
        }
        if (end > bin_count) {
            end = bin_count;
        }

        uint8_t peak = 0;
        for (size_t i = begin; i < end; ++i) {
            if (bins[i] > peak) {
                peak = bins[i];
            }
        }

        /* 0..255 -> 0..64 with rounding. */
        heights[x] = (uint8_t)(((unsigned)peak * NES_SDR_GRAPH_HEIGHT + 127u) / 255u);
    }
}


void nes_sdr_reduce_fft_u8(const uint8_t *bins, size_t bin_count,
                           uint8_t heights[NES_SDR_COLUMNS])
{
    if (bins == NULL || bin_count == 0) {
        memset(heights, 0, NES_SDR_COLUMNS);
        return;
    }

    const size_t half = bin_count / 2;
    for (size_t x = 0; x < NES_SDR_COLUMNS; ++x) {
        size_t begin = (x * bin_count) / NES_SDR_COLUMNS;
        size_t end = ((x + 1) * bin_count) / NES_SDR_COLUMNS;
        if (end <= begin) {
            end = begin + 1;
        }
        if (end > bin_count) {
            end = bin_count;
        }

        uint8_t peak = 0;
        for (size_t shifted = begin; shifted < end; ++shifted) {
            const size_t natural = (shifted + half) % bin_count;
            if (bins[natural] > peak) {
                peak = bins[natural];
            }
        }

        heights[x] = (uint8_t)(((unsigned)peak * NES_SDR_GRAPH_HEIGHT + 127u) / 255u);
    }
}

void nes_sdr_render_graph(const uint8_t heights[NES_SDR_COLUMNS],
                          uint8_t graph[NES_SDR_GRAPH_BYTES])
{
    for (size_t tile_row = 0; tile_row < NES_SDR_GRAPH_TILE_ROWS; ++tile_row) {
        for (size_t x = 0; x < NES_SDR_COLUMNS; ++x) {
            const size_t tile = tile_row * NES_SDR_COLUMNS + x;
            uint8_t *dst = graph + tile * NES_SDR_TILE_BYTES;
            unsigned height = heights[x] > NES_SDR_GRAPH_HEIGHT
                                  ? NES_SDR_GRAPH_HEIGHT
                                  : heights[x];

            for (size_t py = 0; py < 8; ++py) {
                const unsigned global_y = (unsigned)(tile_row * 8 + py);
                const unsigned fill_from = NES_SDR_GRAPH_HEIGHT - height;

                /*
                 * Six-pixel-wide bar with one blank pixel on either side.
                 * Plane 0 = color 1, plane 1 = 0.
                 */
                dst[py] = global_y >= fill_from ? 0x7e : 0x00;
                dst[8 + py] = 0x00;
            }
        }
    }
}

void nes_sdr_render_chr(const uint8_t heights[NES_SDR_COLUMNS],
                        uint8_t chr[NES_SDR_CHR_BYTES])
{
    memset(chr, 0, NES_SDR_CHR_BYTES);
    nes_sdr_render_graph(heights, chr);
}
