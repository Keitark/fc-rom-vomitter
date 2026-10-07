#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nes_sdr_platform.h"
#include "serial_status.h"
#include "ap_dhcp_trace.h"
#include "ap_rx_trace.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

static wifi_mode_t mode;
static esp_err_t mode_error, station_error;
static int clients, queries;
static esp_err_t radio_error, dhcp_error;
static bool missing_netif;
static esp_netif_dhcp_status_t dhcp_status = ESP_NETIF_DHCP_STARTED;
static const char *rf_state = "running";
static char output[1024];
static unsigned lines;
static void emit(const char *line, void *context)
{
    CHECK(context == output);
    CHECK(strlen(output) + strlen(line) < sizeof(output));
    memcpy(output + strlen(output), line, strlen(line) + 1u); ++lines;
}
void nes_sdr_platform_get_rf_status(nes_sdr_rf_status_t *status)
{ *status = (nes_sdr_rf_status_t){100, 98, 0, 200000, 0x12345678}; }
const char *nes_sdr_platform_rf_state_name(void) { return rf_state; }
bool nes_sdr_platform_rf_available(void) { return true; }
esp_err_t esp_wifi_get_mode(wifi_mode_t *result) { *result = mode; return mode_error; }
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *stations)
{ ++queries; stations->num = clients; return station_error; }
esp_err_t esp_wifi_get_protocol(wifi_interface_t interface, uint8_t *protocol)
{ CHECK(interface == WIFI_IF_AP); *protocol = 3; return radio_error; }
esp_err_t esp_wifi_get_bandwidth(wifi_interface_t interface, wifi_bandwidth_t *bandwidth)
{ CHECK(interface == WIFI_IF_AP); *bandwidth = WIFI_BW_HT20; return radio_error; }
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key)
{ CHECK(strcmp(key, "WIFI_AP_DEF") == 0); return missing_netif ? NULL : (void *)1; }
esp_err_t esp_netif_dhcps_get_status(esp_netif_t *netif, esp_netif_dhcp_status_t *status)
{ CHECK(netif == (void *)1); *status = dhcp_status; return dhcp_error; }
const char *esp_err_to_name(esp_err_t error) { return error == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
void controller_get_status(controller_status_t *status)
{
    CHECK(lines == 4); /* Diagnostics precede a potentially busy controller. */
    *status = (controller_status_t){.has_image = true, .console_power = true,
        .console_exposed = true, .mode = CONTROLLER_READY, .sequence = 9,
        .image_crc32 = 0x92acccb3};
}
void ap_dhcp_trace_snapshot(ap_dhcp_trace_t *status)
{ *status = (ap_dhcp_trace_t){3, 2, 1, 2, 1}; }
void ap_rx_trace_snapshot(ap_rx_trace_t *status)
{ *status = (ap_rx_trace_t){5, 3, 0, 0, 0, 1, ESP_OK}; }
const char *controller_mode_name(controller_mode_t value)
{ CHECK(value == CONTROLLER_READY); return "ready"; }
size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 32768; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 16384; }
static void run(void)
{
    output[0] = 0; lines = 0; queries = 0;
    serial_status_send(emit, output);
    CHECK(lines == 5);
}
int main(void)
{
    serial_status_send(NULL, NULL);
    mode = WIFI_MODE_NULL; run();
    CHECK(strstr(output, "RVST rf=running available=1 attempts=100 frames_ok=98 result=0 period_us=200000 graph_crc32=12345678\n"));
    CHECK(strstr(output, "RVAP mode=null clients=0 error=ESP_OK ") && queries == 0);
    CHECK(strstr(output, "dhcp=off dhcp_error=ESP_OK\n"));
    CHECK(strstr(output, "RVROM image=1 power=1 exposed=1 mode=ready sequence=9"));
    CHECK(strstr(output, "RVDHCP rx=3 discover=2 request=1 offer_prepared=2 ack_prepared=1\n"));
    mode = WIFI_MODE_AP; clients = 2; rf_state = "idle"; run();
    CHECK(strstr(output, "RVAP mode=ap clients=2 error=ESP_OK ") && queries == 1);
    CHECK(strstr(output, "protocol=3 bandwidth_mhz=20 radio_error=ESP_OK dhcp=started"));
    mode = WIFI_MODE_APSTA; clients = 1; run();
    CHECK(strstr(output, "RVAP mode=apsta clients=1 error=ESP_OK "));
    dhcp_status = ESP_NETIF_DHCP_STOPPED; run();
    CHECK(strstr(output, "dhcp=stopped dhcp_error=ESP_OK"));
    dhcp_error = ESP_FAIL; run();
    CHECK(strstr(output, "dhcp=unknown dhcp_error=ESP_FAIL"));
    dhcp_error = ESP_OK; missing_netif = true; radio_error = ESP_FAIL; run();
    CHECK(strstr(output, "protocol=-1 bandwidth_mhz=-1 radio_error=ESP_FAIL"));
    CHECK(strstr(output, "dhcp=unknown dhcp_error=ESP_FAIL"));
    missing_netif = false; radio_error = ESP_OK;
    station_error = ESP_FAIL; run();
    CHECK(strstr(output, "clients=-1 error=ESP_FAIL "));
    mode_error = ESP_FAIL; run();
    CHECK(strstr(output, "RVAP mode=unknown clients=-1 error=ESP_FAIL ") && queries == 0);
    puts("Serial status: RF progress, AP clients, query errors, and response order pass");
    return 0;
}
