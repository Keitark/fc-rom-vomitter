#pragma once
#include <stdint.h>
struct pbuf;
struct netif;
typedef struct {
    uint32_t rx, discover, request, offer, ack;
} ap_dhcp_trace_t;
int ap_dhcp_trace_input(struct pbuf *packet, struct netif *netif);
void ap_dhcp_trace_reply(struct netif *netif, int message_type);
void ap_dhcp_trace_snapshot(ap_dhcp_trace_t *status);
