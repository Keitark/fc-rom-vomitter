#include "ines.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_length, const char *format, ...)
{
    if (error == NULL || error_length == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(error, error_length, format, args);
    va_end(args);
}

uint32_t nescart_crc32_begin(void)
{
    return UINT32_C(0xffffffff);
}

uint32_t nescart_crc32_update(uint32_t state, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < length; ++i) {
        state ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(state & 1u);
            state = (state >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return state;
}

uint32_t nescart_crc32_finish(uint32_t state)
{
    return ~state;
}

uint32_t nescart_crc32(const void *data, size_t length)
{
    return nescart_crc32_finish(
        nescart_crc32_update(nescart_crc32_begin(), data, length));
}

int ines_normalize(const uint8_t *input, size_t input_length,
                   nescart_image_t *output, char *error, size_t error_length)
{
    enum { HEADER_SIZE = 16, TRAINER_SIZE = 512 };
    if (input == NULL || output == NULL) {
        set_error(error, error_length, "missing input or output buffer");
        return -1;
    }
    if (input_length < HEADER_SIZE) {
        set_error(error, error_length, "file is shorter than the 16-byte iNES header");
        return -1;
    }
    if (memcmp(input, "NES\x1a", 4) != 0) {
        set_error(error, error_length, "not an iNES file (missing NES 1A magic)");
        return -1;
    }

    const uint8_t prg_banks = input[4];
    const uint8_t chr_banks = input[5];
    const uint8_t flags6 = input[6];
    const uint8_t flags7 = input[7];
    const unsigned mapper = (unsigned)(flags6 >> 4) | (unsigned)(flags7 & 0xf0u);

    if ((flags7 & 0x0cu) == 0x08u) {
        set_error(error, error_length, "NES 2.0 images are not supported in Rev A");
        return -1;
    }
    if (mapper != 0) {
        set_error(error, error_length, "mapper %u is not supported; Rev A is NROM only", mapper);
        return -1;
    }
    if (prg_banks != 1 && prg_banks != 2) {
        set_error(error, error_length, "PRG must be 16 KiB or 32 KiB, got %u banks", prg_banks);
        return -1;
    }
    if (chr_banks != 1) {
        set_error(error, error_length, "CHR must be exactly 8 KiB, got %u banks", chr_banks);
        return -1;
    }
    if ((flags6 & 0x08u) != 0) {
        set_error(error, error_length, "four-screen mirroring is not supported");
        return -1;
    }

    const size_t trainer_size = (flags6 & 0x04u) ? TRAINER_SIZE : 0u;
    const size_t prg_size = (size_t)prg_banks * 16u * 1024u;
    const size_t chr_size = 8u * 1024u;
    const size_t expected = HEADER_SIZE + trainer_size + prg_size + chr_size;
    if (input_length != expected) {
        set_error(error, error_length, "unexpected file length: expected %zu, got %zu",
                  expected, input_length);
        return -1;
    }

    const uint8_t *prg = input + HEADER_SIZE + trainer_size;
    const uint8_t *chr = prg + prg_size;
    if (prg_banks == 1) {
        memcpy(output->data, prg, 16u * 1024u);
        memcpy(output->data + 16u * 1024u, prg, 16u * 1024u);
    } else {
        memcpy(output->data, prg, NESCART_PRG_SIZE);
    }
    memcpy(output->data + NESCART_PRG_SIZE, chr, NESCART_CHR_SIZE);

    /* U17 selects PPU_A10 at LOW (vertical) and PPU_A11 at HIGH (horizontal). */
    output->mirroring = (flags6 & 0x01u)
                            ? NESCART_MIRROR_VERTICAL
                            : NESCART_MIRROR_HORIZONTAL;
    output->crc32 = nescart_crc32(output->data, sizeof(output->data));
    if (error != NULL && error_length != 0) {
        error[0] = '\0';
    }
    return 0;
}
