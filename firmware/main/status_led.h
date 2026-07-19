#pragma once

#include "esp_err.h"

typedef enum {
    LED_NO_IMAGE,
    LED_TRANSFERRING,
    LED_LOADING,
    LED_READY,
    LED_ERROR,
} status_led_mode_t;

esp_err_t status_led_init(void);
void status_led_set(status_led_mode_t mode);
