#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

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
    bool live_armed;
    uint32_t sequence;
    uint32_t image_crc32;
    const char *message;
} controller_status_t;

esp_err_t controller_init(void);
esp_err_t controller_install_ines(const uint8_t *data, size_t length,
                                  char *error, size_t error_length);
/* Requires a powered console displaying the CPU-RAM parked NES-SDR ROM. */
esp_err_t controller_refresh_chr(const uint8_t *data, size_t length);
/* Manual bench arm after the NES-SDR ROM is visibly running from CPU RAM. */
esp_err_t controller_live_arm(void);
/* Holds the controller lock across RF capture and CHR refresh. */
esp_err_t controller_live_begin(void);
void controller_live_end(void);
void controller_get_status(controller_status_t *status);
const char *controller_mode_name(controller_mode_t mode);
