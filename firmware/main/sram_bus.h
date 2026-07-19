#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "ines.h"

esp_err_t sram_bus_init_safe(void);
void sram_bus_hold_isolated(void);
esp_err_t sram_bus_load_and_verify(const nescart_image_t *image,
                                   bool expose_to_console);
void sram_bus_expose_to_console(nescart_mirroring_t mirroring);
bool sram_bus_console_exposed(void);
