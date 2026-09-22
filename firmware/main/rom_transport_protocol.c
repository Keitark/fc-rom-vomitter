#include "rom_transport_protocol.h"

#include <stdio.h>
#include <string.h>

static const uint8_t MAGIC[4] = {'R', 'V', 'U', 'P'};

static uint16_t read_le16(const uint8_t *value)
{
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t read_le32(const uint8_t *value)
{
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static void write_le16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

void rom_usb_header_encode(uint8_t output[ROM_USB_HEADER_SIZE],
                           uint32_t payload_length, uint32_t payload_crc32)
{
    memcpy(output, MAGIC, sizeof(MAGIC));
    output[4] = ROM_USB_PROTOCOL_VERSION;
    output[5] = 0;
    write_le16(output + 6, ROM_USB_HEADER_SIZE);
    write_le32(output + 8, payload_length);
    write_le32(output + 12, payload_crc32);
}

int rom_usb_header_decode(const uint8_t *input, size_t input_length,
                          uint32_t maximum_payload, rom_usb_header_t *header,
                          char *error, size_t error_length)
{
    const char *reason = NULL;
    if (input == NULL || header == NULL) {
        reason = "missing header buffer";
    } else if (input_length != ROM_USB_HEADER_SIZE) {
        reason = "USB upload header must be exactly 16 bytes";
    } else if (memcmp(input, MAGIC, sizeof(MAGIC)) != 0) {
        reason = "bad USB upload magic";
    } else if (input[4] != ROM_USB_PROTOCOL_VERSION) {
        reason = "unsupported USB upload protocol version";
    } else if (input[5] != 0 || read_le16(input + 6) != ROM_USB_HEADER_SIZE) {
        reason = "unsupported USB upload header flags or size";
    } else {
        header->payload_length = read_le32(input + 8);
        header->payload_crc32 = read_le32(input + 12);
        if (header->payload_length == 0 ||
            header->payload_length > maximum_payload) {
            reason = "USB upload payload size is outside the accepted range";
        }
    }

    if (reason != NULL) {
        if (error != NULL && error_length != 0) {
            snprintf(error, error_length, "%s", reason);
        }
        return -1;
    }
    if (error != NULL && error_length != 0) {
        error[0] = '\0';
    }
    return 0;
}
