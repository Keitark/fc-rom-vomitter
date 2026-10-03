#include "controller.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "usb_loader.h"
#include "web_server.h"
#include "sdkconfig.h"
#if CONFIG_NESCART_NES_SDR_LIVE_ENABLE
#include "nes_sdr_task.h"
#endif

static const char *TAG = "rom_vomitter";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(controller_init());
    ESP_ERROR_CHECK(usb_loader_start());
#if CONFIG_NESCART_NES_SDR_LIVE_ENABLE
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(nes_sdr_task_start());
#else
    ESP_ERROR_CHECK(web_server_start());
#endif
    ESP_LOGI(TAG, "firmware prepared; hardware validation pending board arrival");
}
