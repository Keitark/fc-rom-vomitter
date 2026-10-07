#include "nes_sdr_spc1.h"

#include <string.h>

enum {
    SPC1_HEADER_BYTES = 28,
    SPC1_CRC_BYTES = 4,
};

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

bool nes_sdr_spc1_parse(const uint8_t *frame, size_t length,
                        nes_sdr_spc1_view_t *view)
{
    if (frame == NULL || view == NULL || length < SPC1_HEADER_BYTES + SPC1_CRC_BYTES) {
        return false;
    }

    if (memcmp(frame, "SPC1", 4) != 0) {
        return false;
    }

    const uint8_t log2n = frame[26];
    if (log2n < 8 || log2n > 11) {
        return false;
    }

    const size_t bins = (size_t)1u << log2n;
    const size_t required = SPC1_HEADER_BYTES + bins + SPC1_CRC_BYTES;
    if (length < required) {
        return false;
    }

    view->frame_number = get32(frame + 4);
    view->pair_index = get64(frame + 8);
    view->pairs = get32(frame + 16);
    view->ffts = get16(frame + 20);
    view->flags = frame[22];
    view->gain = frame[23];
    view->drops = get16(frame + 24);
    view->nfft_log2 = log2n;
    view->db_step = frame[27];
    view->bins = frame + SPC1_HEADER_BYTES;
    view->bin_count = bins;
    return true;
}

bool nes_sdr_spc1_to_graph(const uint8_t *frame, size_t length,
                           uint8_t graph[NES_SDR_GRAPH_BYTES])
{
    nes_sdr_spc1_view_t view;
    uint8_t heights[NES_SDR_COLUMNS];

    if (!nes_sdr_spc1_parse(frame, length, &view)) {
        return false;
    }

    nes_sdr_reduce_fft_u8(view.bins, view.bin_count, heights);
    nes_sdr_render_graph(heights, graph);
    return true;
}

bool nes_sdr_spc1_to_chr(const uint8_t *frame, size_t length,
                         uint8_t chr[NES_SDR_CHR_BYTES])
{
    return nes_sdr_spc1_to_graph(frame, length, chr);
}
