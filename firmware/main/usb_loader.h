#pragma once

#include <stdbool.h>

#include "esp_err.h"

bool usb_loader_enabled(void);
esp_err_t usb_loader_start(void);
