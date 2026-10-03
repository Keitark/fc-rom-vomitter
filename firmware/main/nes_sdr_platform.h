#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

bool nes_sdr_platform_image_supported(void);
bool nes_sdr_platform_rf_available(void);
const char *nes_sdr_platform_rf_state_name(void);
typedef struct {
    uint32_t attempts;
    uint32_t frames_ok;
    int last_result; /* -1 until the first RF cycle completes */
    uint32_t period_us;
    uint32_t graph_crc32; /* Last successfully written graph, not whole ROM */
} nes_sdr_rf_status_t;
/* Atomic fields; counters cover RF cycles only, excluding demo clicks. */
void nes_sdr_platform_get_rf_status(nes_sdr_rf_status_t *status);
esp_err_t nes_sdr_platform_demo_step(void);
esp_err_t nes_sdr_platform_start_rf(void);
/* Call from an existing task, never from an HTTP handler. */
void nes_sdr_platform_service_rf_start(void);
