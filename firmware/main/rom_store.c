#include "rom_store.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "rom_slot.h"

#define ROM_PARTITION_TYPE ((esp_partition_type_t)0x40)
#define ROM_SLOT_DATA_OFFSET 0x1000u
#define ROM_META_MAGIC UINT32_C(0x564d4f52) /* "ROMV" little-endian */
#define ROM_META_VERSION 1u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint8_t mirroring;
    uint8_t flags;
    uint8_t reserved[38];
    uint32_t metadata_crc32;
} rom_metadata_t;

_Static_assert(sizeof(rom_metadata_t) == 64, "ROM metadata must remain 64 bytes");

static const char *TAG = "rom_store";
static const esp_partition_t *s_slots[2];

static uint32_t metadata_crc(const rom_metadata_t *metadata)
{
    return nescart_crc32(metadata, offsetof(rom_metadata_t, metadata_crc32));
}

static esp_err_t payload_crc(const esp_partition_t *partition, uint32_t *crc)
{
    uint8_t buffer[512];
    uint32_t state = nescart_crc32_begin();
    size_t offset = 0;
    while (offset < NESCART_IMAGE_SIZE) {
        size_t chunk = sizeof(buffer);
        if (chunk > NESCART_IMAGE_SIZE - offset) {
            chunk = NESCART_IMAGE_SIZE - offset;
        }
        esp_err_t err = esp_partition_read(partition, ROM_SLOT_DATA_OFFSET + offset,
                                           buffer, chunk);
        if (err != ESP_OK) {
            return err;
        }
        state = nescart_crc32_update(state, buffer, chunk);
        offset += chunk;
    }
    *crc = nescart_crc32_finish(state);
    return ESP_OK;
}

static bool read_valid_metadata(unsigned slot, rom_metadata_t *metadata)
{
    if (s_slots[slot] == NULL ||
        esp_partition_read(s_slots[slot], 0, metadata, sizeof(*metadata)) != ESP_OK) {
        return false;
    }
    if (metadata->magic != ROM_META_MAGIC ||
        metadata->version != ROM_META_VERSION ||
        metadata->header_size != sizeof(*metadata) ||
        metadata->payload_size != NESCART_IMAGE_SIZE ||
        metadata->mirroring > NESCART_MIRROR_HORIZONTAL ||
        metadata->metadata_crc32 != metadata_crc(metadata)) {
        return false;
    }
    uint32_t actual_crc = 0;
    return payload_crc(s_slots[slot], &actual_crc) == ESP_OK &&
           actual_crc == metadata->payload_crc32;
}

esp_err_t rom_store_init(void)
{
    s_slots[0] = esp_partition_find_first(ROM_PARTITION_TYPE,
                                          (esp_partition_subtype_t)0x00, "rom_a");
    s_slots[1] = esp_partition_find_first(ROM_PARTITION_TYPE,
                                          (esp_partition_subtype_t)0x01, "rom_b");
    if (s_slots[0] == NULL || s_slots[1] == NULL) {
        ESP_LOGE(TAG, "rom_a/rom_b partitions are missing");
        return ESP_ERR_NOT_FOUND;
    }
    if (s_slots[0]->size < ROM_SLOT_DATA_OFFSET + NESCART_IMAGE_SIZE ||
        s_slots[1]->size < ROM_SLOT_DATA_OFFSET + NESCART_IMAGE_SIZE) {
        ESP_LOGE(TAG, "ROM partitions are too small");
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t rom_store_load_latest(nescart_image_t *image, uint32_t *sequence)
{
    rom_metadata_t metadata[2] = {0};
    const bool valid[2] = {
        read_valid_metadata(0, &metadata[0]),
        read_valid_metadata(1, &metadata[1]),
    };
    const int selected = rom_slot_choose(valid[0], metadata[0].sequence,
                                         valid[1], metadata[1].sequence);
    if (selected < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = esp_partition_read(s_slots[selected], ROM_SLOT_DATA_OFFSET,
                                       image->data, sizeof(image->data));
    if (err != ESP_OK) {
        return err;
    }
    image->mirroring = (nescart_mirroring_t)metadata[selected].mirroring;
    image->crc32 = metadata[selected].payload_crc32;
    if (sequence != NULL) {
        *sequence = metadata[selected].sequence;
    }
    ESP_LOGI(TAG, "loaded slot %c sequence %" PRIu32 " CRC %08" PRIx32,
             'A' + selected, metadata[selected].sequence, image->crc32);
    return ESP_OK;
}

esp_err_t rom_store_commit(const nescart_image_t *image, uint32_t *new_sequence)
{
    rom_metadata_t current[2] = {0};
    const bool valid[2] = {
        read_valid_metadata(0, &current[0]),
        read_valid_metadata(1, &current[1]),
    };
    const int active = rom_slot_choose(valid[0], current[0].sequence,
                                       valid[1], current[1].sequence);
    const int target = active == 0 ? 1 : 0;
    const uint32_t sequence = active < 0 ? 1u : current[active].sequence + 1u;

    esp_err_t err = esp_partition_erase_range(s_slots[target], 0, s_slots[target]->size);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_partition_write(s_slots[target], ROM_SLOT_DATA_OFFSET,
                              image->data, sizeof(image->data));
    if (err != ESP_OK) {
        return err;
    }

    uint32_t verify_crc = 0;
    err = payload_crc(s_slots[target], &verify_crc);
    if (err != ESP_OK || verify_crc != image->crc32) {
        ESP_LOGE(TAG, "slot verify failed: expected %08" PRIx32 ", got %08" PRIx32,
                 image->crc32, verify_crc);
        return err == ESP_OK ? ESP_ERR_INVALID_CRC : err;
    }

    rom_metadata_t metadata = {
        .magic = ROM_META_MAGIC,
        .version = ROM_META_VERSION,
        .header_size = sizeof(rom_metadata_t),
        .sequence = sequence,
        .payload_size = NESCART_IMAGE_SIZE,
        .payload_crc32 = image->crc32,
        .mirroring = (uint8_t)image->mirroring,
    };
    metadata.metadata_crc32 = metadata_crc(&metadata);

    /* Commit record is written last. Power loss before this leaves the old slot valid. */
    err = esp_partition_write(s_slots[target], 0, &metadata, sizeof(metadata));
    if (err != ESP_OK) {
        return err;
    }
    if (new_sequence != NULL) {
        *new_sequence = sequence;
    }
    ESP_LOGI(TAG, "committed slot %c sequence %" PRIu32, 'A' + target, sequence);
    return ESP_OK;
}
