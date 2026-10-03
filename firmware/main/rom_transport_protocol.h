#pragma once

#include <stddef.h>
#include <stdint.h>

#define ROM_USB_PROTOCOL_VERSION 1u
#define ROM_USB_HEADER_SIZE 16u

typedef enum { ROM_USB_COMMAND_NONE, ROM_USB_COMMAND_UPLOAD,
               ROM_USB_COMMAND_STATUS } rom_usb_command_t;
/* Use only while seeking a command, never within an upload payload. */
rom_usb_command_t rom_usb_command_feed(unsigned *matched, uint8_t byte);

typedef struct {
    uint32_t payload_length;
    uint32_t payload_crc32;
} rom_usb_header_t;

void rom_usb_header_encode(uint8_t output[ROM_USB_HEADER_SIZE],
                           uint32_t payload_length, uint32_t payload_crc32);
int rom_usb_header_decode(const uint8_t *input, size_t input_length,
                          uint32_t maximum_payload, rom_usb_header_t *header,
                          char *error, size_t error_length);
