#pragma once

#include <stdbool.h>

#include "esp_err.h"

bool nes_sdr_platform_image_supported(void);
esp_err_t nes_sdr_platform_demo_step(void);
