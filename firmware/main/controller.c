#include "controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console_power.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ines.h"
#include "rom_store.h"
#include "sram_bus.h"
#include "status_led.h"

static const char *TAG = "controller";
static SemaphoreHandle_t s_lock;
static nescart_image_t *s_image;
static controller_status_t s_status = {
    .mode = CONTROLLER_NO_IMAGE,
    .message = "No image stored. Upload a mapper-0 .nes file.",
};

static void set_mode(controller_mode_t mode, const char *message)
{
    s_status.mode = mode;
    s_status.message = message;
    switch (mode) {
    case CONTROLLER_NO_IMAGE: status_led_set(LED_NO_IMAGE); break;
    case CONTROLLER_READY: status_led_set(LED_READY); break;
    case CONTROLLER_TRANSFERRING: status_led_set(LED_TRANSFERRING); break;
    case CONTROLLER_LOADING: status_led_set(LED_LOADING); break;
    case CONTROLLER_ERROR: status_led_set(LED_ERROR); break;
    }
}

static esp_err_t load_current_image(bool console_present)
{
    if (s_image == NULL) {
        sram_bus_hold_isolated();
        set_mode(CONTROLLER_NO_IMAGE, "No image stored. Upload a mapper-0 .nes file.");
        return ESP_ERR_NOT_FOUND;
    }
    set_mode(CONTROLLER_LOADING, "Copying image to SRAM and verifying readback.");
    esp_err_t err = sram_bus_load_and_verify(s_image, console_present);
    if (err != ESP_OK) {
        set_mode(CONTROLLER_ERROR, "SRAM verification failed; console remains isolated.");
        return err;
    }
    set_mode(CONTROLLER_READY,
             console_present
                 ? "READY. Press the red RESET button on the Famicom console."
                 : "Image verified. Waiting for console power.");
    return ESP_OK;
}

static void console_power_changed(bool present, void *context)
{
    (void)context;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_status.console_power = present;
    if (present) {
        (void)load_current_image(true);
    } else {
        sram_bus_hold_isolated();
        if (s_image != NULL) {
            set_mode(CONTROLLER_READY, "Image verified. Waiting for console power.");
        }
    }
    s_status.console_exposed = sram_bus_console_exposed();
    xSemaphoreGive(s_lock);
}

esp_err_t controller_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(sram_bus_init_safe(), TAG, "bus init failed");
    ESP_RETURN_ON_ERROR(status_led_init(), TAG, "LED init failed");
    ESP_RETURN_ON_ERROR(rom_store_init(), TAG, "ROM store init failed");
    ESP_RETURN_ON_ERROR(console_power_init(), TAG, "console sense init failed");

    s_image = calloc(1, sizeof(*s_image));
    if (s_image == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint32_t sequence = 0;
    esp_err_t err = rom_store_load_latest(s_image, &sequence);
    if (err == ESP_ERR_NOT_FOUND) {
        free(s_image);
        s_image = NULL;
        s_status.has_image = false;
        set_mode(CONTROLLER_NO_IMAGE, "No image stored. Upload a mapper-0 .nes file.");
    } else if (err != ESP_OK) {
        free(s_image);
        s_image = NULL;
        set_mode(CONTROLLER_ERROR, "Stored image could not be validated.");
        return err;
    } else {
        s_status.has_image = true;
        s_status.sequence = sequence;
        s_status.image_crc32 = s_image->crc32;
    }

    s_status.console_power = console_power_present();
    if (s_image != NULL) {
        err = load_current_image(s_status.console_power);
        if (err != ESP_OK) {
            return err;
        }
    } else {
        sram_bus_hold_isolated();
    }
    s_status.console_exposed = sram_bus_console_exposed();
    return console_power_start_monitor(console_power_changed, NULL);
}

esp_err_t controller_install_ines(const uint8_t *data, size_t length,
                                  char *error, size_t error_length)
{
    nescart_image_t *candidate = calloc(1, sizeof(*candidate));
    if (candidate == NULL) {
        snprintf(error, error_length, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    if (ines_normalize(data, length, candidate, error, error_length) != 0) {
        free(candidate);
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        free(candidate);
        return ESP_ERR_TIMEOUT;
    }
    set_mode(CONTROLLER_TRANSFERRING,
             "Committing the new image to the inactive flash slot.");
    uint32_t sequence = 0;
    esp_err_t err = rom_store_commit(candidate, &sequence);
    if (err != ESP_OK) {
        set_mode(s_image != NULL ? CONTROLLER_READY : CONTROLLER_ERROR,
                 "Flash commit failed; the previous image remains valid.");
        xSemaphoreGive(s_lock);
        free(candidate);
        snprintf(error, error_length, "flash commit failed: %s", esp_err_to_name(err));
        return err;
    }

    nescart_image_t *old = s_image;
    s_image = candidate;
    s_status.has_image = true;
    s_status.sequence = sequence;
    s_status.image_crc32 = candidate->crc32;
    err = load_current_image(s_status.console_power);
    s_status.console_exposed = sram_bus_console_exposed();
    xSemaphoreGive(s_lock);
    free(old);

    if (err != ESP_OK) {
        snprintf(error, error_length, "image saved, but SRAM verification failed");
        return err;
    }
    if (error != NULL && error_length != 0) {
        error[0] = '\0';
    }
    return ESP_OK;
}

void controller_get_status(controller_status_t *status)
{
    if (status == NULL) {
        return;
    }
    if (s_lock != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        *status = s_status;
        status->console_exposed = sram_bus_console_exposed();
        xSemaphoreGive(s_lock);
    } else {
        *status = s_status;
    }
}

const char *controller_mode_name(controller_mode_t mode)
{
    switch (mode) {
    case CONTROLLER_NO_IMAGE: return "no_image";
    case CONTROLLER_READY: return "ready";
    case CONTROLLER_TRANSFERRING: return "transferring";
    case CONTROLLER_LOADING: return "loading";
    case CONTROLLER_ERROR: return "error";
    default: return "unknown";
    }
}
