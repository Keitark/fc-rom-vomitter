#include "status_led.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static volatile status_led_mode_t s_mode = LED_NO_IMAGE;

static void led_task(void *argument)
{
    (void)argument;
    unsigned error_phase = 0;
    while (true) {
        switch (s_mode) {
        case LED_READY:
            gpio_set_level(PIN_LED_STATUS, 1);
            vTaskDelay(pdMS_TO_TICKS(250));
            break;
        case LED_NO_IMAGE:
            gpio_set_level(PIN_LED_STATUS, !gpio_get_level(PIN_LED_STATUS));
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        case LED_TRANSFERRING:
        case LED_LOADING:
            gpio_set_level(PIN_LED_STATUS, !gpio_get_level(PIN_LED_STATUS));
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        case LED_ERROR:
            gpio_set_level(PIN_LED_STATUS, error_phase < 4 && !(error_phase & 1u));
            error_phase = (error_phase + 1u) % 12u;
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
        if (s_mode != LED_ERROR) {
            error_phase = 0;
        }
    }
}

esp_err_t status_led_init(void)
{
    gpio_set_level(PIN_LED_STATUS, 0);
    ESP_ERROR_CHECK(gpio_set_direction(PIN_LED_STATUS, GPIO_MODE_OUTPUT));
    if (xTaskCreate(led_task, "status_led", 2048, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void status_led_set(status_led_mode_t mode)
{
    s_mode = mode;
}
