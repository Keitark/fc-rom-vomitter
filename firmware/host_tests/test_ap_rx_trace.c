#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ap_rx_trace.h"
#include "esp_heap_caps.h"
#include "esp_wifi_netif.h"
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)
static esp_alloc_failed_hook_t failed;
static unsigned forwarded;
static esp_err_t input_result;
static void *expected_netif, *expected_buffer, *expected_eb;
static size_t expected_length;
esp_err_t __real_esp_netif_receive(esp_netif_t *, void *, size_t, void *);
esp_err_t __real_esp_wifi_register_if_rxcb(wifi_netif_driver_t driver, esp_netif_receive_t fn, void *arg)
{ CHECK(driver == (void *)4 && fn == __real_esp_netif_receive && arg == expected_netif); return input_result; }
esp_err_t __wrap_esp_wifi_register_if_rxcb(wifi_netif_driver_t, esp_netif_receive_t, void *);
esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback)
{ failed = callback; return ESP_OK; }
esp_err_t __real_esp_netif_receive(esp_netif_t *netif, void *buffer, size_t length, void *eb)
{
    CHECK(netif == expected_netif && buffer == expected_buffer);
    CHECK(length == expected_length && eb == expected_eb);
    ++forwarded; return input_result;
}
esp_err_t __wrap_esp_netif_receive(esp_netif_t *, void *, size_t, void *);
int main(void)
{
    uint8_t bytes[20] = {0}, original[20];
    bytes[12] = 8; memcpy(original, bytes, sizeof(bytes));
    expected_netif = (void *)1; expected_buffer = bytes;
    expected_eb = (void *)2; expected_length = sizeof(bytes);
    CHECK(ap_rx_trace_init() == ESP_OK && failed != NULL);
    CHECK(__wrap_esp_netif_receive(expected_netif, bytes, sizeof(bytes), expected_eb) == ESP_OK);
    ap_rx_trace_bind(expected_netif);
    CHECK(__wrap_esp_wifi_register_if_rxcb((void *)4, __real_esp_netif_receive, expected_netif) == ESP_OK);
    CHECK(__wrap_esp_netif_receive(expected_netif, bytes, sizeof(bytes), expected_eb) == ESP_OK);
    input_result = ESP_FAIL;
    CHECK(__wrap_esp_netif_receive(expected_netif, bytes, sizeof(bytes), expected_eb) == ESP_FAIL);
    CHECK(memcmp(bytes, original, sizeof(bytes)) == 0);
    expected_length = 5;
    CHECK(__wrap_esp_netif_receive(expected_netif, bytes, 5, expected_eb) == ESP_FAIL);
    expected_netif = (void *)3;
    CHECK(__wrap_esp_netif_receive(expected_netif, bytes, 5, expected_eb) == ESP_FAIL);
    failed(1600, MALLOC_CAP_INTERNAL, "example");
    failed(1024, MALLOC_CAP_8BIT, "example");
    ap_rx_trace_t result; ap_rx_trace_snapshot(&result);
    CHECK(result.frames == 3 && result.ipv4 == 2 && result.input_error == 2);
    CHECK(result.alloc_fail == 2 && result.alloc_last_size == 1024 && forwarded == 5);
    CHECK(result.registered == 1 && result.registration_error == ESP_OK);
    CHECK(__wrap_esp_wifi_register_if_rxcb((void *)4, __real_esp_netif_receive, expected_netif) == ESP_FAIL);
    ap_rx_trace_snapshot(&result);
    CHECK(result.registered == 1); /* A different interface cannot change AP status. */
    expected_netif = (void *)1;
    CHECK(__wrap_esp_wifi_register_if_rxcb((void *)4, __real_esp_netif_receive, expected_netif) == ESP_FAIL);
    ap_rx_trace_snapshot(&result);
    CHECK(result.registered == 0 && result.registration_error == ESP_FAIL);
    puts("AP RX trace: arguments, ownership, errors, AP isolation, and allocation counters pass");
    return 0;
}
