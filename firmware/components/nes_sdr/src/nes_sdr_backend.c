#include "nes_sdr_backend.h"

#if defined(__GNUC__)
#define NES_SDR_WEAK __attribute__((weak))
#else
#define NES_SDR_WEAK
#endif

NES_SDR_WEAK bool nes_sdr_rf_backend_available(void)
{
    return false;
}

NES_SDR_WEAK bool nes_sdr_rf_backend_capture(uint32_t center_frequency_mhz,
                                             uint32_t duration_ms,
                                             const uint8_t **frame,
                                             size_t *length)
{
    (void)center_frequency_mhz;
    (void)duration_ms;
    if (frame != NULL) {
        *frame = NULL;
    }
    if (length != NULL) {
        *length = 0;
    }
    return false;
}
