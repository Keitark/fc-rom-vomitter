#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ines.h"

typedef enum {
    CONTROLLER_NO_IMAGE,
    CONTROLLER_READY,
    CONTROLLER_TRANSFERRING,
    CONTROLLER_LOADING,
    CONTROLLER_ERROR,
} controller_mode_t;

typedef struct {
    controller_mode_t mode;
    bool console_power;
    bool console_exposed;
    bool has_image;
    uint32_t sequence;
    uint32_t image_crc32;
    const char *message;
} controller_status_t;

esp_err_t controller_init(void);
esp_err_t controller_install_ines(const uint8_t *data, size_t length,
                                  char *error, size_t error_length);
esp_err_t controller_install_ines_stream(ines_read_exact_fn read_exact,
                                         void *context, size_t length,
                                         char *error, size_t error_length);
esp_err_t controller_refresh_chr(const uint8_t *data, size_t length);
esp_err_t controller_refresh_chr_if_prg_matches(
    const uint8_t *data, size_t length,
    size_t prg_offset, const void *expected, size_t expected_length);
/* Hold the controller across RF capture, render, and the nested CHR refresh. */
esp_err_t controller_live_cycle_begin(size_t prg_offset,
                                      const void *expected, size_t expected_length);
void controller_live_cycle_end(void);
bool controller_prg_matches(size_t offset, const void *expected, size_t length);
void controller_get_status(controller_status_t *status);
const char *controller_mode_name(controller_mode_t mode);
