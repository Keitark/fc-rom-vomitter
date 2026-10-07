#include "ap_dhcp_trace.h"
#include <stdatomic.h>
#include "lwip/netif.h"
#include "lwip/pbuf.h"

static atomic_uint_least32_t rx, discover, request, offer, ack;
static int is_ap(const struct netif *netif)
{ return netif && netif->name[0] == 'a' && netif->name[1] == 'p'; }
static uint16_t be16(const uint8_t *bytes)
{ return (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]); }
static int read_at(const struct pbuf *packet, uint16_t offset, void *out, uint16_t count)
{ return pbuf_copy_partial(packet, out, count, offset) == count; }

int ap_dhcp_trace_input(struct pbuf *packet, struct netif *netif)
{
    uint8_t ip[20], udp[8], cookie[4];
    if (!is_ap(netif) || !packet || !read_at(packet, 0, ip, sizeof(ip))) return 0;
    const uint16_t ihl = (uint16_t)((ip[0] & 15u) * 4u);
    if ((ip[0] >> 4) != 4 || ihl < 20 || ip[9] != 17 ||
        (be16(ip + 6) & 0x3fff) != 0 || !read_at(packet, ihl, udp, sizeof(udp))) return 0;
    const uint16_t length = be16(udp + 4);
    if (be16(udp) != 68 || be16(udp + 2) != 67 || length < 248 ||
        (uint32_t)ihl + length > packet->tot_len ||
        (uint32_t)ihl + length > be16(ip + 2)) return 0;
    atomic_fetch_add_explicit(&rx, 1, memory_order_relaxed);
    if (!read_at(packet, (uint16_t)(ihl + 244), cookie, sizeof(cookie)) ||
        cookie[0] != 99 || cookie[1] != 130 || cookie[2] != 83 || cookie[3] != 99) return 0;
    uint16_t cursor = (uint16_t)(ihl + 248);
    const uint16_t end = (uint16_t)(ihl + length);
    while (cursor < end) {
        uint8_t code, size, type;
        if (!read_at(packet, cursor++, &code, 1) || code == 255) break;
        if (code == 0) continue;
        if (cursor >= end || !read_at(packet, cursor++, &size, 1) ||
            (uint32_t)cursor + size > end) break;
        if (code == 53 && size == 1 && read_at(packet, cursor, &type, 1)) {
            if (type == 1) atomic_fetch_add_explicit(&discover, 1, memory_order_relaxed);
            if (type == 3) atomic_fetch_add_explicit(&request, 1, memory_order_relaxed);
            break;
        }
        cursor = (uint16_t)(cursor + size);
    }
    return 0;
}
void ap_dhcp_trace_reply(struct netif *netif, int message_type)
{
    if (!is_ap(netif)) return;
    if (message_type == 2) atomic_fetch_add_explicit(&offer, 1, memory_order_relaxed);
    if (message_type == 5) atomic_fetch_add_explicit(&ack, 1, memory_order_relaxed);
}
void ap_dhcp_trace_snapshot(ap_dhcp_trace_t *status)
{
    *status = (ap_dhcp_trace_t){
        atomic_load_explicit(&rx, memory_order_relaxed),
        atomic_load_explicit(&discover, memory_order_relaxed),
        atomic_load_explicit(&request, memory_order_relaxed),
        atomic_load_explicit(&offer, memory_order_relaxed),
        atomic_load_explicit(&ack, memory_order_relaxed)};
}
