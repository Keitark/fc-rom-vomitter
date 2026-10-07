#include "ap_compat.h"
#include "esp_wifi.h"

esp_err_t ap_compat_apply(bool allow_ht)
{
    const uint8_t protocol = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                             (allow_ht ? WIFI_PROTOCOL_11N : 0);
    esp_err_t err = esp_wifi_set_protocol(WIFI_IF_AP, protocol);
    if (err != ESP_OK) return err;
    return esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
}
