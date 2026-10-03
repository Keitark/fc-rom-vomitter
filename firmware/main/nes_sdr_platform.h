#pragma once

#include <stdbool.h>

#include "esp_err.h"

bool nes_sdr_platform_image_supported(void);
bool nes_sdr_platform_rf_available(void);
const char *nes_sdr_platform_rf_state_name(void);
esp_err_t nes_sdr_platform_demo_step(void);
esp_err_t nes_sdr_platform_start_rf(void);
