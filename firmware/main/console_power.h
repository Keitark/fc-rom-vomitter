#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef void (*console_power_callback_t)(bool present, void *context);

esp_err_t console_power_init(void);
bool console_power_present(void);
esp_err_t console_power_start_monitor(console_power_callback_t callback,
                                      void *context);
