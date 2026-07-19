#include "console_power.h"

#include "board_pins.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "console_power";
static adc_oneshot_unit_handle_t s_adc;
static volatile bool s_present;
static console_power_callback_t s_callback;
static void *s_callback_context;

static bool sample_present(void)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw) != ESP_OK) {
        return false;
    }
    return raw >= CONFIG_NESCART_CONSOLE_ADC_THRESHOLD;
}

static void monitor_task(void *argument)
{
    (void)argument;
    bool candidate = s_present;
    unsigned stable_samples = 0;
    while (true) {
        const bool sampled = sample_present();
        if (sampled == candidate) {
            if (stable_samples < 5) {
                ++stable_samples;
            }
        } else {
            candidate = sampled;
            stable_samples = 1;
        }
        if (stable_samples >= 5 && candidate != s_present) {
            s_present = candidate;
            ESP_LOGI(TAG, "console power %s", s_present ? "present" : "absent");
            if (s_callback != NULL) {
                s_callback(s_present, s_callback_context);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t console_power_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_config, &s_adc);
    if (err != ESP_OK) {
        return err;
    }
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &channel_config);
    if (err != ESP_OK) {
        return err;
    }
    unsigned present_samples = 0;
    for (unsigned i = 0; i < 5; ++i) {
        present_samples += sample_present() ? 1u : 0u;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_present = present_samples >= 3;
    ESP_LOGI(TAG, "initial console power: %s", s_present ? "present" : "absent");
    return ESP_OK;
}

bool console_power_present(void)
{
    return s_present;
}

esp_err_t console_power_start_monitor(console_power_callback_t callback,
                                      void *context)
{
    s_callback = callback;
    s_callback_context = context;
    if (xTaskCreate(monitor_task, "console_power", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
