#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef void (*console_power_callback_t)(bool present, void *context);

esp_err_t console_power_init(void);
bool console_power_present(void);
/* Fresh ADC sample for the RUN transition after a timed RF capture. */
bool console_power_sample_now(void);
esp_err_t console_power_start_monitor(console_power_callback_t callback,
                                      void *context);
