#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ines.h"
#include "mapper_logic.h"
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
    CHECK(image->mapper == NESCART_MAPPER_NROM);
    CHECK(image->chr_banks == 1);
    CHECK(memcmp(image->data, image->data + 16u * 1024u, 16u * 1024u) == 0);
    CHECK(memcmp(image->data + NESCART_PRG_SIZE,
                 image->data + NESCART_PRG_SIZE + NESCART_CHR_BANK_SIZE,
                 NESCART_CHR_BANK_SIZE) == 0);
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

static void test_cnrom_two_banks(void)
{
    size_t length;
    uint8_t *rom = make_rom(2, 0x31, 0, 2, &length);
    uint8_t *source_chr = rom + 16u + NESCART_PRG_SIZE;
    source_chr[NESCART_CHR_BANK_SIZE] ^= 0xffu;
    nescart_image_t *image = calloc(1, sizeof(*image));
    CHECK(image != NULL);
    char error[128];
    CHECK(ines_normalize(rom, length, image, error, sizeof(error)) == 0);
    CHECK(image->mapper == NESCART_MAPPER_CNROM);
    CHECK(image->chr_banks == 2);
    CHECK(image->mirroring == NESCART_MIRROR_VERTICAL);
    CHECK(memcmp(image->data + NESCART_PRG_SIZE,
                 source_chr, NESCART_CHR_SIZE) == 0);
    CHECK(memcmp(image->data + NESCART_PRG_SIZE,
                 image->data + NESCART_PRG_SIZE + NESCART_CHR_BANK_SIZE,
                 NESCART_CHR_BANK_SIZE) != 0);
    CHECK(image->crc32 == nescart_crc32(image->data, sizeof(image->data)));
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

    rom = make_rom(2, 0x30, 0, 1, &length);
    CHECK(ines_normalize(rom, length, image, error, sizeof(error)) != 0);
    free(rom);

    rom = make_rom(2, 0x30, 0, 3, &length);
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

static void test_mapper3_write_qualification(void)
{
    for (unsigned load_mode = 0; load_mode < 2; ++load_mode) {
        for (unsigned romsel_inv = 0; romsel_inv < 2; ++romsel_inv) {
            for (unsigned prg_en_n = 0; prg_en_n < 2; ++prg_en_n) {
                for (unsigned d0 = 0; d0 < 2; ++d0) {
                    uint32_t control = load_mode != 0u
                                           ? MAPPER_GPIO_OUT_LOAD_MODE_MASK
                                           : 0u;
                    uint32_t sample = 0;
                    if (romsel_inv != 0u) {
                        sample |= MAPPER_GPIO_IN1_ROMSEL_INV_MASK;
                    }
                    if (prg_en_n != 0u) {
                        sample |= MAPPER_GPIO_IN1_PRG_EN_N_MASK;
                    }
                    if (d0 != 0u) {
                        sample |= MAPPER_GPIO_IN1_D0_MASK;
                    }
                    const bool write =
                        romsel_inv != 0u && prg_en_n != 0u;
                    CHECK(mapper3_sample_is_write(sample) == write);
                    CHECK(mapper3_capture_allowed(control, sample) ==
                          (load_mode == 0u && write));
                    CHECK(mapper3_sample_bank(sample) == d0);
                }
            }
        }
    }
}

static uint32_t mapper_sample(bool romsel_inv, bool prg_en_n, unsigned bank)
{
    uint32_t sample = bank != 0u ? MAPPER_GPIO_IN1_D0_MASK : 0u;
    if (romsel_inv) {
        sample |= MAPPER_GPIO_IN1_ROMSEL_INV_MASK;
    }
    if (prg_en_n) {
        sample |= MAPPER_GPIO_IN1_PRG_EN_N_MASK;
    }
    return sample;
}

static void test_mapper3_pulse_tracker(void)
{
    mapper3_tracker_t tracker;
    mapper3_tracker_reset(&tracker);

    /* A real write may begin with the preceding D0 value. Use the final one. */
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 0u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(false, true, 1u)) ==
          MAPPER3_EVENT_BANK1);

    /* A PRG read followed by a write can keep ROMSEL asserted throughout. */
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, false, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, false, 1u)) ==
          MAPPER3_EVENT_BANK1);

    /* Several consecutive PRG reads before the write must not poison it. */
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, false, 0u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, false, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 0u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 0u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, false, 0u)) ==
          MAPPER3_EVENT_BANK0);

    /* One high/high sample is U21's read-edge hazard, not a write. */
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 0u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(false, true, 0u)) ==
          MAPPER3_EVENT_NONE);

    /* The final selected D0 wins even if it appears for only one poll. */
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 0u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(false, true, 1u)) ==
          MAPPER3_EVENT_BANK1);

    /* The write ends when either qualifier falls. */
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(false, false, 1u)) ==
          MAPPER3_EVENT_BANK1);

    /* LOAD-mode interruption resets a partially observed pulse. */
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(true, true, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, MAPPER_GPIO_OUT_LOAD_MODE_MASK,
                               mapper_sample(true, true, 1u)) ==
          MAPPER3_EVENT_NONE);
    CHECK(mapper3_tracker_step(&tracker, 0u, mapper_sample(false, true, 1u)) ==
          MAPPER3_EVENT_NONE);
}

static void test_mapper3_immediate_confirmation(void)
{
    unsigned bank = 99u;

    /* U21's one-snapshot high hazard must be rejected as soon as it falls. */
    CHECK(!mapper3_confirm_write(mapper_sample(true, true, 1u),
                                 mapper_sample(true, false, 1u), &bank));

    /* A short real write needs only two snapshots; publish the later D0. */
    CHECK(mapper3_confirm_write(mapper_sample(true, true, 0u),
                                mapper_sample(true, true, 1u), &bank));
    CHECK(bank == 1u);

    CHECK(mapper3_confirm_write(mapper_sample(true, true, 1u),
                                mapper_sample(true, true, 0u), &bank));
    CHECK(bank == 0u);

    /* Either qualifier falling during confirmation invalidates the event. */
    CHECK(!mapper3_confirm_write(mapper_sample(true, true, 1u),
                                 mapper_sample(false, true, 1u), &bank));
}

int main(void)
{
    test_crc();
    test_nrom_128();
    test_nrom_256_with_trainer();
    test_cnrom_two_banks();
    test_rejections();
    test_slot_selection();
    test_mapper3_write_qualification();
    test_mapper3_pulse_tracker();
    test_mapper3_immediate_confirmation();
    puts("firmware core tests passed");
    return 0;
}
