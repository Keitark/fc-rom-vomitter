#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "ines.h"

esp_err_t rom_store_init(void);
esp_err_t rom_store_load_latest(nescart_image_t *image, uint32_t *sequence);
esp_err_t rom_store_commit(const nescart_image_t *image, uint32_t *new_sequence);
