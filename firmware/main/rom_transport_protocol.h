#pragma once

#include <stddef.h>
#include <stdint.h>

#define ROM_USB_PROTOCOL_VERSION 1u
#define ROM_USB_HEADER_SIZE 16u

typedef struct {
    uint32_t payload_length;
    uint32_t payload_crc32;
} rom_usb_header_t;

void rom_usb_header_encode(uint8_t output[ROM_USB_HEADER_SIZE],
                           uint32_t payload_length, uint32_t payload_crc32);
int rom_usb_header_decode(const uint8_t *input, size_t input_length,
                          uint32_t maximum_payload, rom_usb_header_t *header,
                          char *error, size_t error_length);
