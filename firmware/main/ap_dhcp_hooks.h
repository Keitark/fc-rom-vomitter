#pragma once
#include "ap_dhcp_trace.h"
/* Observers retain packet ownership and append no DHCP options. */
#define LWIP_HOOK_IP4_INPUT(packet, netif) ap_dhcp_trace_input(packet, netif)
#define LWIP_HOOK_DHCPS_POST_APPEND_OPTS(netif, server, state, end) \
    ap_dhcp_trace_reply(netif, state);
