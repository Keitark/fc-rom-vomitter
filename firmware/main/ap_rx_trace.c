#include "ap_rx_trace.h"
#include <stdatomic.h>
#include "esp_heap_caps.h"

static esp_netif_t *ap_netif;
static atomic_uint_least32_t frames, ipv4, input_error, alloc_fail, alloc_last_size;
static void allocation_failed(size_t size, uint32_t caps, const char *function)
{
    (void)caps; (void)function;
    atomic_fetch_add_explicit(&alloc_fail, 1, memory_order_relaxed);
    atomic_store_explicit(&alloc_last_size, (uint32_t)size, memory_order_relaxed);
}
esp_err_t ap_rx_trace_init(void)
{ return heap_caps_register_failed_alloc_callback(allocation_failed); }
void ap_rx_trace_bind(esp_netif_t *ap) { ap_netif = ap; }

esp_err_t __real_esp_netif_receive(esp_netif_t *netif, void *buffer, size_t length, void *eb);
esp_err_t __wrap_esp_netif_receive(esp_netif_t *netif, void *buffer, size_t length, void *eb)
{
    const int observed = ap_netif != NULL && netif == ap_netif;
    if (observed) {
        atomic_fetch_add_explicit(&frames, 1, memory_order_relaxed);
        if (buffer && length >= 14) {
            const uint8_t *bytes = buffer;
            if (bytes[12] == 8 && bytes[13] == 0)
                atomic_fetch_add_explicit(&ipv4, 1, memory_order_relaxed);
        }
    }
    const esp_err_t result = __real_esp_netif_receive(netif, buffer, length, eb);
    if (observed && result != ESP_OK)
        atomic_fetch_add_explicit(&input_error, 1, memory_order_relaxed);
    return result;
}
void ap_rx_trace_snapshot(ap_rx_trace_t *status)
{
    *status = (ap_rx_trace_t){
        atomic_load_explicit(&frames, memory_order_relaxed),
        atomic_load_explicit(&ipv4, memory_order_relaxed),
        atomic_load_explicit(&input_error, memory_order_relaxed),
        atomic_load_explicit(&alloc_fail, memory_order_relaxed),
        atomic_load_explicit(&alloc_last_size, memory_order_relaxed)};
}
