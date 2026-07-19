#include "controller.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "web_server.h"

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
    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI(TAG, "firmware prepared; hardware validation pending board arrival");
}
