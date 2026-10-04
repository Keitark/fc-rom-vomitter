#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif.h"
typedef struct {
    uint32_t frames, ipv4, input_error, alloc_fail, alloc_last_size;
} ap_rx_trace_t;
esp_err_t ap_rx_trace_init(void);
void ap_rx_trace_bind(esp_netif_t *ap);
void ap_rx_trace_snapshot(ap_rx_trace_t *status);
