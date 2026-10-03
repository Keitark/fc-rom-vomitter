#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional RF backend interface.
 *
 * The NES-SDR component provides weak default implementations that report
 * unavailable. A platform integration may link strong replacements, e.g. a
 * GPL ESP-SDR adapter, without making the renderer/state machine depend on
 * that implementation.
 */
bool nes_sdr_rf_backend_available(void);
bool nes_sdr_rf_backend_capture(uint32_t center_frequency_mhz,
                                uint32_t duration_ms,
                                const uint8_t **frame,
                                size_t *length);

#ifdef __cplusplus
}
#endif
