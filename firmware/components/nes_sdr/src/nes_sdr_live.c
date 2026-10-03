#include "nes_sdr_live.h"

#include "nes_sdr_spc1.h"

nes_sdr_live_result_t nes_sdr_live_step(const nes_sdr_live_ops_t *ops,
                                        nes_sdr_live_stats_t *stats,
                                        uint8_t graph[NES_SDR_GRAPH_BYTES])
{
    if (ops == NULL || stats == NULL || graph == NULL ||
        ops->capture == NULL || ops->refresh == NULL) {
        return NES_SDR_LIVE_BAD_ARGUMENT;
    }

    stats->attempts++;

    const uint8_t *frame = NULL;
    size_t frame_length = 0;
    if (!ops->capture(ops->capture_context, &frame, &frame_length) ||
        frame == NULL || frame_length == 0) {
        stats->capture_failures++;
        return NES_SDR_LIVE_CAPTURE_FAILED;
    }

    if (!nes_sdr_spc1_to_graph(frame, frame_length, graph)) {
        stats->invalid_frames++;
        return NES_SDR_LIVE_FRAME_INVALID;
    }

    if (ops->refresh(ops->refresh_context, graph, NES_SDR_GRAPH_BYTES) != 0) {
        stats->refresh_failures++;
        return NES_SDR_LIVE_REFRESH_FAILED;
    }

    stats->frames_ok++;
    return NES_SDR_LIVE_OK;
}
