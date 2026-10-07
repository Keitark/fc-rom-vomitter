#pragma once
#include <stdint.h>
struct pbuf { struct pbuf *next; const void *payload; uint16_t tot_len, len; };
uint16_t pbuf_copy_partial(const struct pbuf *packet, void *out, uint16_t count, uint16_t offset);
