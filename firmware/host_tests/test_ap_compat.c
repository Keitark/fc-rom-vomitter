#include <stdio.h>
#include <stdlib.h>
#include "ap_compat.h"
#include "esp_wifi.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

static unsigned calls;
static uint8_t expected_protocol;
static esp_err_t protocol_result, bandwidth_result;
esp_err_t esp_wifi_set_protocol(wifi_interface_t interface, uint8_t protocol)
{
    CHECK(interface == WIFI_IF_AP && calls++ == 0 && protocol == expected_protocol);
    return protocol_result;
}
esp_err_t esp_wifi_set_bandwidth(wifi_interface_t interface, wifi_bandwidth_t bandwidth)
{
    CHECK(interface == WIFI_IF_AP && calls++ == 1 && bandwidth == WIFI_BW_HT20);
    return bandwidth_result;
}
int main(void)
{
    expected_protocol = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G;
    CHECK(ap_compat_apply(false) == ESP_OK && calls == 2);
    calls = 0; expected_protocol |= WIFI_PROTOCOL_11N;
    CHECK(ap_compat_apply(true) == ESP_OK && calls == 2);
    calls = 0; protocol_result = ESP_FAIL;
    CHECK(ap_compat_apply(true) == ESP_FAIL && calls == 1);
    calls = 0; protocol_result = ESP_OK; bandwidth_result = ESP_FAIL;
    CHECK(ap_compat_apply(true) == ESP_FAIL && calls == 2);
    puts("AP protocol selection, 20 MHz, interface scope and failure propagation pass");
    return 0;
}
