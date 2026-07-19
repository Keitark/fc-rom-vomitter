#pragma once

#include <stddef.h>
#include <stdint.h>

#define NESCART_PRG_SIZE (32u * 1024u)
#define NESCART_CHR_SIZE (8u * 1024u)
#define NESCART_IMAGE_SIZE (NESCART_PRG_SIZE + NESCART_CHR_SIZE)
#define NESCART_MAX_INES_SIZE (16u + 512u + NESCART_IMAGE_SIZE)

typedef enum {
    NESCART_MIRROR_VERTICAL = 0,
    NESCART_MIRROR_HORIZONTAL = 1,
} nescart_mirroring_t;

typedef struct {
    uint8_t data[NESCART_IMAGE_SIZE];
    nescart_mirroring_t mirroring;
    uint32_t crc32;
} nescart_image_t;

uint32_t nescart_crc32_begin(void);
uint32_t nescart_crc32_update(uint32_t state, const void *data, size_t length);
uint32_t nescart_crc32_finish(uint32_t state);
uint32_t nescart_crc32(const void *data, size_t length);

int ines_normalize(const uint8_t *input, size_t input_length,
                   nescart_image_t *output, char *error, size_t error_length);
