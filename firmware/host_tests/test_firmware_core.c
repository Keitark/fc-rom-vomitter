#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ines.h"
#include "rom_slot.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                                \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

static uint8_t *make_rom(unsigned prg_banks, uint8_t flags6, uint8_t flags7,
                         unsigned chr_banks, size_t *length)
{
    const size_t trainer = (flags6 & 0x04u) ? 512u : 0u;
    *length = 16u + trainer + prg_banks * 16u * 1024u + chr_banks * 8u * 1024u;
    uint8_t *rom = calloc(1, *length);
    CHECK(rom != NULL);
    memcpy(rom, "NES\x1a", 4);
    rom[4] = (uint8_t)prg_banks;
    rom[5] = (uint8_t)chr_banks;
    rom[6] = flags6;
    rom[7] = flags7;
    uint8_t *prg = rom + 16u + trainer;
    for (size_t i = 0; i < prg_banks * 16u * 1024u; ++i) {
        prg[i] = (uint8_t)(i * 17u + 3u);
    }
    uint8_t *chr = prg + prg_banks * 16u * 1024u;
    for (size_t i = 0; i < chr_banks * 8u * 1024u; ++i) {
        chr[i] = (uint8_t)(i * 29u + 7u);
    }
    return rom;
}

static void test_crc(void)
{
    CHECK(nescart_crc32("123456789", 9) == UINT32_C(0xcbf43926));
}

static void test_nrom_128(void)
{
    size_t length;
    uint8_t *rom = make_rom(1, 0x01, 0, 1, &length);
    nescart_image_t *image = calloc(1, sizeof(*image));
    CHECK(image != NULL);
    char error[128];
    CHECK(ines_normalize(rom, length, image, error, sizeof(error)) == 0);
    CHECK(image->mirroring == NESCART_MIRROR_VERTICAL);
    CHECK(memcmp(image->data, image->data + 16u * 1024u, 16u * 1024u) == 0);
    CHECK(image->crc32 == nescart_crc32(image->data, sizeof(image->data)));
    free(image);
    free(rom);
}

static void test_nrom_256_with_trainer(void)
{
    size_t length;
    uint8_t *rom = make_rom(2, 0x04, 0, 1, &length);
    nescart_image_t *image = calloc(1, sizeof(*image));
    CHECK(image != NULL);
    char error[128];
    CHECK(ines_normalize(rom, length, image, error, sizeof(error)) == 0);
    CHECK(image->mirroring == NESCART_MIRROR_HORIZONTAL);
    const uint8_t *source_prg = rom + 16u + 512u;
    CHECK(memcmp(image->data, source_prg, NESCART_PRG_SIZE) == 0);
    free(image);
    free(rom);
}

static void test_rejections(void)
{
    size_t length;
    nescart_image_t *image = calloc(1, sizeof(*image));
    CHECK(image != NULL);
    char error[128];

    uint8_t *rom = make_rom(1, 0, 0, 1, &length);
    rom[0] = 0;
    CHECK(ines_normalize(rom, length, image, error, sizeof(error)) != 0);
    free(rom);

    rom = make_rom(1, 0x10, 0, 1, &length);
    CHECK(ines_normalize(rom, length, image, error, sizeof(error)) != 0);
    free(rom);

    rom = make_rom(1, 0, 0, 0, &length);
    CHECK(ines_normalize(rom, length, image, error, sizeof(error)) != 0);
    free(rom);

    rom = make_rom(1, 0x08, 0, 1, &length);
    CHECK(ines_normalize(rom, length, image, error, sizeof(error)) != 0);
    free(rom);

    rom = make_rom(1, 0, 0x08, 1, &length);
    CHECK(ines_normalize(rom, length, image, error, sizeof(error)) != 0);
    free(rom);

    rom = make_rom(1, 0, 0, 1, &length);
    CHECK(ines_normalize(rom, length - 1u, image, error, sizeof(error)) != 0);
    free(rom);
    free(image);
}

static void test_slot_selection(void)
{
    CHECK(rom_slot_choose(false, 0, false, 0) == -1);
    CHECK(rom_slot_choose(true, 10, false, 0) == 0);
    CHECK(rom_slot_choose(false, 0, true, 10) == 1);
    CHECK(rom_slot_choose(true, 10, true, 11) == 1);
    CHECK(rom_slot_choose(true, UINT32_MAX, true, 0) == 1);
    CHECK(rom_slot_choose(true, 0, true, UINT32_MAX) == 0);
}

int main(void)
{
    test_crc();
    test_nrom_128();
    test_nrom_256_with_trainer();
    test_rejections();
    test_slot_selection();
    puts("firmware core tests passed");
    return 0;
}
