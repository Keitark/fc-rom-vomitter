#pragma once

#include <stdbool.h>

#include "esp_err.h"

bool cloud_sync_enabled(void);
const char *cloud_sync_status_name(void);
esp_err_t cloud_sync_prepare_wifi(void);
esp_err_t cloud_sync_start(void);
