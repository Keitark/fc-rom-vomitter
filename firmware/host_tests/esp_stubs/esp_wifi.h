#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef struct { int num; } wifi_sta_list_t;
typedef enum { WIFI_IF_STA, WIFI_IF_AP } wifi_interface_t;
typedef enum { WIFI_BW_HT20, WIFI_BW_HT40 } wifi_bandwidth_t;
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
esp_err_t esp_wifi_set_protocol(wifi_interface_t interface, uint8_t protocol);
esp_err_t esp_wifi_set_bandwidth(wifi_interface_t interface, wifi_bandwidth_t bandwidth);
esp_err_t esp_wifi_get_protocol(wifi_interface_t interface, uint8_t *protocol);
esp_err_t esp_wifi_get_bandwidth(wifi_interface_t interface, wifi_bandwidth_t *bandwidth);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *stations);
#define WIFI_PS_NONE 0
#define WIFI_SECOND_CHAN_NONE 0
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_ps(int mode);
esp_err_t esp_wifi_set_promiscuous(bool enabled);
esp_err_t esp_wifi_set_channel(int primary, int secondary);
