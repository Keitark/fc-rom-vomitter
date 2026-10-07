#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_8BIT 2u
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps, const char *function);
esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback);
