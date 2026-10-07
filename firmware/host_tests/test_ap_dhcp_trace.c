#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ap_dhcp_trace.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)
uint16_t pbuf_copy_partial(const struct pbuf *packet, void *out, uint16_t count, uint16_t offset)
{
    uint16_t copied = 0;
    for (; packet && copied < count; packet = packet->next) {
        if (offset >= packet->len) { offset = (uint16_t)(offset - packet->len); continue; }
        uint16_t take = (uint16_t)(packet->len - offset);
        if (take > count - copied) take = (uint16_t)(count - copied);
        memcpy((uint8_t *)out + copied, (const uint8_t *)packet->payload + offset, take);
        copied = (uint16_t)(copied + take); offset = 0;
    }
    return copied;
}
int main(void)
{
    uint8_t bytes[280] = {0}, original[280];
    struct netif ap = {{'a', 'p'}}, sta = {{'s', 't'}};
    bytes[0] = 0x45; bytes[2] = 1; bytes[3] = 24; bytes[9] = 17;
    bytes[21] = 68; bytes[23] = 67; bytes[24] = 1; bytes[25] = 4;
    bytes[264] = 99; bytes[265] = 130; bytes[266] = 83; bytes[267] = 99;
    /* Padded and preceding non-type option, including a split at the cookie. */
    bytes[268] = 0; bytes[269] = 12; bytes[270] = 2; bytes[271] = 'p'; bytes[272] = 'c';
    bytes[273] = 53; bytes[274] = 1; bytes[275] = 1; bytes[276] = 255;
    struct pbuf tail = {NULL, bytes + 266, 14, 14};
    struct pbuf head = {&tail, bytes, 280, 266};
    memcpy(original, bytes, sizeof(bytes));
    CHECK(ap_dhcp_trace_input(&head, &sta) == 0);
    CHECK(ap_dhcp_trace_input(&head, &ap) == 0);
    CHECK(memcmp(original, bytes, sizeof(bytes)) == 0 && head.next == &tail);
    bytes[275] = 3; CHECK(ap_dhcp_trace_input(&head, &ap) == 0);
    bytes[270] = 255; CHECK(ap_dhcp_trace_input(&head, &ap) == 0); /* Invalid option size. */
    bytes[264] = 0; CHECK(ap_dhcp_trace_input(&head, &ap) == 0); /* Invalid cookie. */
    bytes[6] = 0x20; CHECK(ap_dhcp_trace_input(&head, &ap) == 0); /* Fragment. */
    bytes[6] = 0; head.tot_len = 20; CHECK(ap_dhcp_trace_input(&head, &ap) == 0);
    CHECK(ap_dhcp_trace_input(NULL, &ap) == 0);
    ap_dhcp_trace_reply(&sta, 2);
    ap_dhcp_trace_reply(&ap, 2); ap_dhcp_trace_reply(&ap, 5); ap_dhcp_trace_reply(&ap, 6);
    ap_dhcp_trace_t result; ap_dhcp_trace_snapshot(&result);
    CHECK(result.rx == 4 && result.discover == 1 && result.request == 1);
    CHECK(result.offer == 1 && result.ack == 1);
    puts("DHCP trace: chained packets, malformed options, AP isolation, and passive ownership pass");
    return 0;
}
