#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NES_SDR_COLUMNS = 24,
    NES_SDR_GRAPH_TILE_ROWS = 8,
    NES_SDR_GRAPH_HEIGHT = 64,
    NES_SDR_TILE_BYTES = 16,
    NES_SDR_CHR_BYTES = 8192,
    NES_SDR_GRAPH_TILES = NES_SDR_COLUMNS * NES_SDR_GRAPH_TILE_ROWS,
    NES_SDR_GRAPH_BYTES = NES_SDR_GRAPH_TILES * NES_SDR_TILE_BYTES,
    NES_SDR_BLANK_TILE = 192,
    NES_SDR_FONT_FIRST_TILE = 193
};

/* Collapse an arbitrary 8-bit spectrum into 24 display columns using max-hold. */
void nes_sdr_reduce_bins_u8(const uint8_t *bins, size_t bin_count,
                            uint8_t heights[NES_SDR_COLUMNS]);

/* Reduce bins supplied in natural FFT order (DC at bin 0), applying fftshift
 * so the frequency axis runs negative -> DC -> positive from left to right. */
void nes_sdr_reduce_fft_u8(const uint8_t *bins, size_t bin_count,
                           uint8_t heights[NES_SDR_COLUMNS]);

/*
 * Render 31 bar heights (0..64 pixels) into a complete 8 KiB NES CHR image.
 * Tile 0..247 contain the graph, tile 248 is blank, remaining tiles are zero.
 */
/* Update only the dynamic graph tiles; bytes after NES_SDR_GRAPH_BYTES are preserved. */
void nes_sdr_render_graph(const uint8_t heights[NES_SDR_COLUMNS],
                          uint8_t graph[NES_SDR_GRAPH_BYTES]);

/* Convenience helper for tests / blank CHR generation. */
void nes_sdr_render_chr(const uint8_t heights[NES_SDR_COLUMNS],
                        uint8_t chr[NES_SDR_CHR_BYTES]);

#ifdef __cplusplus
}
#endif
