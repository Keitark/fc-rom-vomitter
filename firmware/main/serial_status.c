#include "serial_status.h"

#include <inttypes.h>
#include <stdio.h>

#include "controller.h"
#include "ap_dhcp_trace.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nes_sdr_platform.h"

static const char *wifi_mode_name(wifi_mode_t mode)
{
    switch (mode) {
    case WIFI_MODE_NULL: return "null";
    case WIFI_MODE_STA: return "sta";
    case WIFI_MODE_AP: return "ap";
    case WIFI_MODE_APSTA: return "apsta";
    default: return "unknown";
    }
}

void serial_status_send(serial_status_emit_fn emit, void *context)
{
    if (emit == NULL) return;
    char line[224];
    nes_sdr_rf_status_t rf;
    nes_sdr_platform_get_rf_status(&rf);
    snprintf(line, sizeof(line),
             "RVST rf=%s available=%u attempts=%" PRIu32
             " frames_ok=%" PRIu32 " result=%d period_us=%" PRIu32
             " graph_crc32=%08" PRIx32 "\n",
             nes_sdr_platform_rf_state_name(), nes_sdr_platform_rf_available() ? 1u : 0u,
             rf.attempts, rf.frames_ok, rf.last_result, rf.period_us, rf.graph_crc32);
    emit(line, context);

    wifi_mode_t mode = WIFI_MODE_NULL;
    const esp_err_t mode_err = esp_wifi_get_mode(&mode);
    esp_err_t err = mode_err;
    int clients = -1; /* Never turn a failed query into a zero-client claim. */
    const char *mode_name = err == ESP_OK ? wifi_mode_name(mode) : "unknown";
    if (err == ESP_OK) {
        if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
            wifi_sta_list_t stations;
            err = esp_wifi_ap_get_sta_list(&stations);
            if (err == ESP_OK) clients = stations.num;
        } else {
            clients = 0;
        }
    }
    int protocol = -1, bandwidth_mhz = -1;
    const char *dhcp_name = "off";
    esp_err_t radio_err = ESP_OK, dhcp_err = ESP_OK;
    if (mode_err == ESP_OK && (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)) {
        uint8_t actual_protocol;
        wifi_bandwidth_t bandwidth;
        radio_err = esp_wifi_get_protocol(WIFI_IF_AP, &actual_protocol);
        if (radio_err == ESP_OK) protocol = actual_protocol;
        if (radio_err == ESP_OK) {
            radio_err = esp_wifi_get_bandwidth(WIFI_IF_AP, &bandwidth);
            if (radio_err == ESP_OK) bandwidth_mhz = bandwidth == WIFI_BW_HT20 ? 20 : 40;
        }
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_dhcp_status_t dhcp = ESP_NETIF_DHCP_INIT;
        dhcp_err = netif == NULL ? ESP_ERR_INVALID_STATE : esp_netif_dhcps_get_status(netif, &dhcp);
        dhcp_name = dhcp_err != ESP_OK ? "unknown" :
                    dhcp == ESP_NETIF_DHCP_STARTED ? "started" :
                    dhcp == ESP_NETIF_DHCP_STOPPED ? "stopped" : "init";
    } else if (mode_err != ESP_OK) {
        dhcp_name = "unknown";
        radio_err = dhcp_err = err;
    }
    snprintf(line, sizeof(line),
             "RVAP mode=%s clients=%d error=%s protocol=%d bandwidth_mhz=%d"
             " radio_error=%s dhcp=%s dhcp_error=%s\n",
             mode_name, clients, esp_err_to_name(err), protocol, bandwidth_mhz,
             esp_err_to_name(radio_err), dhcp_name, esp_err_to_name(dhcp_err));
    emit(line, context);

    ap_dhcp_trace_t dhcp_trace;
    ap_dhcp_trace_snapshot(&dhcp_trace);
    snprintf(line, sizeof(line),
             "RVDHCP rx=%" PRIu32 " discover=%" PRIu32 " request=%" PRIu32
             " offer_prepared=%" PRIu32 " ack_prepared=%" PRIu32 "\n",
             dhcp_trace.rx, dhcp_trace.discover, dhcp_trace.request,
             dhcp_trace.offer, dhcp_trace.ack);
    emit(line, context);

    /* Send RF/AP first so a busy controller cannot hide their status. */
    controller_status_t rom;
    controller_get_status(&rom);
    snprintf(line, sizeof(line),
             "RVROM image=%u power=%u exposed=%u mode=%s sequence=%" PRIu32
             " image_crc32=%08" PRIx32 " heap_free=%u heap_largest=%u\n",
             rom.has_image ? 1u : 0u, rom.console_power ? 1u : 0u,
             rom.console_exposed ? 1u : 0u, controller_mode_name(rom.mode),
             rom.sequence, rom.image_crc32,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    emit(line, context);
}
