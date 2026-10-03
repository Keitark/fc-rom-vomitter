#pragma once
#include "esp_err.h"
typedef void esp_netif_t;
typedef enum { ESP_NETIF_DHCP_INIT, ESP_NETIF_DHCP_STARTED,
               ESP_NETIF_DHCP_STOPPED } esp_netif_dhcp_status_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
esp_err_t esp_netif_dhcps_get_status(esp_netif_t *netif, esp_netif_dhcp_status_t *status);
